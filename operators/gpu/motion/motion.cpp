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

static const char* kMotionFragment = R"(

struct Uniforms {
    gain: f32,
    threshold: f32,
    decay: f32,
    _pad: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;
@group(0) @binding(3) var prevTex:  texture_2d<f32>;
@group(0) @binding(4) var accumTex: texture_2d<f32>;

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
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let curr = textureSample(inputTex, texSampler, input.uv).rgb;
    let prev = textureSample(prevTex,  texSampler, input.uv).rgb;
    let acc  = textureSample(accumTex, texSampler, input.uv).r;
    let diff = max(abs(luma(curr) - luma(prev)) * uniforms.gain - uniforms.threshold, 0.0);
    let m = clamp(max(diff, acc * uniforms.decay), 0.0, 1.0);
    return vec4f(m, m, m, 1.0);
}
)";

struct MotionUniforms {
    float gain;
    float threshold;
    float decay;
    float _pad;
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

    vivid::Param<float> gain      {"gain",      8.0f,  0.0f, 32.0f};
    vivid::Param<float> threshold {"threshold", 0.04f, 0.0f, 1.0f};
    vivid::Param<float> decay     {"decay",     0.85f, 0.0f, 0.99f};

    Motion() {
        vivid::description(gain, "Motion sensitivity — multiplies the frame-to-frame luminance change");
        vivid::description(threshold, "Cutoff below which motion is treated as zero (kills sensor noise)");
        vivid::description(decay, "How long motion lingers each frame, 0 = pure 1-frame difference");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
        out.push_back(&threshold);
        out.push_back(&decay);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
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
        u.gain      = gain.value;
        u.threshold = threshold.value;
        u.decay     = decay.value;
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        if (input_view != cached_input_tex_ || bind_group_dirty_) {
            rebuild_bind_group(ctx, input_view);
            cached_input_tex_  = input_view;
            bind_group_dirty_  = false;
        }

        static constexpr WGPUColor kClearBlack{0, 0, 0, 1};
        vivid::gpu::run_pass(ctx->command_encoder, pipeline_, bind_group_,
                             ctx->output_texture_view, "Motion Pass", kClearBlack);

        WGPUExtent3D size = { ctx->output_width, ctx->output_height, 1 };

        // output -> accum (next frame's temporal smear). Output has CopySrc.
        {
            WGPUTexelCopyTextureInfo src{}; src.texture = ctx->output_texture;
            WGPUTexelCopyTextureInfo dst{}; dst.texture = accum_tex_;
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
    WGPUTexture     accum_tex_  = nullptr;  // previous output mask (temporal smear)
    WGPUTextureView accum_view_ = nullptr;

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

        td.label = vivid_sv("Motion Accum");
        accum_tex_  = wgpuDeviceCreateTexture(gpu->device, &td);
        accum_view_ = wgpuTextureCreateView(accum_tex_, &vd);

        bind_group_dirty_ = true;
    }

    void rebuild_bind_group(const VividGpuContext* gpu, WGPUTextureView input_view) {
        vivid::gpu::release(bind_group_);
        WGPUBindGroupEntry e[5]{};
        e[0].binding = 0; e[0].buffer = uniform_buf_; e[0].size = sizeof(MotionUniforms);
        e[1].binding = 1; e[1].sampler = sampler_;
        e[2].binding = 2; e[2].textureView = input_view;
        e[3].binding = 3; e[3].textureView = prev_view_;
        e[4].binding = 4; e[4].textureView = accum_view_;
        WGPUBindGroupDescriptor desc{};
        desc.label = vivid_sv("Motion BG");
        desc.layout = bind_layout_;
        desc.entryCount = 5; desc.entries = e;
        bind_group_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    }

    bool lazy_init(const VividGpuContext* gpu) {
        shader_ = vivid::gpu::create_shader(gpu->device, kMotionFragment, "Motion Shader");
        if (!shader_) return false;
        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(MotionUniforms), "Motion Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Motion Sampler");

        WGPUBindGroupLayoutEntry e[5]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform;
        e[0].buffer.minBindingSize = sizeof(MotionUniforms);
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].sampler.type = WGPUSamplerBindingType_Filtering;
        for (int i = 2; i < 5; ++i) {
            e[i].binding = static_cast<uint32_t>(i);
            e[i].visibility = WGPUShaderStage_Fragment;
            e[i].texture.sampleType    = WGPUTextureSampleType_Float;
            e[i].texture.viewDimension = WGPUTextureViewDimension_2D;
            e[i].texture.multisampled  = false;
        }
        WGPUBindGroupLayoutDescriptor bgl{};
        bgl.label = vivid_sv("Motion BGL"); bgl.entryCount = 5; bgl.entries = e;
        bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl);

        WGPUPipelineLayoutDescriptor pl{};
        pl.label = vivid_sv("Motion Pipeline Layout");
        pl.bindGroupLayoutCount = 1; pl.bindGroupLayouts = &bind_layout_;
        pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);

        pipeline_ = vivid::gpu::create_pipeline(gpu->device, shader_, pipe_layout_, gpu->output_format, "Motion Pipeline");
        if (!pipeline_) return false;

        recreate_history(gpu);
        return true;
    }
};

VIVID_DEFINE_OP(Motion) {
}
