#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <string>

// =============================================================================
// Bloom WGSL Shaders
// =============================================================================

static const char* kThresholdFragment = R"(

struct Uniforms {
    threshold: f32,
    intensity: f32,
    radius: f32,
    texel_w: f32,
    texel_h: f32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;

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
    let color = textureSample(inputTex, texSampler, input.uv);
    let luminance = dot(color.rgb, vec3f(0.2126, 0.7152, 0.0722));
    let t = uniforms.threshold;
    let contribution = max(luminance - t, 0.0) / max(1.0 - t, 0.001);
    return vec4f(color.rgb * contribution, color.a);
}
)";

static const char* kBlurHFragment = R"(

struct Uniforms {
    threshold: f32,
    intensity: f32,
    radius: f32,
    texel_w: f32,
    texel_h: f32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;

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
    let dir = vec2f(uniforms.texel_w, 0.0);
    let scale = uniforms.radius;

    var result = textureSample(inputTex, texSampler, input.uv) * 0.2270270270;
    result += textureSample(inputTex, texSampler, input.uv + dir * 1.3846153846 * scale) * 0.3162162162;
    result += textureSample(inputTex, texSampler, input.uv - dir * 1.3846153846 * scale) * 0.3162162162;
    result += textureSample(inputTex, texSampler, input.uv + dir * 3.2307692308 * scale) * 0.0702702703;
    result += textureSample(inputTex, texSampler, input.uv - dir * 3.2307692308 * scale) * 0.0702702703;

    return result;
}
)";

static const char* kBlurVFragment = R"(

struct Uniforms {
    threshold: f32,
    intensity: f32,
    radius: f32,
    texel_w: f32,
    texel_h: f32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;

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
    let dir = vec2f(0.0, uniforms.texel_h);
    let scale = uniforms.radius;

    var result = textureSample(inputTex, texSampler, input.uv) * 0.2270270270;
    result += textureSample(inputTex, texSampler, input.uv + dir * 1.3846153846 * scale) * 0.3162162162;
    result += textureSample(inputTex, texSampler, input.uv - dir * 1.3846153846 * scale) * 0.3162162162;
    result += textureSample(inputTex, texSampler, input.uv + dir * 3.2307692308 * scale) * 0.0702702703;
    result += textureSample(inputTex, texSampler, input.uv - dir * 3.2307692308 * scale) * 0.0702702703;

    return result;
}
)";

static const char* kBloomCompositeFragment = R"(

struct Uniforms {
    threshold: f32,
    intensity: f32,
    radius: f32,
    texel_w: f32,
    texel_h: f32,
    _pad0: f32,
    _pad1: f32,
    _pad2: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var texOriginal: texture_2d<f32>;
@group(0) @binding(3) var texBloom: texture_2d<f32>;

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
    let original = textureSample(texOriginal, texSampler, input.uv);
    let bloom = textureSample(texBloom, texSampler, input.uv);
    return vec4f(original.rgb + bloom.rgb * uniforms.intensity, original.a);
}
)";

// =============================================================================
// Uniform struct matching WGSL (32 bytes)
// =============================================================================

struct BloomUniforms {
    float threshold;
    float intensity;
    float radius;
    float texel_w;
    float texel_h;
    float _pad0, _pad1, _pad2;
};

// =============================================================================
// Bloom Operator
// =============================================================================

struct Bloom : vivid::GpuOperatorBase {
    static constexpr const char* kName   = "Bloom";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> threshold {"threshold", 0.8f, 0.0f, 1.0f};
    vivid::Param<float> intensity {"intensity", 1.0f, 0.0f, 2.0f};
    vivid::Param<float> radius    {"radius",    1.0f, 0.5f, 3.0f};
    vivid::Param<int>   passes    {"passes",    3,    1,    8};

    Bloom() {
        vivid::semantic_tag(threshold, "probability_01");
        vivid::semantic_shape(threshold, "scalar");

        vivid::semantic_tag(intensity, "amplitude_linear");
        vivid::semantic_shape(intensity, "scalar");

        vivid::semantic_tag(radius, "scale_xy");
        vivid::semantic_shape(radius, "scalar");

        vivid::semantic_tag(passes, "count");
        vivid::semantic_shape(passes, "int");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&threshold);
        out.push_back(&intensity);
        out.push_back(&radius);
        out.push_back(&passes);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!threshold_pipeline_) {
            if (!lazy_init(ctx)) {
                std::fprintf(stderr, "[bloom] lazy_init FAILED\n");
                return;
            }
        }

        // Get input texture view
        WGPUTextureView input_tex = nullptr;
        if (ctx->input_texture_views && ctx->input_texture_count >= 1)
            input_tex = ctx->input_texture_views[0];

        if (!input_tex && !fallback_view_) create_fallback(ctx);
        if (!input_tex) input_tex = fallback_view_;

        // Recreate intermediates if resolution changed
        if (ctx->output_width != cached_width_ || ctx->output_height != cached_height_) {
            recreate_intermediates(ctx);
            cached_width_  = ctx->output_width;
            cached_height_ = ctx->output_height;
        }

        // Write uniforms once per frame
        BloomUniforms u{};
        u.threshold = threshold.value;
        u.intensity = intensity.value;
        u.radius    = radius.value;
        u.texel_w   = 1.0f / static_cast<float>(ctx->output_width);
        u.texel_h   = 1.0f / static_cast<float>(ctx->output_height);
        wgpuQueueWriteBuffer(ctx->queue, uniform_buf_, 0, &u, sizeof(u));

        // Rebuild bind groups if input texture changed or intermediates recreated
        if (input_tex != cached_input_tex_ || bind_groups_dirty_) {
            rebuild_bind_groups(ctx, input_tex);
            cached_input_tex_ = input_tex;
            bind_groups_dirty_ = false;
        }

        static constexpr WGPUColor kClearTransparent{0, 0, 0, 0};

        // Pass 1: Threshold — input → inter_a
        vivid::gpu::run_pass(ctx->command_encoder, threshold_pipeline_, threshold_bg_, inter_view_a_, "Bloom Threshold", kClearTransparent);

        // Pass 2: Blur passes — ping-pong between inter_a and inter_b
        int n = passes.int_value();
        for (int i = 0; i < n; ++i) {
            vivid::gpu::run_pass(ctx->command_encoder, blur_h_pipeline_, blur_h_bg_, inter_view_b_, "Bloom Blur H", kClearTransparent);
            vivid::gpu::run_pass(ctx->command_encoder, blur_v_pipeline_, blur_v_bg_, inter_view_a_, "Bloom Blur V", kClearTransparent);
        }

        // Pass 3: Composite — original + blurred → output
        vivid::gpu::run_pass(ctx->command_encoder, composite_pipeline_, composite_bg_, ctx->output_texture_view, "Bloom Composite", kClearTransparent);
    }

    ~Bloom() override {
        vivid::gpu::release(threshold_pipeline_);
        vivid::gpu::release(blur_h_pipeline_);
        vivid::gpu::release(blur_v_pipeline_);
        vivid::gpu::release(composite_pipeline_);
        vivid::gpu::release(single_bind_layout_);
        vivid::gpu::release(dual_bind_layout_);
        vivid::gpu::release(single_pipe_layout_);
        vivid::gpu::release(dual_pipe_layout_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(threshold_shader_);
        vivid::gpu::release(blur_h_shader_);
        vivid::gpu::release(blur_v_shader_);
        vivid::gpu::release(composite_shader_);
        vivid::gpu::release(threshold_bg_);
        vivid::gpu::release(blur_h_bg_);
        vivid::gpu::release(blur_v_bg_);
        vivid::gpu::release(composite_bg_);
        vivid::gpu::release(inter_tex_a_);
        vivid::gpu::release(inter_view_a_);
        vivid::gpu::release(inter_tex_b_);
        vivid::gpu::release(inter_view_b_);
        vivid::gpu::release(fallback_tex_);
        vivid::gpu::release(fallback_view_);
    }

private:
    // Pipelines
    WGPURenderPipeline threshold_pipeline_ = nullptr;
    WGPURenderPipeline blur_h_pipeline_    = nullptr;
    WGPURenderPipeline blur_v_pipeline_    = nullptr;
    WGPURenderPipeline composite_pipeline_ = nullptr;

    // Layouts
    WGPUBindGroupLayout single_bind_layout_ = nullptr; // uniform + sampler + 1 tex
    WGPUBindGroupLayout dual_bind_layout_   = nullptr; // uniform + sampler + 2 tex
    WGPUPipelineLayout  single_pipe_layout_ = nullptr;
    WGPUPipelineLayout  dual_pipe_layout_   = nullptr;

    // Shader modules
    WGPUShaderModule threshold_shader_  = nullptr;
    WGPUShaderModule blur_h_shader_     = nullptr;
    WGPUShaderModule blur_v_shader_     = nullptr;
    WGPUShaderModule composite_shader_  = nullptr;

    // Shared resources
    WGPUBuffer  uniform_buf_ = nullptr;
    WGPUSampler sampler_     = nullptr;

    // Bind groups
    WGPUBindGroup threshold_bg_ = nullptr;
    WGPUBindGroup blur_h_bg_    = nullptr;
    WGPUBindGroup blur_v_bg_    = nullptr;
    WGPUBindGroup composite_bg_ = nullptr;

    // Intermediate textures (ping-pong)
    WGPUTexture     inter_tex_a_  = nullptr;
    WGPUTextureView inter_view_a_ = nullptr;
    WGPUTexture     inter_tex_b_  = nullptr;
    WGPUTextureView inter_view_b_ = nullptr;

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

    void create_fallback(const VividGpuContext* gpu) {
        WGPUTextureDescriptor td{};
        td.label         = vivid_sv("Bloom Fallback");
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

        const uint8_t zero[8] = {};  // 8 bytes for RGBA16Float
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

    // -------------------------------------------------------------------------
    // Create/recreate intermediate ping-pong textures
    // -------------------------------------------------------------------------

    void recreate_intermediates(const VividGpuContext* gpu) {
        vivid::gpu::release(inter_tex_a_);
        vivid::gpu::release(inter_view_a_);
        vivid::gpu::release(inter_tex_b_);
        vivid::gpu::release(inter_view_b_);

        for (int i = 0; i < 2; ++i) {
            WGPUTextureDescriptor td{};
            td.label         = vivid_sv(i == 0 ? "Bloom Inter A" : "Bloom Inter B");
            td.size          = { gpu->output_width, gpu->output_height, 1 };
            td.mipLevelCount = 1;
            td.sampleCount   = 1;
            td.dimension     = WGPUTextureDimension_2D;
            td.format        = gpu->output_format;
            td.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;

            WGPUTexture tex = wgpuDeviceCreateTexture(gpu->device, &td);

            WGPUTextureViewDescriptor vd{};
            vd.format          = gpu->output_format;
            vd.dimension       = WGPUTextureViewDimension_2D;
            vd.mipLevelCount   = 1;
            vd.arrayLayerCount = 1;
            vd.aspect          = WGPUTextureAspect_All;

            WGPUTextureView view = wgpuTextureCreateView(tex, &vd);

            if (i == 0) { inter_tex_a_ = tex; inter_view_a_ = view; }
            else        { inter_tex_b_ = tex; inter_view_b_ = view; }
        }

        bind_groups_dirty_ = true;
    }

    // -------------------------------------------------------------------------
    // Rebuild all four bind groups
    // -------------------------------------------------------------------------

    void rebuild_bind_groups(const VividGpuContext* gpu, WGPUTextureView input_tex) {
        vivid::gpu::release(threshold_bg_);
        vivid::gpu::release(blur_h_bg_);
        vivid::gpu::release(blur_v_bg_);
        vivid::gpu::release(composite_bg_);

        // Threshold: uniform + sampler + input
        {
            WGPUBindGroupEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(BloomUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = input_tex;

            WGPUBindGroupDescriptor desc{};
            desc.label      = vivid_sv("Bloom Threshold BG");
            desc.layout     = single_bind_layout_;
            desc.entryCount = 3;
            desc.entries    = entries;
            threshold_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Blur H: uniform + sampler + inter_a
        {
            WGPUBindGroupEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(BloomUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = inter_view_a_;

            WGPUBindGroupDescriptor desc{};
            desc.label      = vivid_sv("Bloom Blur H BG");
            desc.layout     = single_bind_layout_;
            desc.entryCount = 3;
            desc.entries    = entries;
            blur_h_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Blur V: uniform + sampler + inter_b
        {
            WGPUBindGroupEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(BloomUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = inter_view_b_;

            WGPUBindGroupDescriptor desc{};
            desc.label      = vivid_sv("Bloom Blur V BG");
            desc.layout     = single_bind_layout_;
            desc.entryCount = 3;
            desc.entries    = entries;
            blur_v_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Composite: uniform + sampler + original + bloom
        {
            WGPUBindGroupEntry entries[4]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(BloomUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = input_tex;
            entries[3].binding = 3;
            entries[3].textureView = inter_view_a_;

            WGPUBindGroupDescriptor desc{};
            desc.label      = vivid_sv("Bloom Composite BG");
            desc.layout     = dual_bind_layout_;
            desc.entryCount = 4;
            desc.entries    = entries;
            composite_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }
    }

    // -------------------------------------------------------------------------
    // One-time GPU resource initialization
    // -------------------------------------------------------------------------

    bool lazy_init(const VividGpuContext* gpu) {
        // Shader modules
        threshold_shader_ = vivid::gpu::create_shader(gpu->device, kThresholdFragment, "Bloom Threshold Shader");
        blur_h_shader_    = vivid::gpu::create_shader(gpu->device, kBlurHFragment,     "Bloom Blur H Shader");
        blur_v_shader_    = vivid::gpu::create_shader(gpu->device, kBlurVFragment,     "Bloom Blur V Shader");
        composite_shader_ = vivid::gpu::create_shader(gpu->device, kBloomCompositeFragment, "Bloom Composite Shader");
        if (!threshold_shader_ || !blur_h_shader_ || !blur_v_shader_ || !composite_shader_)
            return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(BloomUniforms), "Bloom Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "Bloom Sampler");

        // Single-texture bind group layout: uniform(0) + sampler(1) + tex(2)
        {
            WGPUBindGroupLayoutEntry entries[3]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(BloomUniforms);

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[2].binding    = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
            entries[2].texture.multisampled  = false;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label      = vivid_sv("Bloom Single BGL");
            bgl_desc.entryCount = 3;
            bgl_desc.entries    = entries;
            single_bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // Dual-texture bind group layout: uniform(0) + sampler(1) + texA(2) + texB(3)
        {
            WGPUBindGroupLayoutEntry entries[4]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(BloomUniforms);

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
            bgl_desc.label      = vivid_sv("Bloom Dual BGL");
            bgl_desc.entryCount = 4;
            bgl_desc.entries    = entries;
            dual_bind_layout_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // Pipeline layouts
        {
            WGPUPipelineLayoutDescriptor pl_desc{};
            pl_desc.label               = vivid_sv("Bloom Single Pipeline Layout");
            pl_desc.bindGroupLayoutCount = 1;
            pl_desc.bindGroupLayouts     = &single_bind_layout_;
            single_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);
        }
        {
            WGPUPipelineLayoutDescriptor pl_desc{};
            pl_desc.label               = vivid_sv("Bloom Dual Pipeline Layout");
            pl_desc.bindGroupLayoutCount = 1;
            pl_desc.bindGroupLayouts     = &dual_bind_layout_;
            dual_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl_desc);
        }

        // Render pipelines
        threshold_pipeline_ = vivid::gpu::create_pipeline(gpu->device, threshold_shader_, single_pipe_layout_, gpu->output_format, "Bloom Threshold Pipeline");
        blur_h_pipeline_    = vivid::gpu::create_pipeline(gpu->device, blur_h_shader_,    single_pipe_layout_, gpu->output_format, "Bloom Blur H Pipeline");
        blur_v_pipeline_    = vivid::gpu::create_pipeline(gpu->device, blur_v_shader_,    single_pipe_layout_, gpu->output_format, "Bloom Blur V Pipeline");
        composite_pipeline_ = vivid::gpu::create_pipeline(gpu->device, composite_shader_, dual_pipe_layout_,   gpu->output_format, "Bloom Composite Pipeline");

        if (!threshold_pipeline_ || !blur_h_pipeline_ || !blur_v_pipeline_ || !composite_pipeline_)
            return false;

        // Create initial intermediates
        recreate_intermediates(gpu);

        return true;
    }
};

VIVID_REGISTER(Bloom)
