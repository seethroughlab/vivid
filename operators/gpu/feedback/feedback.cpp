#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cmath>
#include <cstdio>
#include <string>

// =============================================================================
// Feedback WGSL Shaders
// =============================================================================

static const char* kFeedbackCompositeFragment = R"(

struct Uniforms {
    decay: f32,
    mix_val: f32,
    offset_x: f32,
    offset_y: f32,
    zoom: f32,
    rotate_rad: f32,
    _pad0: f32,
    _pad1: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;
@group(0) @binding(3) var feedbackTex: texture_2d<f32>;

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
    let input_color = textureSample(inputTex, texSampler, input.uv);

    // Transform UV for feedback sampling: center → rotate → scale → offset → uncenter
    var fuv = input.uv - vec2f(0.5, 0.5);

    let c = cos(uniforms.rotate_rad);
    let s = sin(uniforms.rotate_rad);
    fuv = vec2f(fuv.x * c - fuv.y * s, fuv.x * s + fuv.y * c);

    fuv = fuv * (1.0 / uniforms.zoom);

    fuv = fuv + vec2f(uniforms.offset_x, uniforms.offset_y);

    fuv = fuv + vec2f(0.5, 0.5);

    let fb_color = textureSample(feedbackTex, texSampler, fuv) * uniforms.decay;
    return vec4f(input_color.rgb + fb_color.rgb * uniforms.mix_val, max(input_color.a, fb_color.a));
}
)";

// =============================================================================
// Uniform struct matching WGSL (32 bytes)
// =============================================================================

struct FeedbackUniforms {
    float decay;
    float mix_val;
    float offset_x;
    float offset_y;
    float zoom;
    float rotate_rad;
    float _pad0, _pad1;
};

// =============================================================================
// Feedback Operator
// =============================================================================

struct Feedback : vivid::OperatorBase {
    static constexpr const char* kName   = "Feedback";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> decay    {"decay",    0.95f, 0.0f, 1.0f};
    vivid::Param<float> mix      {"mix",      0.5f,  0.0f, 1.0f};
    vivid::Param<float> offset_x {"offset_x", 0.0f, -0.5f, 0.5f};
    vivid::Param<float> offset_y {"offset_y", 0.0f, -0.5f, 0.5f};
    vivid::Param<float> zoom     {"zoom",     1.0f,  0.9f, 1.1f};
    vivid::Param<float> rotate   {"rotate",   0.0f,  0.0f, 360.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        layout_row(offset_x, 2, 0);
        layout_row(offset_y, 2, 1);

        out.push_back(&decay);
        out.push_back(&mix);
        out.push_back(&offset_x);
        out.push_back(&offset_y);
        out.push_back(&zoom);
        out.push_back(&rotate);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!composite_pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[feedback] lazy_init FAILED\n");
                return;
            }
        }

        // Get input texture view
        WGPUTextureView input_tex = nullptr;
        if (gpu->input_texture_views && gpu->input_texture_count >= 1)
            input_tex = gpu->input_texture_views[0];

        if (!input_tex && !fallback_view_) create_fallback(gpu);
        if (!input_tex) input_tex = fallback_view_;

        // Recreate feedback texture if resolution changed
        if (gpu->output_width != cached_width_ || gpu->output_height != cached_height_) {
            recreate_feedback_texture(gpu);
            cached_width_  = gpu->output_width;
            cached_height_ = gpu->output_height;
        }

        // Write uniforms once per frame
        FeedbackUniforms u{};
        u.decay      = decay.value;
        u.mix_val    = mix.value;
        u.offset_x   = offset_x.value;
        u.offset_y   = offset_y.value;
        u.zoom       = zoom.value;
        u.rotate_rad = rotate.value * (3.14159265358979323846f / 180.0f);
        wgpuQueueWriteBuffer(gpu->queue, uniform_buf_, 0, &u, sizeof(u));

        // Rebuild bind group if input texture or feedback texture changed
        if (input_tex != cached_input_tex_ || bind_groups_dirty_) {
            rebuild_bind_groups(gpu, input_tex);
            cached_input_tex_ = input_tex;
            bind_groups_dirty_ = false;
        }

        static constexpr WGPUColor kClearTransparent{0, 0, 0, 0};

        // Pass 1: Composite — input + feedback buffer → output texture
        vivid::gpu::run_pass(gpu->command_encoder, composite_pipeline_, composite_bg_,
                             gpu->output_texture_view, "Feedback Composite", kClearTransparent);

        // Copy output → feedback buffer (for next frame) via direct texture copy
        WGPUTexelCopyTextureInfo src{};
        src.texture = gpu->output_texture;
        WGPUTexelCopyTextureInfo dst{};
        dst.texture = fb_tex_;
        WGPUExtent3D size = { gpu->output_width, gpu->output_height, 1 };
        wgpuCommandEncoderCopyTextureToTexture(gpu->command_encoder, &src, &dst, &size);
    }

    ~Feedback() override {
        vivid::gpu::release(composite_pipeline_);
        vivid::gpu::release(dual_bind_layout_);
        vivid::gpu::release(dual_pipe_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(composite_shader_);
        vivid::gpu::release(composite_bg_);
        vivid::gpu::release(fb_tex_);
        vivid::gpu::release(fb_view_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    // Pipeline
    WGPURenderPipeline composite_pipeline_ = nullptr;

    // Layouts
    WGPUBindGroupLayout dual_bind_layout_   = nullptr; // uniform + sampler + 2 tex
    WGPUPipelineLayout  dual_pipe_layout_   = nullptr;

    // Shader module
    WGPUShaderModule composite_shader_ = nullptr;

    // Shared resources
    WGPUBuffer  uniform_buf_ = nullptr;
    WGPUSampler sampler_     = nullptr;

    // Bind group
    WGPUBindGroup composite_bg_ = nullptr;

    // Persistent feedback texture
    WGPUTexture     fb_tex_  = nullptr;
    WGPUTextureView fb_view_ = nullptr;

    // Fallback 1x1 transparent texture
    WGPUTexture     fallback_tex_  = nullptr;
    WGPUTextureView fallback_view_ = nullptr;

    // Cache tracking
    WGPUTextureView cached_input_tex_ = nullptr;
    uint32_t cached_width_  = 0;
    uint32_t cached_height_ = 0;
    bool bind_groups_dirty_ = true;

    // -------------------------------------------------------------------------
    // Create 1x1 transparent fallback texture
    // -------------------------------------------------------------------------

    void create_fallback(VividGpuState* gpu) {
        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("Feedback Fallback");
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

        const uint8_t zero[4] = {0, 0, 0, 0};
        WGPUTexelCopyTextureInfo dest_info{};
        dest_info.texture  = fallback_tex_;
        dest_info.mipLevel = 0;
        dest_info.origin   = {0, 0, 0};
        dest_info.aspect   = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow  = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = {1, 1, 1};
        wgpuQueueWriteTexture(gpu->queue, &dest_info, zero, sizeof(zero), &layout, &extent);
    }

    // -------------------------------------------------------------------------
    // Create/recreate persistent feedback texture
    // -------------------------------------------------------------------------

    void recreate_feedback_texture(VividGpuState* gpu) {
        vivid::gpu::release(fb_tex_);
        vivid::gpu::release(fb_view_);

        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("Feedback Buffer");
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

    // -------------------------------------------------------------------------
    // Rebuild bind group
    // -------------------------------------------------------------------------

    void rebuild_bind_groups(VividGpuState* gpu, WGPUTextureView input_tex) {
        vivid::gpu::release(composite_bg_);

        // Composite: uniform + sampler + inputTex + feedbackTex
        WGPUBindGroupEntry entries[4]{};
        entries[0].binding = 0;
        entries[0].buffer  = uniform_buf_;
        entries[0].size    = sizeof(FeedbackUniforms);
        entries[1].binding = 1;
        entries[1].sampler = sampler_;
        entries[2].binding = 2;
        entries[2].textureView = input_tex;
        entries[3].binding = 3;
        entries[3].textureView = fb_view_;

        WGPUBindGroupDescriptor desc{};
        desc.label      = vivid_sv("Feedback Composite BG");
        desc.layout     = dual_bind_layout_;
        desc.entryCount = 4;
        desc.entries    = entries;
        composite_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
    }

    // -------------------------------------------------------------------------
    // One-time GPU resource initialization
    // -------------------------------------------------------------------------

    bool lazy_init(VividGpuState* gpu) {
        // Shader module
        composite_shader_ = vivid::gpu::create_shader(gpu->device, kFeedbackCompositeFragment, "Feedback Composite Shader");
        if (!composite_shader_)
            return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(FeedbackUniforms), "Feedback Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Feedback Sampler");

        // Dual-texture bind group layout: uniform(0) + sampler(1) + texA(2) + texB(3)
        {
            WGPUBindGroupLayoutEntry entries[4]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(FeedbackUniforms);

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[2].binding    = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2].texture.multisampled  = false;

            entries[3].binding    = 3;
            entries[3].visibility = WGPUShaderStage_Fragment;
            entries[3].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[3].texture.multisampled  = false;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label      = vivid_sv("Feedback Dual BGL");
            bgl_desc.entryCount = 4;
            bgl_desc.entries    = entries;
            dual_bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // Pipeline layout
        {
            WGPUPipelineLayoutDescriptor pl_desc{};
            pl_desc.label               = vivid_sv("Feedback Pipeline Layout");
            pl_desc.bindGroupLayoutCount = 1;
            pl_desc.bindGroupLayouts     = &dual_bind_layout_;
            dual_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);
        }

        // Render pipeline
        composite_pipeline_ = vivid::gpu::create_pipeline(gpu->device, composite_shader_, dual_pipe_layout_, gpu->output_format, "Feedback Composite Pipeline");

        if (!composite_pipeline_)
            return false;

        // Create initial feedback texture
        recreate_feedback_texture(gpu);

        return true;
    }
};

VIVID_REGISTER(Feedback)
