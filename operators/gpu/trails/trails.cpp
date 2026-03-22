// Trails / Ribbons — Persistent motion trails with per-voice role bindings.
//
// Autonomous trail heads move with configurable speed and curvature. A ping-pong
// feedback texture creates trail persistence through per-frame decay. Three
// PER_VOICE role bindings (width_mod, opacity_mod, color_shift) allow independent
// modulation per trail. Optional texture input composites behind the trails.

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/bound_control_instance.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

static constexpr int kMaxTrails = 32;

// Simple LCG hash for deterministic pseudo-random initialization
static float hash_float(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>(seed >> 8) / 16777216.0f; // [0, 1)
}

// ── WGSL fragment shader ────────────────────────────────────────────────

static const char* kTrailsFragment = R"(

struct Uniforms {
    resolution: vec2f,
    time: f32,
    active_count: f32,
    decay: f32,
    glow: f32,
    has_input: f32,
    _pad: f32,
    base_color: vec4f,
    trails_geo:   array<vec4f, 32>,  // xy=current_pos, zw=prev_pos
    trails_style: array<vec4f, 32>,  // x=width, y=opacity, z=hue_shift, w=unused
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var feedbackTex: texture_2d<f32>;
@group(0) @binding(3) var inputTex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

// Capsule SDF — line segment with rounded ends
fn sdCapsule(p: vec2f, a: vec2f, b: vec2f, r: f32) -> f32 {
    let pa = p - a;
    let ba = b - a;
    let h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h) - r;
}

// Hue rotation via Rodrigues formula around (1,1,1)/sqrt(3)
fn hueRotate(color: vec3f, angle: f32) -> vec3f {
    let c = cos(angle);
    let s = sin(angle);
    let k = vec3f(0.57735026919); // 1/sqrt(3)
    return color * c + cross(k, color) * s + k * dot(k, color) * (1.0 - c);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // 1. Sample & decay previous frame
    let fb = textureSample(feedbackTex, texSampler, input.uv) * u.decay;

    // 2. Optional input texture
    var input_color = vec4f(0.0);
    if (u.has_input > 0.5) {
        input_color = textureSample(inputTex, texSampler, input.uv);
    }

    // 3. Draw new trail segments
    let aspect = u.resolution.x / u.resolution.y;
    let uv = vec2f(input.uv.x * aspect, input.uv.y);
    let n = i32(u.active_count);
    let softness = 0.003;

    var trail_accum = vec4f(0.0);

    for (var i = 0; i < n; i++) {
        let geo = u.trails_geo[i];
        let style = u.trails_style[i];

        let cur = vec2f(geo.x * aspect, geo.y);
        let prev = vec2f(geo.z * aspect, geo.w);
        let w = style.x;
        let opacity = style.y;
        let hue_shift = style.z;

        // Capsule SDF for trail segment
        let d = sdCapsule(uv, cur, prev, w);
        let alpha = (1.0 - smoothstep(-softness, softness, d)) * opacity;

        // Apply hue-shifted color
        let color = hueRotate(u.base_color.rgb, hue_shift);

        // Glow: wider, softer capsule
        var glow_alpha = 0.0;
        if (u.glow > 0.01) {
            let glow_r = w * 3.0;
            let d_glow = sdCapsule(uv, cur, prev, glow_r);
            glow_alpha = (1.0 - smoothstep(0.0, glow_r, d_glow)) * u.glow * 0.3 * opacity;
        }

        let total_alpha = max(alpha, glow_alpha);
        trail_accum += vec4f(color * total_alpha, total_alpha);
    }

    // 4. Composite: input + max(feedback, new trails)
    let trails = vec4f(
        max(fb.rgb, min(trail_accum.rgb, vec3f(1.0))),
        max(fb.a, min(trail_accum.a, 1.0))
    );

    return vec4f(
        max(input_color.rgb, trails.rgb),
        max(input_color.a, trails.a)
    );
}
)";

// ── Uniform struct (must match WGSL layout exactly) ─────────────────────

struct TrailsUniforms {
    float resolution[2];              // vec2f
    float time;                       // f32
    float active_count;               // f32
    float decay;                      // f32
    float glow;                       // f32
    float has_input;                  // f32
    float _pad;                       // f32
    float base_color[4];              // vec4f
    float trails_geo[kMaxTrails * 4];   // array<vec4f, 32>
    float trails_style[kMaxTrails * 4]; // array<vec4f, 32>
};

// ── Trail state ─────────────────────────────────────────────────────────

struct Trail {
    float x = 0.5f, y = 0.5f;           // current position [0,1]
    float prev_x = 0.5f, prev_y = 0.5f; // previous position (capsule endpoint)
    float angle = 0.0f;                  // heading direction
    float curve_offset = 0.0f;           // per-trail curvature variation
};

// ── Role binding pool ───────────────────────────────────────────────────

struct RolePool {
    std::vector<std::unique_ptr<vivid::BoundControlInstance>> pool;
    bool initialized = false;
    VividCreateBindableFn  cached_create_fn  = nullptr;
    VividDestroyBindableFn cached_destroy_fn = nullptr;
};

// ── Operator ────────────────────────────────────────────────────────────

struct Trails : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "Trails";
    static constexpr bool kTimeDependent = true;

    // Count & decay
    vivid::Param<int>   count     {"count",     8,     1,     kMaxTrails};
    vivid::Param<float> decay     {"decay",     0.95f, 0.80f, 1.0f};

    // Motion
    vivid::Param<float> width     {"width",     0.008f, 0.001f, 0.05f};
    vivid::Param<float> speed     {"speed",     0.2f,  0.01f, 1.0f};
    vivid::Param<float> curvature {"curvature", 1.0f,  0.0f,  5.0f};

    // Color
    vivid::Param<float> color_r   {"color_r",   0.3f,  0.0f,  1.0f};
    vivid::Param<float> color_g   {"color_g",   0.7f,  0.0f,  1.0f};
    vivid::Param<float> color_b   {"color_b",   1.0f,  0.0f,  1.0f};

    // Glow
    vivid::Param<float> glow      {"glow",      0.0f,  0.0f,  1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::layout_row(count,     2, 0);
        vivid::layout_row(decay,     2, 1);
        // width, speed, curvature: full-width sliders
        vivid::display_hint(color_r, VIVID_DISPLAY_COLOR);
        vivid::display_hint(color_g, VIVID_DISPLAY_COLOR);
        vivid::display_hint(color_b, VIVID_DISPLAY_COLOR);
        // color: compound widget handles COLOR triplet automatically
        // glow: full-width by default
        out.push_back(&count);
        out.push_back(&decay);
        out.push_back(&width);
        out.push_back(&speed);
        out.push_back(&curvature);
        out.push_back(&color_r);
        out.push_back(&color_g);
        out.push_back(&color_b);
        out.push_back(&glow);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_role_bindings(std::vector<VividRoleBindingDescriptor>& out) override {
        {
            VividRoleBindingDescriptor role{};
            role.role_id                            = "width_mod";
            role.label                              = "Width Mod";
            role.accepted_domain                    = VIVID_DOMAIN_CONTROL;
            role.runtime_scope                      = VIVID_ROLE_PER_VOICE;
            role.allowed_operator_types             = nullptr;
            role.allowed_operator_type_count        = 0;
            role.default_operator_type              = "LFO";
            role.preferred_output_name              = "value";
            role.preferred_output_semantic_tags      = nullptr;
            role.preferred_output_semantic_tag_count = 0;
            out.push_back(role);
        }
        {
            VividRoleBindingDescriptor role{};
            role.role_id                            = "opacity_mod";
            role.label                              = "Opacity Mod";
            role.accepted_domain                    = VIVID_DOMAIN_CONTROL;
            role.runtime_scope                      = VIVID_ROLE_PER_VOICE;
            role.allowed_operator_types             = nullptr;
            role.allowed_operator_type_count        = 0;
            role.default_operator_type              = "LFO";
            role.preferred_output_name              = "value";
            role.preferred_output_semantic_tags      = nullptr;
            role.preferred_output_semantic_tag_count = 0;
            out.push_back(role);
        }
        {
            VividRoleBindingDescriptor role{};
            role.role_id                            = "color_shift";
            role.label                              = "Color Shift";
            role.accepted_domain                    = VIVID_DOMAIN_CONTROL;
            role.runtime_scope                      = VIVID_ROLE_PER_VOICE;
            role.allowed_operator_types             = nullptr;
            role.allowed_operator_type_count        = 0;
            role.default_operator_type              = "LFO";
            role.preferred_output_name              = "value";
            role.preferred_output_semantic_tags      = nullptr;
            role.preferred_output_semantic_tag_count = 0;
            out.push_back(role);
        }
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_ && !lazy_init(ctx)) return;

        // ── Get input texture (optional) ─────────────────────────────
        WGPUTextureView input_tex = nullptr;
        if (ctx->input_texture_views && ctx->input_texture_count >= 1)
            input_tex = ctx->input_texture_views[0];

        if (!input_tex && !fallback_view_) create_fallback(ctx);
        if (!input_tex) input_tex = fallback_view_;

        // ── Recreate feedback texture on resolution change ───────────
        if (ctx->output_width != cached_width_ || ctx->output_height != cached_height_) {
            recreate_feedback_texture(ctx);
            cached_width_  = ctx->output_width;
            cached_height_ = ctx->output_height;
        }

        int n = count.int_value();
        if (n < 1) n = 1;
        if (n > kMaxTrails) n = kMaxTrails;

        // ── Initialize role binding pools ────────────────────────────
        maybe_init_pool(width_pool_,       ctx, "width_mod",   n);
        maybe_init_pool(opacity_pool_,     ctx, "opacity_mod", n);
        maybe_init_pool(color_shift_pool_, ctx, "color_shift", n);

        // ── Re-randomize on count change ─────────────────────────────
        if (n != prev_count_) {
            randomize_trails(n);
            prev_count_ = n;
        }

        float dt = static_cast<float>(ctx->delta_time);
        if (dt > 0.05f) dt = 0.05f;

        // ── Process role bindings ────────────────────────────────────
        VividProcessContext ctrl_ctx{};
        ctrl_ctx.time       = ctx->time;
        ctrl_ctx.delta_time = ctx->delta_time;
        ctrl_ctx.frame      = ctx->frame;

        float width_mod_vals[kMaxTrails];
        float opacity_mod_vals[kMaxTrails];
        float color_shift_vals[kMaxTrails];

        for (int i = 0; i < n; ++i) {
            width_mod_vals[i] = 0.0f;
            if (i < static_cast<int>(width_pool_.pool.size()) && width_pool_.pool[i]) {
                width_pool_.pool[i]->process(&ctrl_ctx);
                width_mod_vals[i] = width_pool_.pool[i]->output("value");
            }

            opacity_mod_vals[i] = 0.0f;
            if (i < static_cast<int>(opacity_pool_.pool.size()) && opacity_pool_.pool[i]) {
                opacity_pool_.pool[i]->process(&ctrl_ctx);
                opacity_mod_vals[i] = opacity_pool_.pool[i]->output("value");
            }

            color_shift_vals[i] = 0.0f;
            if (i < static_cast<int>(color_shift_pool_.pool.size()) && color_shift_pool_.pool[i]) {
                color_shift_pool_.pool[i]->process(&ctrl_ctx);
                color_shift_vals[i] = color_shift_pool_.pool[i]->output("value");
            }
        }

        // ── Update trail positions ───────────────────────────────────
        float spd  = speed.value;
        float curv = curvature.value;

        for (int i = 0; i < n; ++i) {
            auto& t = trails_[i];
            t.prev_x = t.x;
            t.prev_y = t.y;

            t.angle += t.curve_offset * curv * spd * dt;
            t.x += std::cos(t.angle) * spd * dt;
            t.y += std::sin(t.angle) * spd * dt;

            // Wrap to [0,1]
            t.x -= std::floor(t.x);
            t.y -= std::floor(t.y);

            // If wrapped across screen, collapse capsule to avoid spanning line
            float dx = t.x - t.prev_x;
            float dy = t.y - t.prev_y;
            if (std::abs(dx) > 0.3f || std::abs(dy) > 0.3f) {
                t.prev_x = t.x;
                t.prev_y = t.y;
            }
        }

        // ── Pack uniforms ────────────────────────────────────────────
        TrailsUniforms u{};
        u.resolution[0] = static_cast<float>(ctx->output_width);
        u.resolution[1] = static_cast<float>(ctx->output_height);
        u.time          = static_cast<float>(ctx->time);
        u.active_count  = static_cast<float>(n);
        u.decay         = decay.value;
        u.glow          = glow.value;
        u.has_input     = (input_tex != fallback_view_) ? 1.0f : 0.0f;
        u.base_color[0] = color_r.value;
        u.base_color[1] = color_g.value;
        u.base_color[2] = color_b.value;
        u.base_color[3] = 1.0f;

        float w = width.value;

        for (int i = 0; i < n; ++i) {
            auto& t = trails_[i];

            // Geometry: xy=current, zw=prev
            u.trails_geo[i * 4 + 0] = t.x;
            u.trails_geo[i * 4 + 1] = t.y;
            u.trails_geo[i * 4 + 2] = t.prev_x;
            u.trails_geo[i * 4 + 3] = t.prev_y;

            // Style: x=width, y=opacity, z=hue_shift
            float mod_w = w * std::max(0.0f, 1.0f + width_mod_vals[i]);
            float mod_o = std::max(0.0f, 1.0f + opacity_mod_vals[i] * 0.5f);
            float hue   = color_shift_vals[i] * 3.14159265f;
            u.trails_style[i * 4 + 0] = mod_w;
            u.trails_style[i * 4 + 1] = mod_o;
            u.trails_style[i * 4 + 2] = hue;
            u.trails_style[i * 4 + 3] = 0.0f;
        }

        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        // ── Rebuild bind groups if needed ────────────────────────────
        if (input_tex != cached_input_tex_ || bind_groups_dirty_) {
            rebuild_bind_groups(ctx, input_tex);
            cached_input_tex_ = input_tex;
            bind_groups_dirty_ = false;
        }

        // ── Render pass ──────────────────────────────────────────────
        static constexpr WGPUColor kClearTransparent{0, 0, 0, 0};
        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "Trails Pass", kClearTransparent);

        // ── Copy output → feedback texture for next frame ────────────
        WGPUTexelCopyTextureInfo src{};
        src.texture = ctx->output_texture;
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = fb_tex_;
        WGPUExtent3D copy_size = { ctx->output_width, ctx->output_height, 1 };
        wgpuCommandEncoderCopyTextureToTexture(ctx->command_encoder, &src, &dst, &copy_size);
    }

    ~Trails() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(fb_tex_);
        vivid::gpu::release(fb_view_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    // Trail state
    std::vector<Trail> trails_;
    int prev_count_ = -1;

    // Role binding pools
    RolePool width_pool_;
    RolePool opacity_pool_;
    RolePool color_shift_pool_;

    // GPU handles — pipeline
    WGPURenderPipeline  pipeline_    = nullptr;
    WGPUBindGroupLayout bind_layout_ = nullptr;
    WGPUPipelineLayout  pipe_layout_ = nullptr;
    WGPUShaderModule    shader_      = nullptr;
    WGPUBuffer          uniform_buf_ = nullptr;

    // GPU handles — ping-pong
    WGPUSampler         sampler_     = nullptr;
    WGPUTexture         fb_tex_      = nullptr;
    WGPUTextureView     fb_view_     = nullptr;
    WGPUBindGroup       bind_group_  = nullptr;

    // GPU handles — fallback
    WGPUTexture         fallback_tex_  = nullptr;
    WGPUTextureView     fallback_view_ = nullptr;

    // Cache tracking
    WGPUTextureView     cached_input_tex_ = nullptr;
    uint32_t            cached_width_  = 0;
    uint32_t            cached_height_ = 0;
    bool                bind_groups_dirty_ = true;

    // ── One-time GPU initialization ──────────────────────────────────

    bool lazy_init(const VividGpuContext* gpu) {
        std::string frag = std::string(vivid::gpu::WGSL_CONSTANTS) + kTrailsFragment;
        shader_ = vivid::gpu::create_shader(gpu->device, frag.c_str(), "Trails Shader");
        if (!shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(
            gpu->device, sizeof(TrailsUniforms), "Trails Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Trails Sampler");

        // Bind layout: uniform(0) + sampler(1) + feedbackTex(2) + inputTex(3)
        bind_layout_ = vivid::gpu::create_standard_bind_layout(
            gpu->device, 2, "Trails BGL", sizeof(TrailsUniforms), WGPUShaderStage_Vertex);

        WGPUPipelineLayoutDescriptor pl_desc{};
        pl_desc.label = vivid_sv("Trails Pipeline Layout");
        pl_desc.bindGroupLayoutCount = 1;
        pl_desc.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);

        pipeline_ = vivid::gpu::create_pipeline(
            gpu->device, shader_, pipe_layout_, gpu->output_format, "Trails Pipeline");
        if (!pipeline_) return false;

        // Create initial feedback texture
        recreate_feedback_texture(gpu);
        cached_width_  = gpu->output_width;
        cached_height_ = gpu->output_height;

        trails_.resize(kMaxTrails);
        return true;
    }

    // ── Feedback texture management ──────────────────────────────────

    void recreate_feedback_texture(const VividGpuContext* gpu) {
        vivid::gpu::release(fb_tex_);
        vivid::gpu::release(fb_view_);

        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("Trails Feedback");
        td.size          = { gpu->output_width, gpu->output_height, 1 };
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.dimension     = WGPUTextureDimension_2D;
        td.format        = gpu->output_format;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

        fb_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format          = gpu->output_format;
        vd.dimension       = WGPUTextureViewDimension_2D;
        vd.mipLevelCount   = 1;
        vd.arrayLayerCount = 1;
        vd.aspect          = WGPUTextureAspect_All;

        fb_view_ = wgpuTextureCreateView(fb_tex_, &vd);
        bind_groups_dirty_ = true;
    }

    // ── Fallback texture (1x1 black) ─────────────────────────────────

    void create_fallback(const VividGpuContext* gpu) {
        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("Trails Fallback");
        td.size          = { 1, 1, 1 };
        td.mipLevelCount = 1;
        td.sampleCount   = 1;
        td.dimension     = WGPUTextureDimension_2D;
        td.format        = gpu->output_format;
        td.usage         = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        fallback_tex_    = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTextureViewDescriptor vd{};
        vd.format          = gpu->output_format;
        vd.dimension       = WGPUTextureViewDimension_2D;
        vd.mipLevelCount   = 1;
        vd.arrayLayerCount = 1;
        vd.aspect          = WGPUTextureAspect_All;
        fallback_view_     = wgpuTextureCreateView(fallback_tex_, &vd);

        const uint8_t zero[8] = {};
        WGPUTexelCopyTextureInfo dest_info{};
        dest_info.texture  = fallback_tex_;
        dest_info.mipLevel = 0;
        dest_info.origin   = {0, 0, 0};
        dest_info.aspect   = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = 8;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &dest_info, zero, sizeof(zero), &layout, &extent);
    }

    // ── Bind group rebuild ───────────────────────────────────────────

    void rebuild_bind_groups(const VividGpuContext* gpu, WGPUTextureView input_tex) {
        vivid::gpu::release(bind_group_);

        WGPUTextureView views[2] = { fb_view_, input_tex };
        bind_group_ = vivid::gpu::create_standard_bind_group(
            gpu->device, bind_layout_, uniform_buf_, sizeof(TrailsUniforms),
            sampler_, views, 2, "Trails Bind Group");
    }

    // ── Trail initialization ─────────────────────────────────────────

    void randomize_trails(int n) {
        constexpr float TAU    = 6.2831853f;
        constexpr float margin = 0.1f;
        uint32_t seed = 42;

        for (int i = 0; i < n; ++i) {
            auto& t = trails_[i];
            t.x      = margin + hash_float(seed) * (1.0f - 2.0f * margin);
            t.y      = margin + hash_float(seed) * (1.0f - 2.0f * margin);
            t.prev_x = t.x;
            t.prev_y = t.y;
            t.angle  = TAU * static_cast<float>(i) / static_cast<float>(n);
            t.curve_offset = (hash_float(seed) - 0.5f) * 2.0f;
        }
    }

    // ── Role binding pool management ────────────────────────────────

    void maybe_init_pool(RolePool& rp, const VividGpuContext* ctx,
                         const char* role_id, int n) {
        if (!ctx->role_binding_configs || ctx->role_binding_count == 0) {
            if (rp.initialized) {
                rp.pool.clear();
                rp.initialized = false;
            }
            return;
        }

        const VividRoleBindingRuntimeConfig* cfg = nullptr;
        for (uint32_t i = 0; i < ctx->role_binding_count; ++i) {
            if (std::strcmp(ctx->role_binding_configs[i].role_id, role_id) == 0) {
                cfg = &ctx->role_binding_configs[i];
                break;
            }
        }

        if (!cfg || !cfg->create_fn) {
            if (rp.initialized) {
                rp.pool.clear();
                rp.initialized = false;
            }
            return;
        }

        bool need_reinit = !rp.initialized
            || rp.cached_create_fn != cfg->create_fn
            || static_cast<int>(rp.pool.size()) != n;

        if (need_reinit) {
            rp.pool.clear();
            rp.cached_create_fn  = cfg->create_fn;
            rp.cached_destroy_fn = cfg->destroy_fn;

            for (int i = 0; i < n; ++i) {
                auto* raw = static_cast<vivid::OperatorBase*>(cfg->create_fn());
                if (!raw) continue;

                auto destroy = [dfn = rp.cached_destroy_fn](vivid::OperatorBase* p) {
                    if (dfn) dfn(p); else delete p;
                };
                auto inst = std::make_unique<vivid::BoundControlInstance>(raw, std::move(destroy));

                for (uint32_t pi = 0; pi < cfg->param_count; ++pi) {
                    if (inst->has_param(cfg->param_names[pi])) {
                        inst->set_param(cfg->param_names[pi], cfg->param_values[pi]);
                    }
                }

                if (inst->has_param("phase_offset")) {
                    inst->set_param("phase_offset",
                        static_cast<float>(i) / static_cast<float>(n));
                }

                rp.pool.push_back(std::move(inst));
            }
            rp.initialized = true;
        } else {
            for (int i = 0; i < static_cast<int>(rp.pool.size()); ++i) {
                auto& inst = rp.pool[i];
                if (!inst) continue;
                for (uint32_t pi = 0; pi < cfg->param_count; ++pi) {
                    if (inst->has_param(cfg->param_names[pi])) {
                        inst->set_param(cfg->param_names[pi], cfg->param_values[pi]);
                    }
                }
                if (inst->has_param("phase_offset")) {
                    inst->set_param("phase_offset",
                        static_cast<float>(i) / static_cast<float>(rp.pool.size()));
                }
            }
        }
    }
};

VIVID_REGISTER(Trails)
