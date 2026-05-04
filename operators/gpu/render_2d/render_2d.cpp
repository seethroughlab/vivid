#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_2d.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// =============================================================================
// Render2D — terminal drawable-pipeline rasteriser (MVP: SHAPE type only)
// =============================================================================
//
// Consumes a VividDrawable2D tree (single root, optionally with children[])
// and rasterises every leaf drawable to ctx->output_texture_view.
//
// MVP scope (E.1.5):
//   - SHAPE drawables only (SPRITE and TEXT come later)
//   - Single-instance drawables (instance_buffer is ignored — Instancer2D
//     support arrives in E.1.6)
//   - Alpha blending (src-over) only (all 5 blend modes land in E.2)
//   - No z_layer sort (traversal order = draw order for now)
//   - Fixed orthographic viewport: [-1, 1] in NDC, matching the emitter
//     transform convention.
//
// Rendering approach (Render3D pattern, adapted to 2D):
//   - One big uniform buffer with 256-byte slots (WebGPU dynamic-offset
//     alignment). Each collected drawable gets one slot.
//   - One render pass per frame, multiple Draw(6) calls, each with a
//     different dynamic offset into the uniform buffer.
//   - Single cached pipeline variant for now (shape + alpha + no-instancing).
// =============================================================================

static constexpr uint32_t kSlotSize = 256;  // WebGPU min-dynamic-offset alignment

// Uniform payload bound for each drawable. Padded to kSlotSize so the
// dynamic-offset slots line up.
struct Render2DUniforms {
    float transform[6];           // mat3x2 column-major
    float _pad_xform[2];
    float color[4];
    // shape_params.x = sides (as float), .y = star_factor, .z = softness, .w = type
    float shape_params[4];
    float viewport[2];            // output_width, output_height (currently unused but available)
    float _pad_viewport[2];
    float _tail_pad[kSlotSize / 4 - 20];  // pad to exactly kSlotSize bytes
};
static_assert(sizeof(Render2DUniforms) == kSlotSize, "Render2DUniforms must be kSlotSize bytes");

// Shared uniform + SDF helpers — used by both pipeline variants below.
static const char* kRender2DCommonWGSL = R"(
struct Uniforms {
    transform: mat3x2f,
    color: vec4f,
    shape_params: vec4f,  // x=sides, y=star_factor, z=softness, w=type
    viewport: vec4f,
};

@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) local: vec2f,
    @location(1) inst_color: vec4f,  // used by instanced variant only
};

fn unit_quad(vid: u32) -> vec2f {
    var positions = array<vec2f, 6>(
        vec2f(-1.0, -1.0),
        vec2f( 1.0, -1.0),
        vec2f(-1.0,  1.0),
        vec2f( 1.0, -1.0),
        vec2f( 1.0,  1.0),
        vec2f(-1.0,  1.0)
    );
    return positions[vid];
}

// Aspect-ratio correction: users work in square NDC [-1, 1], but the output
// texture may be wider than it is tall. Divide x by aspect ratio so shapes
// keep their intended proportions on non-square viewports.
fn apply_aspect(world: vec2f) -> vec2f {
    let aspect = u.viewport.x / max(u.viewport.y, 1.0);
    return vec2f(world.x / aspect, world.y);
}

// SDF evaluation in local [-1, 1] coords, implicit size 1.0.
// PI is defined by WGSL_CONSTANTS (prepended in lazy_init).
fn sd_shape(p: vec2f) -> f32 {
    let sides = u.shape_params.x;
    let sf = u.shape_params.y;
    let size = 1.0;

    if (sides < 1.0) {
        return length(p) - size;
    }

    let an = PI / sides;
    let angle = atan2(p.y, p.x);

    if (sf < 0.001) {
        let sector = round(angle / (2.0 * an));
        let folded = angle - sector * 2.0 * an;
        let q = length(p) * vec2f(cos(folded), abs(sin(folded)));
        return q.x - size * cos(an);
    } else {
        let inner_r = size * (1.0 - sf);
        let sector = round(angle / (2.0 * an));
        let folded = abs(angle - sector * 2.0 * an);
        let lp = length(p);
        let q = vec2f(lp * cos(folded), lp * sin(folded));
        let v0 = vec2f(size, 0.0);
        let v1 = vec2f(inner_r * cos(an), inner_r * sin(an));
        let edge = v1 - v0;
        let normal = normalize(vec2f(edge.y, -edge.x));
        return dot(q - v0, normal);
    }
}

fn shape_color(in_local: vec2f, inst_color: vec4f) -> vec4f {
    let d = sd_shape(in_local);
    let softness = max(u.shape_params.z, 0.0001);
    let alpha_mask = 1.0 - smoothstep(-softness, softness, d);
    let c = u.color * inst_color;
    let a = c.a * alpha_mask;
    return vec4f(c.rgb * a, a);
}

// Tint a sampled texture colour by the drawable color + per-instance color,
// with premultiplied-alpha output matching shape_color()'s convention.
fn sprite_color_tinted(sampled: vec4f, inst_color: vec4f) -> vec4f {
    let c = sampled * u.color * inst_color;
    return vec4f(c.rgb * c.a, c.a);
}

// Shared instance record layout (used by instanced variants).
struct InstanceData2D {
    transform: mat3x2f,
    color: vec4f,
};
)";

// Single-instance shader — one draw per drawable, drawable's transform
// directly drives the quad placement. Uses group 0 (uniforms) only.
static const char* kRender2DShapeSingleShader = R"(
@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VertexOutput {
    let local = unit_quad(vid);
    let world = u.transform * vec3f(local, 1.0);
    var out: VertexOutput;
    out.position = vec4f(apply_aspect(world), 0.0, 1.0);
    out.local = local;
    out.inst_color = vec4f(1.0);
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return shape_color(in.local, in.inst_color);
}
)";

// Shape-instanced shader — DrawIndexed(6, N). Per-instance transform + color
// from storage buffer at group 1. The drawable's own transform is applied
// first (local shape size), then the per-instance transform (placement).
static const char* kRender2DShapeInstancedShader = R"(
@group(1) @binding(0) var<storage, read> instances: array<InstanceData2D>;

@vertex
fn vs_main(@builtin(vertex_index) vid: u32,
           @builtin(instance_index) iid: u32) -> VertexOutput {
    let local = unit_quad(vid);
    let inst = instances[iid];
    let after_draw = u.transform * vec3f(local, 1.0);
    let world      = inst.transform * vec3f(after_draw, 1.0);
    var out: VertexOutput;
    out.position = vec4f(apply_aspect(world), 0.0, 1.0);
    out.local = local;
    out.inst_color = inst.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return shape_color(in.local, in.inst_color);
}
)";

// Sprite-single shader — one textured quad per drawable.
// group 1: texture + sampler.
static const char* kRender2DSpriteSingleShader = R"(
@group(1) @binding(0) var sprite_tex: texture_2d<f32>;
@group(1) @binding(1) var sprite_samp: sampler;

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VertexOutput {
    let local = unit_quad(vid);
    let world = u.transform * vec3f(local, 1.0);
    var out: VertexOutput;
    out.position = vec4f(apply_aspect(world), 0.0, 1.0);
    out.local = local;
    out.inst_color = vec4f(1.0);
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Map local [-1, 1] → uv [0, 1], flip Y so the sprite matches the
    // orientation of upstream textures (most texture sources are top-down).
    let uv = vec2f(in.local.x * 0.5 + 0.5, 0.5 - in.local.y * 0.5);
    let tex = textureSample(sprite_tex, sprite_samp, uv);
    return sprite_color_tinted(tex, in.inst_color);
}
)";

// Sprite-instanced shader — DrawIndexed(6, N) textured quads.
// group 1: instance storage buffer. group 2: texture + sampler.
static const char* kRender2DSpriteInstancedShader = R"(
@group(1) @binding(0) var<storage, read> instances: array<InstanceData2D>;
@group(2) @binding(0) var sprite_tex: texture_2d<f32>;
@group(2) @binding(1) var sprite_samp: sampler;

@vertex
fn vs_main(@builtin(vertex_index) vid: u32,
           @builtin(instance_index) iid: u32) -> VertexOutput {
    let local = unit_quad(vid);
    let inst = instances[iid];
    let after_draw = u.transform * vec3f(local, 1.0);
    let world      = inst.transform * vec3f(after_draw, 1.0);
    var out: VertexOutput;
    out.position = vec4f(apply_aspect(world), 0.0, 1.0);
    out.local = local;
    out.inst_color = inst.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let uv = vec2f(in.local.x * 0.5 + 0.5, 0.5 - in.local.y * 0.5);
    let tex = textureSample(sprite_tex, sprite_samp, uv);
    return sprite_color_tinted(tex, in.inst_color);
}
)";

// Text-instanced shader — one quad per glyph. Vertex shader reads the
// per-glyph transform + uv_rect from a storage buffer; fragment shader
// samples the atlas (single-channel R8Unorm) and uses it as alpha over the
// per-glyph colour. WGSL struct layout matches CPU-side GlyphInstance2D (64 B).
static const char* kRender2DTextInstancedShader = R"(
struct GlyphInstance {
    transform: mat3x2f,
    uv_rect: vec4f,
    color: vec4f,
};

@group(1) @binding(0) var<storage, read> glyphs: array<GlyphInstance>;
@group(2) @binding(0) var text_atlas: texture_2d<f32>;
@group(2) @binding(1) var text_samp:  sampler;

struct GlyphVertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv:    vec2f,
    @location(1) color: vec4f,
};

fn quad_uv(vid: u32) -> vec2f {
    // 6 verts forming two tris. u=0 left, u=1 right, v=0 top, v=1 bottom.
    var uvs = array<vec2f, 6>(
        vec2f(0.0, 1.0),   // bottom-left (local -1,-1)
        vec2f(1.0, 1.0),   // bottom-right
        vec2f(0.0, 0.0),   // top-left
        vec2f(1.0, 1.0),   // bottom-right (tri 2)
        vec2f(1.0, 0.0),   // top-right
        vec2f(0.0, 0.0)    // top-left
    );
    return uvs[vid];
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32,
           @builtin(instance_index) iid: u32) -> GlyphVertexOutput {
    let local = unit_quad(vid);
    let g = glyphs[iid];
    let after_draw = u.transform * vec3f(local, 1.0);
    let world      = g.transform  * vec3f(after_draw, 1.0);
    var out: GlyphVertexOutput;
    out.position = vec4f(apply_aspect(world), 0.0, 1.0);
    // Map the local [-1,1] quad to the glyph's sub-rect of the atlas.
    let uv01 = quad_uv(vid);
    out.uv   = vec2f(
        mix(g.uv_rect.x, g.uv_rect.z, uv01.x),
        mix(g.uv_rect.y, g.uv_rect.w, uv01.y)
    );
    out.color = g.color;
    return out;
}

@fragment
fn fs_main(in: GlyphVertexOutput) -> @location(0) vec4f {
    // Atlas is R8Unorm — coverage lives in the red channel.
    let coverage = textureSample(text_atlas, text_samp, in.uv).r;
    let tint = u.color * in.color;
    let a    = tint.a * coverage;
    return vec4f(tint.rgb * a, a);
}
)";

/**
 * @brief Terminal rasterizer for the 2D drawable pipeline.
 *
 * Takes a VividDrawable2D tree, walks it, sorts children by z_layer, and
 * issues per-drawable DrawIndexed calls (one pipeline variant per
 * shape/sprite/instancing combo). Emits a standard `gpu_texture` so
 * downstream texture-chain operators (Bloom, Feedback, etc.) continue from
 * there.
 *
 * @param bg_r / bg_g / bg_b / bg_a  Background clear colour.
 *
 * @tip Every drawable graph terminates at Render2D — wire its texture output to video_out.
 * @tip Switch blend modes on the upstream drawable (Shape2D/Particles2D) to control look.
 * @recipe Shape2D -> Render2D -> video_out
 * @recipe Shape2D -> Render2D -> Bloom -> video_out
 * @common_companions Shape2D, Sprite2D, Particles2D, Instancer2D, Bloom, video_out
 * @best_used_with video_out, Bloom
 * @family 2D drawable pipeline
 * @see Shape2D, Instancer2D, Particles2D
 */
struct Render2D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Render2D";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_REDUCTION;

    vivid::Param<float> bg_r {"bg_r", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_g {"bg_g", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_b {"bg_b", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> bg_a {"bg_a", 1.0f, 0.0f, 1.0f};

    Render2D() {
        vivid::display_hint(bg_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(bg_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(bg_b, VIVID_DISPLAY_COLOR);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(bg_r, "Background");
        vivid::param_group(bg_g, "Background");
        vivid::param_group(bg_b, "Background");
        vivid::param_group(bg_a, "Background");
        out.push_back(&bg_r);
        out.push_back(&bg_g);
        out.push_back(&bg_b);
        out.push_back(&bg_a);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(vivid::gpu::drawable_port("drawable", VIVID_PORT_INPUT));
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    ~Render2D() override {
        for (auto& e : pipeline_cache_) vivid::gpu::release(e.pipeline);
        pipeline_cache_.clear();
        vivid::gpu::release(shader_shape_single_);
        vivid::gpu::release(shader_shape_instanced_);
        vivid::gpu::release(shader_sprite_single_);
        vivid::gpu::release(shader_sprite_instanced_);
        vivid::gpu::release(shader_text_instanced_);
        vivid::gpu::release(text_sampler_);
        vivid::gpu::release(pipe_layout_shape_single_);
        vivid::gpu::release(pipe_layout_shape_instanced_);
        vivid::gpu::release(pipe_layout_sprite_single_);
        vivid::gpu::release(pipe_layout_sprite_instanced_);
        vivid::gpu::release(bind_layout_uniforms_);
        vivid::gpu::release(bind_layout_storage_);
        vivid::gpu::release(bind_layout_texture_);
        vivid::gpu::release(ubo_bind_group_);
        vivid::gpu::release(ubo_);
        for (auto& e : storage_bind_cache_) vivid::gpu::release(e.bind_group);
        for (auto& e : texture_bind_cache_) vivid::gpu::release(e.bind_group);
        storage_bind_cache_.clear();
        texture_bind_cache_.clear();
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!ctx->output_texture_view) return;

        if (!lazy_init_done_ && !lazy_init(ctx)) return;

        // Collect drawables by walking the input tree in traversal order.
        collected_.clear();
        auto* root = vivid::gpu::drawable_input(ctx, 0);
        if (root) {
            collect_recursive(root);
        }

        // Stable-sort by z_layer. NaN z_layer is treated as 0, so drawables
        // that haven't opted into explicit ordering just keep traversal order
        // (stable sort preserves relative position within the z=0 bucket).
        // Painter's-algorithm semantics: depth test stays off, lower z draws
        // first (behind), higher z draws last (on top).
        std::stable_sort(collected_.begin(), collected_.end(),
            [](const vivid::gpu::VividDrawable2D* a, const vivid::gpu::VividDrawable2D* b) {
                const float az = std::isnan(a->z_layer) ? 0.0f : a->z_layer;
                const float bz = std::isnan(b->z_layer) ? 0.0f : b->z_layer;
                return az < bz;
            });

        const uint32_t n = static_cast<uint32_t>(collected_.size());

        ensure_ubo_capacity(ctx, n > 0 ? n : 1);

        // Pack per-drawable uniforms.
        uniform_staging_.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            const auto* d = collected_[i];
            auto& u = uniform_staging_[i];
            std::memset(&u, 0, sizeof(u));
            for (int k = 0; k < 6; ++k) u.transform[k] = d->transform[k];
            for (int k = 0; k < 4; ++k) u.color[k] = d->color[k];
            u.shape_params[0] = static_cast<float>(d->shape_sides);
            u.shape_params[1] = d->shape_star_factor;
            u.shape_params[2] = d->shape_softness > 0.0f ? d->shape_softness : 0.01f;
            u.shape_params[3] = static_cast<float>(d->type);
            u.viewport[0]     = static_cast<float>(ctx->output_width);
            u.viewport[1]     = static_cast<float>(ctx->output_height);
        }
        if (n > 0) {
            wgpuQueueWriteBuffer(ctx->queue, ubo_, 0,
                                 uniform_staging_.data(),
                                 static_cast<uint64_t>(n) * kSlotSize);
        }

        // Single render pass — clear, then draw each collected drawable.
        WGPURenderPassColorAttachment ca{};
        ca.view        = ctx->output_texture_view;
        ca.depthSlice  = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp      = WGPULoadOp_Clear;
        ca.storeOp     = WGPUStoreOp_Store;
        ca.clearValue  = {bg_r.value, bg_g.value, bg_b.value, bg_a.value};

        WGPURenderPassDescriptor rp{};
        rp.label                = vivid_sv("Render2D Pass");
        rp.colorAttachmentCount = 1;
        rp.colorAttachments     = &ca;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            ctx->command_encoder, &rp);

        WGPURenderPipeline last_pipeline = nullptr;

        for (uint32_t i = 0; i < n; ++i) {
            const auto* d = collected_[i];
            const uint32_t offset = i * kSlotSize;
            const bool is_text      = (d->type == vivid::gpu::VIVID_DRAWABLE2D_TEXT)
                                       && d->text_atlas_view && d->text_glyph_buffer
                                       && d->text_glyph_count > 0;
            const bool is_sprite    = (d->type == vivid::gpu::VIVID_DRAWABLE2D_SPRITE)
                                       && d->texture_view && d->texture_sampler;
            const bool is_instanced = (d->instance_buffer && d->instance_count > 0);

            WGPURenderPipeline target = get_or_create_pipeline(
                ctx, is_text, is_sprite, is_instanced, d->blend_mode);
            if (!target) continue;
            if (target != last_pipeline) {
                wgpuRenderPassEncoderSetPipeline(pass, target);
                last_pipeline = target;
            }

            wgpuRenderPassEncoderSetBindGroup(pass, 0, ubo_bind_group_, 1, &offset);

            if (is_text) {
                WGPUBindGroup sbg = get_or_create_storage_bind_group(ctx, d->text_glyph_buffer);
                WGPUBindGroup tbg = get_or_create_texture_bind_group(ctx, d->text_atlas_view,
                                                                     text_sampler(ctx));
                if (!sbg || !tbg) continue;
                wgpuRenderPassEncoderSetBindGroup(pass, 1, sbg, 0, nullptr);
                wgpuRenderPassEncoderSetBindGroup(pass, 2, tbg, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 6, d->text_glyph_count, 0, 0);
            } else if (is_sprite && is_instanced) {
                WGPUBindGroup sbg = get_or_create_storage_bind_group(ctx, d->instance_buffer);
                WGPUBindGroup tbg = get_or_create_texture_bind_group(ctx, d->texture_view,
                                                                     d->texture_sampler);
                if (!sbg || !tbg) continue;
                wgpuRenderPassEncoderSetBindGroup(pass, 1, sbg, 0, nullptr);
                wgpuRenderPassEncoderSetBindGroup(pass, 2, tbg, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 6, d->instance_count, 0, 0);
            } else if (is_sprite) {
                WGPUBindGroup tbg = get_or_create_texture_bind_group(ctx, d->texture_view,
                                                                     d->texture_sampler);
                if (!tbg) continue;
                wgpuRenderPassEncoderSetBindGroup(pass, 1, tbg, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
            } else if (is_instanced) {
                WGPUBindGroup sbg = get_or_create_storage_bind_group(ctx, d->instance_buffer);
                if (!sbg) continue;
                wgpuRenderPassEncoderSetBindGroup(pass, 1, sbg, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 6, d->instance_count, 0, 0);
            } else {
                wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
            }
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

private:
    // Linear sampler shared by all TEXT drawables — TEXT drawables don't
    // carry their own sampler (they reference only the atlas view).
    WGPUSampler         text_sampler_             = nullptr;

    WGPUSampler text_sampler(const VividGpuContext* ctx) {
        if (text_sampler_) return text_sampler_;
        WGPUSamplerDescriptor sd{};
        sd.label         = vivid_sv("Render2D text sampler");
        sd.addressModeU  = WGPUAddressMode_ClampToEdge;
        sd.addressModeV  = WGPUAddressMode_ClampToEdge;
        sd.addressModeW  = WGPUAddressMode_ClampToEdge;
        sd.magFilter     = WGPUFilterMode_Linear;
        sd.minFilter     = WGPUFilterMode_Linear;
        sd.mipmapFilter  = WGPUMipmapFilterMode_Nearest;
        sd.maxAnisotropy = 1;
        text_sampler_    = wgpuDeviceCreateSampler(ctx->device, &sd);
        return text_sampler_;
    }

    // Shader modules — 5 variants (shape / sprite × single / instanced + text_instanced).
    WGPUShaderModule    shader_shape_single_      = nullptr;
    WGPUShaderModule    shader_shape_instanced_   = nullptr;
    WGPUShaderModule    shader_sprite_single_     = nullptr;
    WGPUShaderModule    shader_sprite_instanced_  = nullptr;
    WGPUShaderModule    shader_text_instanced_    = nullptr;

    // Pipeline layouts — 5 variants (text reuses sprite_instanced's layout
    // since the binding shape is identical: uniforms + storage + texture).
    WGPUPipelineLayout  pipe_layout_shape_single_    = nullptr;
    WGPUPipelineLayout  pipe_layout_shape_instanced_ = nullptr;
    WGPUPipelineLayout  pipe_layout_sprite_single_   = nullptr;
    WGPUPipelineLayout  pipe_layout_sprite_instanced_= nullptr;

    // Pipeline cache keyed on (is_text, is_sprite, is_instanced, blend_mode).
    // is_text is the highest-priority discriminator — when set, is_sprite is
    // ignored (the TEXT path samples an R8 atlas via its own shader).
    // Up to 5 × 4 = 20 entries.
    struct PipelineEntry {
        bool                 is_text;
        bool                 is_sprite;
        bool                 is_instanced;
        vivid::gpu::VividBlendMode blend;
        WGPURenderPipeline   pipeline;
    };
    std::vector<PipelineEntry> pipeline_cache_;
    bool lazy_init_done_ = false;

    // Bind group layouts — three kinds, reused across pipelines:
    //   uniforms  (group 0 everywhere)
    //   storage   (group 1 in *-instanced pipelines)
    //   texture   (group 1 in sprite-single, group 2 in sprite-instanced)
    WGPUBindGroupLayout bind_layout_uniforms_ = nullptr;
    WGPUBindGroupLayout bind_layout_storage_  = nullptr;
    WGPUBindGroupLayout bind_layout_texture_  = nullptr;

    // Per-drawable uniform buffer (dynamic-offset slots) + its bind group.
    WGPUBuffer    ubo_               = nullptr;
    WGPUBindGroup ubo_bind_group_    = nullptr;
    uint32_t      ubo_slot_capacity_ = 0;

    // Per-drawable storage-buffer bind groups, cached by WGPUBuffer pointer.
    struct StorageBindEntry {
        WGPUBuffer    buffer     = nullptr;
        WGPUBindGroup bind_group = nullptr;
    };
    std::vector<StorageBindEntry> storage_bind_cache_;

    // Per-drawable texture bind groups, cached by (texture_view, sampler).
    struct TextureBindEntry {
        WGPUTextureView view       = nullptr;
        WGPUSampler     sampler    = nullptr;
        WGPUBindGroup   bind_group = nullptr;
    };
    std::vector<TextureBindEntry> texture_bind_cache_;

    std::vector<const vivid::gpu::VividDrawable2D*> collected_;
    std::vector<Render2DUniforms>                   uniform_staging_;

    void collect_recursive(const vivid::gpu::VividDrawable2D* d) {
        if (!d) return;
        if (d->child_count > 0 && d->children) {
            for (uint32_t i = 0; i < d->child_count; ++i) {
                collect_recursive(d->children[i]);
            }
            return;
        }
        // SHAPE, SPRITE, and TEXT variants are rasterised in this phase.
        if (d->type == vivid::gpu::VIVID_DRAWABLE2D_SHAPE ||
            d->type == vivid::gpu::VIVID_DRAWABLE2D_SPRITE ||
            d->type == vivid::gpu::VIVID_DRAWABLE2D_TEXT) {
            collected_.push_back(d);
        }
    }

    WGPUBindGroup get_or_create_texture_bind_group(const VividGpuContext* ctx,
                                                    WGPUTextureView view,
                                                    WGPUSampler sampler) {
        for (auto& e : texture_bind_cache_) {
            if (e.view == view && e.sampler == sampler) return e.bind_group;
        }
        WGPUBindGroupEntry entries[2]{};
        entries[0].binding     = 0;
        entries[0].textureView = view;
        entries[1].binding     = 1;
        entries[1].sampler     = sampler;
        WGPUBindGroupDescriptor bg{};
        bg.label      = vivid_sv("Render2D Texture BG");
        bg.layout     = bind_layout_texture_;
        bg.entryCount = 2;
        bg.entries    = entries;
        WGPUBindGroup bind = wgpuDeviceCreateBindGroup(ctx->device, &bg);
        texture_bind_cache_.push_back({view, sampler, bind});
        return bind;
    }

    WGPUBindGroup get_or_create_storage_bind_group(const VividGpuContext* ctx,
                                                   WGPUBuffer buffer) {
        for (auto& e : storage_bind_cache_) {
            if (e.buffer == buffer) return e.bind_group;
        }
        WGPUBindGroupEntry be{};
        be.binding = 0;
        be.buffer  = buffer;
        be.offset  = 0;
        be.size    = WGPU_WHOLE_SIZE;
        WGPUBindGroupDescriptor bg{};
        bg.label      = vivid_sv("Render2D Instances BG");
        bg.layout     = bind_layout_storage_;
        bg.entryCount = 1;
        bg.entries    = &be;
        WGPUBindGroup bind = wgpuDeviceCreateBindGroup(ctx->device, &bg);
        storage_bind_cache_.push_back({buffer, bind});
        return bind;
    }

    void ensure_ubo_capacity(const VividGpuContext* ctx, uint32_t needed_slots) {
        if (ubo_ && ubo_slot_capacity_ >= needed_slots) return;
        uint32_t new_cap = ubo_slot_capacity_ > 0 ? ubo_slot_capacity_ : 16;
        while (new_cap < needed_slots) new_cap *= 2;
        vivid::gpu::release(ubo_);
        vivid::gpu::release(ubo_bind_group_);
        WGPUBufferDescriptor bd{};
        bd.label = vivid_sv("Render2D UBO");
        bd.size  = static_cast<uint64_t>(new_cap) * kSlotSize;
        bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        ubo_ = wgpuDeviceCreateBuffer(ctx->device, &bd);
        ubo_slot_capacity_ = new_cap;
        rebuild_ubo_bind_group(ctx);
    }

    void rebuild_ubo_bind_group(const VividGpuContext* ctx) {
        if (!bind_layout_uniforms_ || !ubo_) return;
        WGPUBindGroupEntry e{};
        e.binding = 0;
        e.buffer  = ubo_;
        e.offset  = 0;
        e.size    = kSlotSize;
        WGPUBindGroupDescriptor bg{};
        bg.label      = vivid_sv("Render2D UBO BG");
        bg.layout     = bind_layout_uniforms_;
        bg.entryCount = 1;
        bg.entries    = &e;
        ubo_bind_group_ = wgpuDeviceCreateBindGroup(ctx->device, &bg);
    }

    WGPUShaderModule compile_shader(const VividGpuContext* ctx,
                                    const char* variant_src, const char* label) {
        std::string wgsl = std::string(vivid::gpu::WGSL_CONSTANTS)
                         + kRender2DCommonWGSL
                         + variant_src;
        WGPUShaderSourceWGSL src{};
        src.chain.sType = WGPUSType_ShaderSourceWGSL;
        src.code        = vivid_sv(wgsl.c_str());
        WGPUShaderModuleDescriptor sm{};
        sm.nextInChain = &src.chain;
        sm.label       = vivid_sv(label);
        return wgpuDeviceCreateShaderModule(ctx->device, &sm);
    }

    // Map a VividBlendMode to WGPU blend factors. The shader outputs
    // premultiplied colour (rgb * a, a) so src factors stay at One for ALPHA.
    static void blend_state_for(vivid::gpu::VividBlendMode mode, WGPUBlendState& blend) {
        blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha.operation = WGPUBlendOperation_Add;
        switch (mode) {
            case vivid::gpu::VIVID_BLEND_ADDITIVE:
                blend.color.srcFactor = WGPUBlendFactor_One;
                blend.color.dstFactor = WGPUBlendFactor_One;
                blend.alpha.srcFactor = WGPUBlendFactor_One;
                blend.alpha.dstFactor = WGPUBlendFactor_One;
                break;
            case vivid::gpu::VIVID_BLEND_MULTIPLY:
                blend.color.srcFactor = WGPUBlendFactor_Dst;
                blend.color.dstFactor = WGPUBlendFactor_Zero;
                blend.alpha.srcFactor = WGPUBlendFactor_One;
                blend.alpha.dstFactor = WGPUBlendFactor_Zero;
                break;
            case vivid::gpu::VIVID_BLEND_SCREEN:
                blend.color.srcFactor = WGPUBlendFactor_One;
                blend.color.dstFactor = WGPUBlendFactor_OneMinusSrc;
                blend.alpha.srcFactor = WGPUBlendFactor_One;
                blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrc;
                break;
            case vivid::gpu::VIVID_BLEND_OVERLAY:
                // OVERLAY requires destination-read in the fragment shader —
                // out of scope for E.2. Fall through to ALPHA so visuals remain
                // sensible rather than silently broken.
            case vivid::gpu::VIVID_BLEND_ALPHA:
            default:
                blend.color.srcFactor = WGPUBlendFactor_One;
                blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
                blend.alpha.srcFactor = WGPUBlendFactor_One;
                blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
                break;
        }
    }

    WGPURenderPipeline build_pipeline(const VividGpuContext* ctx,
                                       WGPUShaderModule shader,
                                       WGPUPipelineLayout layout,
                                       vivid::gpu::VividBlendMode blend_mode,
                                       const char* label) {
        WGPUBlendState blend{};
        blend_state_for(blend_mode, blend);

        WGPUColorTargetState target{};
        target.format    = ctx->output_format;
        target.blend     = &blend;
        target.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fs{};
        fs.module      = shader;
        fs.entryPoint  = vivid_sv("fs_main");
        fs.targetCount = 1;
        fs.targets     = &target;

        WGPURenderPipelineDescriptor rpd{};
        rpd.label             = vivid_sv(label);
        rpd.layout            = layout;
        rpd.vertex.module     = shader;
        rpd.vertex.entryPoint = vivid_sv("vs_main");
        rpd.vertex.bufferCount = 0;
        rpd.primitive.topology  = WGPUPrimitiveTopology_TriangleList;
        rpd.primitive.frontFace = WGPUFrontFace_CCW;
        rpd.primitive.cullMode  = WGPUCullMode_None;
        rpd.multisample.count = 1;
        rpd.multisample.mask  = 0xFFFFFFFF;
        rpd.fragment          = &fs;
        return wgpuDeviceCreateRenderPipeline(ctx->device, &rpd);
    }

    bool lazy_init(const VividGpuContext* ctx) {
        // --- Bind group layout: uniforms (group 0 in every pipeline) ---
        {
            WGPUBindGroupLayoutEntry ble{};
            ble.binding    = 0;
            ble.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
            ble.buffer.type             = WGPUBufferBindingType_Uniform;
            ble.buffer.hasDynamicOffset = true;
            ble.buffer.minBindingSize   = kSlotSize;
            WGPUBindGroupLayoutDescriptor bld{};
            bld.label      = vivid_sv("Render2D BGL Uniforms");
            bld.entryCount = 1;
            bld.entries    = &ble;
            bind_layout_uniforms_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bld);
        }

        // --- Bind group layout: instance storage buffer (group 1 in *-instanced) ---
        {
            WGPUBindGroupLayoutEntry ble{};
            ble.binding    = 0;
            ble.visibility = WGPUShaderStage_Vertex;
            ble.buffer.type             = WGPUBufferBindingType_ReadOnlyStorage;
            ble.buffer.minBindingSize   = 0;
            WGPUBindGroupLayoutDescriptor bld{};
            bld.label      = vivid_sv("Render2D BGL Storage");
            bld.entryCount = 1;
            bld.entries    = &ble;
            bind_layout_storage_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bld);
        }

        // --- Bind group layout: sprite texture + sampler ---
        {
            WGPUBindGroupLayoutEntry entries[2]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;
            WGPUBindGroupLayoutDescriptor bld{};
            bld.label      = vivid_sv("Render2D BGL Texture");
            bld.entryCount = 2;
            bld.entries    = entries;
            bind_layout_texture_ = wgpuDeviceCreateBindGroupLayout(ctx->device, &bld);
        }

        // --- Pipeline layouts ---
        auto make_layout = [&](const char* label,
                               std::initializer_list<WGPUBindGroupLayout> layouts) -> WGPUPipelineLayout {
            WGPUPipelineLayoutDescriptor pld{};
            pld.label                = vivid_sv(label);
            pld.bindGroupLayoutCount = static_cast<uint32_t>(layouts.size());
            pld.bindGroupLayouts     = layouts.begin();
            return wgpuDeviceCreatePipelineLayout(ctx->device, &pld);
        };

        pipe_layout_shape_single_     = make_layout("Render2D PL shape-single",
                                                     {bind_layout_uniforms_});
        pipe_layout_shape_instanced_  = make_layout("Render2D PL shape-instanced",
                                                     {bind_layout_uniforms_, bind_layout_storage_});
        pipe_layout_sprite_single_    = make_layout("Render2D PL sprite-single",
                                                     {bind_layout_uniforms_, bind_layout_texture_});
        pipe_layout_sprite_instanced_ = make_layout("Render2D PL sprite-instanced",
                                                     {bind_layout_uniforms_, bind_layout_storage_, bind_layout_texture_});

        // --- Shaders ---
        shader_shape_single_    = compile_shader(ctx, kRender2DShapeSingleShader,     "Render2D SH shape-single");
        shader_shape_instanced_ = compile_shader(ctx, kRender2DShapeInstancedShader,  "Render2D SH shape-instanced");
        shader_sprite_single_   = compile_shader(ctx, kRender2DSpriteSingleShader,    "Render2D SH sprite-single");
        shader_sprite_instanced_= compile_shader(ctx, kRender2DSpriteInstancedShader, "Render2D SH sprite-instanced");
        shader_text_instanced_  = compile_shader(ctx, kRender2DTextInstancedShader,   "Render2D SH text-instanced");
        if (!shader_shape_single_ || !shader_shape_instanced_ ||
            !shader_sprite_single_ || !shader_sprite_instanced_ ||
            !shader_text_instanced_) {
            std::fprintf(stderr, "[render_2d] shader compile failed\n");
            return false;
        }

        // --- Pre-create the four ALPHA pipelines so first-frame perf matches
        // the E.1 baseline. Other blend modes are lazily compiled on first use.
        for (bool is_sprite : {false, true}) {
            for (bool is_instanced : {false, true}) {
                get_or_create_pipeline(ctx, false, is_sprite, is_instanced,
                                       vivid::gpu::VIVID_BLEND_ALPHA);
            }
        }
        // Pre-create the TEXT ALPHA pipeline too.
        get_or_create_pipeline(ctx, true, false, true, vivid::gpu::VIVID_BLEND_ALPHA);

        ensure_ubo_capacity(ctx, 16);
        lazy_init_done_ = true;
        return true;
    }

    WGPURenderPipeline get_or_create_pipeline(const VividGpuContext* ctx,
                                              bool is_text,
                                              bool is_sprite, bool is_instanced,
                                              vivid::gpu::VividBlendMode blend) {
        for (auto& e : pipeline_cache_) {
            if (e.is_text == is_text && e.is_sprite == is_sprite &&
                e.is_instanced == is_instanced && e.blend == blend) {
                return e.pipeline;
            }
        }
        WGPUShaderModule shader = nullptr;
        WGPUPipelineLayout layout = nullptr;
        const char* label = nullptr;
        if (is_text) {
            shader = shader_text_instanced_;
            layout = pipe_layout_sprite_instanced_;  // identical binding shape
            label  = "Render2D PIPE text-instanced";
        } else if (is_sprite) {
            if (is_instanced) {
                shader = shader_sprite_instanced_;
                layout = pipe_layout_sprite_instanced_;
                label  = "Render2D PIPE sprite-instanced";
            } else {
                shader = shader_sprite_single_;
                layout = pipe_layout_sprite_single_;
                label  = "Render2D PIPE sprite-single";
            }
        } else {
            if (is_instanced) {
                shader = shader_shape_instanced_;
                layout = pipe_layout_shape_instanced_;
                label  = "Render2D PIPE shape-instanced";
            } else {
                shader = shader_shape_single_;
                layout = pipe_layout_shape_single_;
                label  = "Render2D PIPE shape-single";
            }
        }
        WGPURenderPipeline p = build_pipeline(ctx, shader, layout, blend, label);
        if (!p) {
            std::fprintf(stderr, "[render_2d] pipeline build failed for text=%d sprite=%d inst=%d blend=%u\n",
                         (int)is_text, (int)is_sprite, (int)is_instanced, (unsigned)blend);
            return nullptr;
        }
        pipeline_cache_.push_back({is_text, is_sprite, is_instanced, blend, p});
        return p;
    }
};

VIVID_DEFINE_OP(Render2D) {
}

VIVID_REGISTER(Render2D)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::VividDrawable2D)
