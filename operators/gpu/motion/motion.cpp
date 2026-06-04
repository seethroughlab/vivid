#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <algorithm>
#include <cstdio>

// =============================================================================
// Motion — per-pixel frame-difference / motion magnitude
//
// Keeps the previous input frame and outputs a grayscale mask of where the
// image is *changing*: m = |luma(current) - luma(previous)|. A temporal `decay`
// lets motion linger so the mask reads as continuous flow rather than 1-frame
// flicker. Designed to drive Particles2D's `emit_mask` (emit where it moves).
// =============================================================================

// Two render targets (MRT):
//   @location(0) magnitude  — grayscale motion mask        (primary output, RGBA16F)
//   @location(1) flow       — signed optical-flow vector    (aux output,     RG16F)
// History is split to match: accumMagTex carries the previous magnitude,
// accumFlowTex the previous *raw signed* flow (no encode/decode round-trip).
static const char* kMotionFragment = R"(

struct Uniforms {
    gain: f32,
    threshold: f32,
    decay: f32,
    flow_scale: f32,
    mode: u32,        // 0 = Magnitude, 1 = Flow
    _pad0: u32,
    _pad1: u32,
    _pad2: u32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

struct FragOut {
    @location(0) magnitude: vec4f,   // motion magnitude (grayscale)
    @location(1) flow:      vec2f,   // signed motion vector
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex:     texture_2d<f32>;
@group(0) @binding(3) var prevTex:      texture_2d<f32>;
@group(0) @binding(4) var accumMagTex:  texture_2d<f32>;   // previous magnitude
@group(0) @binding(5) var accumFlowTex: texture_2d<f32>;   // previous raw signed flow (RG)

fn luma(c: vec3f) -> f32 { return dot(c, vec3f(0.2126, 0.7152, 0.0722)); }

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

@fragment
fn fs_main(input: VertexOutput) -> FragOut {
    let curr   = textureSample(inputTex, texSampler, input.uv).rgb;
    let prev   = textureSample(prevTex,  texSampler, input.uv).rgb;
    let prev_m = textureSample(accumMagTex, texSampler, input.uv).r; // previous magnitude
    let It     = luma(curr) - luma(prev);                            // temporal change
    let diff   = max(abs(It) * uniforms.gain - uniforms.threshold, 0.0);
    let m      = clamp(max(diff, prev_m * uniforms.decay), 0.0, 1.0);

    var out: FragOut;
    out.magnitude = vec4f(m, m, m, 1.0);
    out.flow = vec2f(0.0, 0.0);

    if (uniforms.mode == 1u) {
        // Gradient / brightness-constancy "normal" flow: flow = -It * grad / |grad|^2.
        let texel = 1.0 / vec2f(textureDimensions(inputTex));
        let gx = luma(textureSample(inputTex, texSampler, input.uv + vec2f(texel.x, 0.0)).rgb)
               - luma(textureSample(inputTex, texSampler, input.uv - vec2f(texel.x, 0.0)).rgb);
        let gy = luma(textureSample(inputTex, texSampler, input.uv + vec2f(0.0, texel.y)).rgb)
               - luma(textureSample(inputTex, texSampler, input.uv - vec2f(0.0, texel.y)).rgb);
        let grad   = vec2f(gx, gy);
        let f_inst = clamp(-It * grad / (dot(grad, grad) + 1e-4) * uniforms.flow_scale, vec2f(-1.0), vec2f(1.0));
        // Smear the flow vector through the decaying magnitude band so particles
        // spawning anywhere in the band inherit a coherent direction (not just the 1px edge).
        let a_flow = textureSample(accumFlowTex, texSampler, input.uv).rg; // raw signed, no decode
        var flow = f_inst;
        if (length(a_flow) * uniforms.decay > length(f_inst)) { flow = a_flow * uniforms.decay; }
        out.flow = select(vec2f(0.0, 0.0), flow, m > 0.02);  // zero flow where there's no motion
    }
    return out;
}
)";

struct MotionUniforms {
    float    gain;
    float    threshold;
    float    decay;
    float    flow_scale;
    uint32_t mode;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};

/**
 * @brief Per-pixel frame-difference (motion magnitude) of a video texture.
 *
 * Outputs a grayscale mask that lights up where the input is changing between
 * frames — moving edges glow, still regions stay black. A temporal decay makes
 * motion linger so the mask is continuous, not flickery. Wire it into
 * Particles2D's `emit_mask` to spawn particles off the moving parts of a video.
 *
 * @tip Raise `gain` until motion is visible, then `threshold` to kill sensor noise.
 * @see Particles2D, Feedback, Trails
 */
struct Motion : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName   = "Motion";
    static constexpr bool kTimeDependent = true;
    static constexpr const char* kSummary =
        "Frame-difference motion mask: bright where the video moves, for driving emit_mask";
    static constexpr std::array<const char*, 5> kKeywords =
        {"motion", "difference", "optical flow", "frame diff", "temporal"};

    vivid::Param<int>   mode      {"mode",      0, {"Magnitude", "Flow"}};
    vivid::Param<float> gain      {"gain",      8.0f,  0.0f, 32.0f};
    vivid::Param<float> threshold {"threshold", 0.04f, 0.0f, 1.0f};
    vivid::Param<float> decay     {"decay",     0.85f, 0.0f, 0.99f};
    vivid::Param<float> flow_scale {"flow_scale", 20.0f, 1.0f, 100.0f};

    Motion() {
        vivid::description(mode, "Magnitude = grayscale motion mask only; Flow = also emit a signed motion-vector field on the flow_vector output (for Particles2D emit_flow)");
        vivid::description(gain, "Motion sensitivity — multiplies the frame-to-frame luminance change");
        vivid::description(threshold, "Cutoff below which motion is treated as zero (kills sensor noise)");
        vivid::description(decay, "How long motion lingers each frame, 0 = pure 1-frame difference");
        vivid::description(flow_scale, "Flow mode: how strongly raw flow maps into the encoded vector range");
        vivid::visible_when_eq(flow_scale, mode, {1});
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);
        out.push_back(&gain);
        out.push_back(&threshold);
        out.push_back(&decay);
        out.push_back(&flow_scale);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});  // primary: magnitude
        // Secondary output: signed optical-flow vector field at float precision.
        out.push_back({.name = "flow_vector", .type = VIVID_PORT_TEXTURE,
                       .direction = VIVID_PORT_OUTPUT,
                       .description = "Signed per-pixel motion vector (Flow mode); wire into Particles2D flow_vector",
                       .gpu_texture_format = VIVID_TEXFMT_RG16F});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!pipeline_) {
            if (!lazy_init(ctx)) { std::fprintf(stderr, "[motion] lazy_init FAILED\n"); return; }
        }

        WGPUTextureView input_view = nullptr;
        WGPUTexture     input_tex  = nullptr;
        if (ctx->input_texture_views && ctx->input_texture_count >= 1)
            input_view = ctx->input_texture_views[0];
        if (ctx->input_textures && ctx->input_texture_count >= 1)
            input_tex = ctx->input_textures[0];

        if (!input_view && !fallback_view_) create_fallback(ctx);
        if (!input_view) input_view = fallback_view_;

        if (ctx->output_width != cached_width_ || ctx->output_height != cached_height_) {
            recreate_history(ctx);
            cached_width_  = ctx->output_width;
            cached_height_ = ctx->output_height;
        }

        MotionUniforms u{};
        u.gain       = gain.value;
        u.threshold  = threshold.value;
        u.decay      = decay.value;
        u.flow_scale = flow_scale.value;
        u.mode       = static_cast<uint32_t>(mode.int_value());
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        if (input_view != cached_input_tex_ || bind_group_dirty_) {
            rebuild_bind_group(ctx, input_view);
            cached_input_tex_  = input_view;
            bind_group_dirty_  = false;
        }

        static constexpr WGPUColor kClearBlack{0, 0, 0, 1};
        WGPUTextureView flow_view = (ctx->aux_output_texture_count >= 1)
                                        ? ctx->aux_output_texture_views[0] : nullptr;
        if (flow_view) {
            WGPUTextureView targets[2] = { ctx->output_texture_view, flow_view };
            WGPUColor       clears[2]  = { kClearBlack, kClearBlack };
            vivid::gpu::run_pass_mrt(ctx->command_encoder, pipeline_, bind_group_,
                                     targets, 2, clears, "Motion Pass");
        } else {
            // Defensive: no aux texture allocated. Render magnitude only.
            static bool warned = false;
            if (!warned) { std::fprintf(stderr, "[motion] no aux flow texture; magnitude only\n"); warned = true; }
            vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                                 ctx->output_texture_view, "Motion Pass", kClearBlack);
        }

        WGPUExtent3D size = { ctx->output_width, ctx->output_height, 1 };

        // primary magnitude -> accum_mag (next frame's temporal decay). Has CopySrc.
        {
            WGPUTexelCopyTextureInfo src{}; src.texture = ctx->output_texture;
            WGPUTexelCopyTextureInfo dst{}; dst.texture = accum_tex_;
            wgpuCommandEncoderCopyTextureToTexture(ctx->command_encoder, &src, &dst, &size);
        }
        // aux flow -> accum_flow (next frame's flow smear). Both RG16F, runtime-allocated with CopySrc.
        if (flow_view && ctx->aux_output_textures && ctx->aux_output_textures[0] && accum_flow_tex_) {
            WGPUTexelCopyTextureInfo src{}; src.texture = ctx->aux_output_textures[0];
            WGPUTexelCopyTextureInfo dst{}; dst.texture = accum_flow_tex_;
            wgpuCommandEncoderCopyTextureToTexture(ctx->command_encoder, &src, &dst, &size);
        }
        // current input -> prev (next frame's "previous"). Input operator outputs
        // share output_format and carry CopySrc; if a future source lacks it this
        // copy would error — fall back to a passthrough blit pass into prev_tex_.
        if (input_tex) {
            WGPUTexelCopyTextureInfo src{}; src.texture = input_tex;
            WGPUTexelCopyTextureInfo dst{}; dst.texture = prev_tex_;
            wgpuCommandEncoderCopyTextureToTexture(ctx->command_encoder, &src, &dst, &size);
        }
    }

    ~Motion() override {
        vivid::gpu::release(pipeline_);
        vivid::gpu::release(bind_layout_);
        vivid::gpu::release(pipe_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(shader_);
        vivid::gpu::release(bind_group_);
        vivid::gpu::release(prev_tex_);
        vivid::gpu::release(prev_view_);
        vivid::gpu::release(accum_tex_);
        vivid::gpu::release(accum_view_);
        vivid::gpu::release(accum_flow_tex_);
        vivid::gpu::release(accum_flow_view_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    WGPURenderPipeline  pipeline_     = nullptr;
    WGPUBindGroupLayout bind_layout_  = nullptr;
    WGPUPipelineLayout  pipe_layout_  = nullptr;
    WGPUShaderModule    shader_       = nullptr;
    WGPUBuffer          uniform_buf_  = nullptr;
    WGPUSampler         sampler_      = nullptr;
    WGPUBindGroup       bind_group_   = nullptr;

    WGPUTexture     prev_tex_   = nullptr;  // previous input frame
    WGPUTextureView prev_view_  = nullptr;
    WGPUTexture     accum_tex_  = nullptr;  // previous magnitude (temporal decay), RGBA16F
    WGPUTextureView accum_view_ = nullptr;
    WGPUTexture     accum_flow_tex_  = nullptr;  // previous signed flow (temporal smear), RG16F
    WGPUTextureView accum_flow_view_ = nullptr;

    WGPUTexture     fallback_tex_  = nullptr;
    WGPUTextureView fallback_view_ = nullptr;

    WGPUTextureView cached_input_tex_ = nullptr;
    uint32_t cached_width_  = 0;
    uint32_t cached_height_ = 0;
    bool bind_group_dirty_  = true;

    void create_fallback(const VividGpuContext* gpu) {
        WGPUTextureDescriptor td{};
        td.label = vivid_sv("Motion Fallback");
        td.size = {1, 1, 1};
        td.mipLevelCount = 1; td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = gpu->output_format;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        fallback_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);
        WGPUTextureViewDescriptor vd{};
        vd.format = gpu->output_format; vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
        fallback_view_ = wgpuTextureCreateView(fallback_tex_, &vd);
        const uint8_t zero[8] = {};
        WGPUTexelCopyTextureInfo di{}; di.texture = fallback_tex_; di.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout ly{}; ly.bytesPerRow = 8; ly.rowsPerImage = 1;
        WGPUExtent3D ext = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &di, zero, sizeof(zero), &ly, &ext);
    }

    void recreate_history(const VividGpuContext* gpu) {
        vivid::gpu::release(prev_tex_);  vivid::gpu::release(prev_view_);
        vivid::gpu::release(accum_tex_); vivid::gpu::release(accum_view_);
        vivid::gpu::release(accum_flow_tex_); vivid::gpu::release(accum_flow_view_);

        WGPUTextureDescriptor td{};
        td.size = { gpu->output_width, gpu->output_height, 1 };
        td.mipLevelCount = 1; td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = gpu->output_format;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

        WGPUTextureViewDescriptor vd{};
        vd.format = gpu->output_format; vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;

        td.label = vivid_sv("Motion Prev");
        prev_tex_  = wgpuDeviceCreateTexture(gpu->device, &td);
        prev_view_ = wgpuTextureCreateView(prev_tex_, &vd);

        td.label = vivid_sv("Motion Accum Mag");
        accum_tex_  = wgpuDeviceCreateTexture(gpu->device, &td);
        accum_view_ = wgpuTextureCreateView(accum_tex_, &vd);

        // Flow history matches the flow_vector aux output format (RG16F).
        WGPUTextureDescriptor ftd = td;
        ftd.format = WGPUTextureFormat_RG16Float;
        ftd.label  = vivid_sv("Motion Accum Flow");
        WGPUTextureViewDescriptor fvd = vd;
        fvd.format = WGPUTextureFormat_RG16Float;
        accum_flow_tex_  = wgpuDeviceCreateTexture(gpu->device, &ftd);
        accum_flow_view_ = wgpuTextureCreateView(accum_flow_tex_, &fvd);

        bind_group_dirty_ = true;
    }

    void rebuild_bind_group(const VividGpuContext* gpu, WGPUTextureView input_view) {
        vivid::gpu::release(bind_group_);
        WGPUBindGroupEntry e[6]{};
        e[0].binding = 0; e[0].buffer = uniform_buf_; e[0].size = sizeof(MotionUniforms);
        e[1].binding = 1; e[1].sampler = sampler_;
        e[2].binding = 2; e[2].textureView = input_view;
        e[3].binding = 3; e[3].textureView = prev_view_;
        e[4].binding = 4; e[4].textureView = accum_view_;
        e[5].binding = 5; e[5].textureView = accum_flow_view_;
        WGPUBindGroupDescriptor desc{};
        desc.label = vivid_sv("Motion BG");
        desc.layout = bind_layout_;
        desc.entryCount = 6; desc.entries = e;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    }

    bool lazy_init(const VividGpuContext* gpu) {
        shader_ = vivid::gpu::create_shader(gpu->device, kMotionFragment, "Motion Shader");
        if (!shader_) return false;
        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(MotionUniforms), "Motion Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Motion Sampler");

        WGPUBindGroupLayoutEntry e[6]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform;
        e[0].buffer.minBindingSize = sizeof(MotionUniforms);
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].sampler.type = WGPUSamplerBindingType_Filtering;
        for (int i = 2; i < 6; ++i) {
            e[i].binding = static_cast<uint32_t>(i);
            e[i].visibility = WGPUShaderStage_Fragment;
            e[i].texture.sampleType    = WGPUTextureSampleType_Float;
            e[i].texture.viewDimension = WGPUTextureViewDimension_2D;
            e[i].texture.multisampled  = false;
        }
        WGPUBindGroupLayoutDescriptor bgl{};
        bgl.label = vivid_sv("Motion BGL"); bgl.entryCount = 6; bgl.entries = e;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl);

        WGPUPipelineLayoutDescriptor pl{};
        pl.label = vivid_sv("Motion Pipeline Layout");
        pl.bindGroupLayoutCount = 1; pl.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);

        // MRT: target 0 = magnitude (offscreen format), target 1 = flow vector (RG16F).
        WGPUTextureFormat fmts[2] = { gpu->output_format, WGPUTextureFormat_RG16Float };
        pipeline_ = vivid::gpu::create_pipeline_mrt(gpu->device, shader_, pipe_layout_,
                                                    fmts, 2, "Motion Pipeline");
        if (!pipeline_) return false;

        recreate_history(gpu);
        return true;
    }
};

VIVID_DEFINE_OP(Motion) {
}
