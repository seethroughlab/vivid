#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

// =============================================================================
// DoF Shaders
// =============================================================================

static const char* kDoFCoCFragment = R"(
struct DoFParams {
    focus_distance: f32,
    aperture: f32,
    max_blur: f32,
    near_plane: f32,
    far_plane: f32,
    texel_w: f32,
    texel_h: f32,
    _pad: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> params: DoFParams;
@group(0) @binding(1) var depth_tex: texture_2d<f32>;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

fn linearize_depth(d: f32, near: f32, far: f32) -> f32 {
    return near * far / (far - d * (far - near));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let dims = textureDimensions(depth_tex);
    let px = vec2i(input.uv * vec2f(f32(dims.x), f32(dims.y)));
    let raw_depth = textureLoad(depth_tex, clamp(px, vec2i(0), vec2i(dims) - vec2i(1)), 0).r;

    let linear_d = linearize_depth(raw_depth, params.near_plane, params.far_plane);
    let coc = clamp(
        (linear_d - params.focus_distance) * params.aperture / max(linear_d, 0.001),
        -params.max_blur, params.max_blur
    );

    // Store signed CoC normalized to max_blur range
    let normalized = coc / max(params.max_blur, 0.001);
    // Map [-1, 1] to [0, 1] for R16Float (which supports negative, but keeping range sane)
    return vec4f(coc, 0.0, 0.0, 1.0);
}
)";

static const char* kDoFBlurHFragment = R"(
struct DoFParams {
    focus_distance: f32,
    aperture: f32,
    max_blur: f32,
    near_plane: f32,
    far_plane: f32,
    texel_w: f32,
    texel_h: f32,
    _pad: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> params: DoFParams;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var color_tex: texture_2d<f32>;
@group(0) @binding(3) var coc_tex: texture_2d<f32>;

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
    let center_coc = textureSample(coc_tex, texSampler, input.uv).r;
    let blur_radius = abs(center_coc);

    if (blur_radius < 0.5) {
        return textureSample(color_tex, texSampler, input.uv);
    }

    // Near-field dilation: use max CoC in neighborhood
    var max_coc = blur_radius;
    for (var i: i32 = -2; i <= 2; i++) {
        let sample_uv = input.uv + vec2f(f32(i) * params.texel_w * 2.0, 0.0);
        let c = abs(textureSample(coc_tex, texSampler, sample_uv).r);
        max_coc = max(max_coc, c);
    }

    let kernel_radius = min(max_coc, params.max_blur);
    let steps = i32(clamp(kernel_radius, 1.0, 16.0));

    var color = vec4f(0.0);
    var total_weight: f32 = 0.0;

    for (var i: i32 = -steps; i <= steps; i++) {
        let offset = f32(i) / f32(steps) * kernel_radius;
        let sample_uv = input.uv + vec2f(offset * params.texel_w, 0.0);
        let sample_color = textureSample(color_tex, texSampler, sample_uv);

        // Gaussian-ish weight
        let t = f32(i) / max(f32(steps), 1.0);
        let w = exp(-t * t * 2.0);

        color += sample_color * w;
        total_weight += w;
    }

    return color / max(total_weight, 0.001);
}
)";

static const char* kDoFBlurVFragment = R"(
struct DoFParams {
    focus_distance: f32,
    aperture: f32,
    max_blur: f32,
    near_plane: f32,
    far_plane: f32,
    texel_w: f32,
    texel_h: f32,
    _pad: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> params: DoFParams;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var color_tex: texture_2d<f32>;
@group(0) @binding(3) var coc_tex: texture_2d<f32>;

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
    let center_coc = textureSample(coc_tex, texSampler, input.uv).r;
    let blur_radius = abs(center_coc);

    if (blur_radius < 0.5) {
        return textureSample(color_tex, texSampler, input.uv);
    }

    // Near-field dilation: use max CoC in neighborhood
    var max_coc = blur_radius;
    for (var i: i32 = -2; i <= 2; i++) {
        let sample_uv = input.uv + vec2f(0.0, f32(i) * params.texel_h * 2.0);
        let c = abs(textureSample(coc_tex, texSampler, sample_uv).r);
        max_coc = max(max_coc, c);
    }

    let kernel_radius = min(max_coc, params.max_blur);
    let steps = i32(clamp(kernel_radius, 1.0, 16.0));

    var color = vec4f(0.0);
    var total_weight: f32 = 0.0;

    for (var i: i32 = -steps; i <= steps; i++) {
        let offset = f32(i) / f32(steps) * kernel_radius;
        let sample_uv = input.uv + vec2f(0.0, offset * params.texel_h);
        let sample_color = textureSample(color_tex, texSampler, sample_uv);

        // Gaussian-ish weight
        let t = f32(i) / max(f32(steps), 1.0);
        let w = exp(-t * t * 2.0);

        color += sample_color * w;
        total_weight += w;
    }

    return color / max(total_weight, 0.001);
}
)";

// =============================================================================
// Uniform struct (must match WGSL DoFParams)
// =============================================================================

struct DoFUniforms {
    float focus_distance;
    float aperture;
    float max_blur;
    float near_plane;
    float far_plane;
    float texel_w;
    float texel_h;
    float _pad;
};

// =============================================================================
// DepthOfField3D Operator
// =============================================================================

struct DepthOfField3D : vivid::OperatorBase {
    static constexpr const char* kName   = "DepthOfField3D";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> focus_distance {"focus_distance", 5.0f,  0.1f, 100.0f};
    vivid::Param<float> aperture       {"aperture",       0.5f,  0.0f, 2.0f};
    vivid::Param<float> max_blur       {"max_blur",       8.0f,  0.0f, 32.0f};
    vivid::Param<float> near_plane     {"near_plane",     0.1f,  0.001f, 10.0f};
    vivid::Param<float> far_plane      {"far_plane",      100.0f, 1.0f, 10000.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&focus_distance);
        out.push_back(&aperture);
        out.push_back(&max_blur);
        out.push_back(&near_plane);
        out.push_back(&far_plane);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"color",   VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"depth",   VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        if (!coc_pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[dof3d] lazy_init FAILED\n");
                return;
            }
        }

        // Get inputs
        WGPUTextureView color_input = nullptr;
        WGPUTextureView depth_input = nullptr;
        if (gpu->input_texture_views && gpu->input_texture_count >= 1)
            color_input = gpu->input_texture_views[0];
        if (gpu->input_texture_views && gpu->input_texture_count >= 2)
            depth_input = gpu->input_texture_views[1];

        if (!color_input || !depth_input) return;

        // Resize intermediates
        if (gpu->output_width != cached_w_ || gpu->output_height != cached_h_) {
            recreate_intermediates(gpu);
            cached_w_ = gpu->output_width;
            cached_h_ = gpu->output_height;
        }

        // Update uniforms
        DoFUniforms u{};
        u.focus_distance = focus_distance.value;
        u.aperture       = aperture.value;
        u.max_blur       = max_blur.value;
        u.near_plane     = near_plane.value;
        u.far_plane      = far_plane.value;
        u.texel_w        = 1.0f / static_cast<float>(gpu->output_width);
        u.texel_h        = 1.0f / static_cast<float>(gpu->output_height);
        wgpuQueueWriteBuffer(gpu->queue, uniform_buf_, 0, &u, sizeof(u));

        // Rebuild bind groups if inputs changed
        if (color_input != cached_color_ || depth_input != cached_depth_ || bg_dirty_) {
            rebuild_bind_groups(gpu, color_input, depth_input);
            cached_color_ = color_input;
            cached_depth_ = depth_input;
            bg_dirty_ = false;
        }

        static constexpr WGPUColor kClear{0, 0, 0, 0};

        // Pass 1: CoC → coc_view_ (R16Float)
        vivid::gpu::run_pass(gpu->command_encoder, coc_pipeline_, coc_bg_,
                             coc_view_, "DoF CoC", kClear);

        // Pass 2: Horizontal blur → blur_h_view_
        vivid::gpu::run_pass(gpu->command_encoder, blur_h_pipeline_, blur_h_bg_,
                             blur_h_view_, "DoF Blur H", kClear);

        // Pass 3: Vertical blur → output
        vivid::gpu::run_pass(gpu->command_encoder, blur_v_pipeline_, blur_v_bg_,
                             gpu->output_texture_view, "DoF Blur V", kClear);
    }

    ~DepthOfField3D() override {
        vivid::gpu::release(coc_pipeline_);
        vivid::gpu::release(blur_h_pipeline_);
        vivid::gpu::release(blur_v_pipeline_);
        vivid::gpu::release(coc_bgl_);
        vivid::gpu::release(blur_bgl_);
        vivid::gpu::release(coc_pipe_layout_);
        vivid::gpu::release(blur_pipe_layout_);
        vivid::gpu::release(coc_shader_);
        vivid::gpu::release(blur_h_shader_);
        vivid::gpu::release(blur_v_shader_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(coc_bg_);
        vivid::gpu::release(blur_h_bg_);
        vivid::gpu::release(blur_v_bg_);
        vivid::gpu::release(coc_tex_);
        vivid::gpu::release(coc_view_);
        vivid::gpu::release(blur_h_tex_);
        vivid::gpu::release(blur_h_view_);
    }

private:
    WGPURenderPipeline coc_pipeline_    = nullptr;
    WGPURenderPipeline blur_h_pipeline_ = nullptr;
    WGPURenderPipeline blur_v_pipeline_ = nullptr;

    WGPUBindGroupLayout coc_bgl_  = nullptr;
    WGPUBindGroupLayout blur_bgl_ = nullptr;
    WGPUPipelineLayout  coc_pipe_layout_  = nullptr;
    WGPUPipelineLayout  blur_pipe_layout_ = nullptr;

    WGPUShaderModule coc_shader_    = nullptr;
    WGPUShaderModule blur_h_shader_ = nullptr;
    WGPUShaderModule blur_v_shader_ = nullptr;

    WGPUBuffer  uniform_buf_ = nullptr;
    WGPUSampler sampler_     = nullptr;

    WGPUBindGroup coc_bg_    = nullptr;
    WGPUBindGroup blur_h_bg_ = nullptr;
    WGPUBindGroup blur_v_bg_ = nullptr;

    WGPUTexture     coc_tex_    = nullptr;
    WGPUTextureView coc_view_   = nullptr;
    WGPUTexture     blur_h_tex_ = nullptr;
    WGPUTextureView blur_h_view_ = nullptr;

    WGPUTextureView cached_color_ = nullptr;
    WGPUTextureView cached_depth_ = nullptr;
    uint32_t cached_w_ = 0;
    uint32_t cached_h_ = 0;
    bool bg_dirty_ = true;

    void recreate_intermediates(VividGpuState* gpu) {
        vivid::gpu::release(coc_tex_);
        vivid::gpu::release(coc_view_);
        vivid::gpu::release(blur_h_tex_);
        vivid::gpu::release(blur_h_view_);

        // CoC intermediate: R16Float
        {
            WGPUTextureDescriptor td{};
            td.label = vivid_sv("DoF CoC");
            td.size = { gpu->output_width, gpu->output_height, 1 };
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D;
            td.format = WGPUTextureFormat_R16Float;
            td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
            coc_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

            WGPUTextureViewDescriptor vd{};
            vd.format = WGPUTextureFormat_R16Float;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            coc_view_ = wgpuTextureCreateView(coc_tex_, &vd);
        }

        // Horizontal blur intermediate: RGBA8Unorm
        {
            WGPUTextureDescriptor td{};
            td.label = vivid_sv("DoF Blur H");
            td.size = { gpu->output_width, gpu->output_height, 1 };
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D;
            td.format = gpu->output_format;
            td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
            blur_h_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

            WGPUTextureViewDescriptor vd{};
            vd.format = gpu->output_format;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            blur_h_view_ = wgpuTextureCreateView(blur_h_tex_, &vd);
        }

        bg_dirty_ = true;
    }

    void rebuild_bind_groups(VividGpuState* gpu, WGPUTextureView color_input,
                              WGPUTextureView depth_input) {
        vivid::gpu::release(coc_bg_);
        vivid::gpu::release(blur_h_bg_);
        vivid::gpu::release(blur_v_bg_);

        // CoC: uniform + depth
        {
            WGPUBindGroupEntry entries[2]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(DoFUniforms);
            entries[1].binding = 1;
            entries[1].textureView = depth_input;

            WGPUBindGroupDescriptor desc{};
            desc.label = vivid_sv("DoF CoC BG");
            desc.layout = coc_bgl_;
            desc.entryCount = 2;
            desc.entries = entries;
            coc_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Blur H: uniform + sampler + color + coc
        {
            WGPUBindGroupEntry entries[4]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(DoFUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = color_input;
            entries[3].binding = 3;
            entries[3].textureView = coc_view_;

            WGPUBindGroupDescriptor desc{};
            desc.label = vivid_sv("DoF Blur H BG");
            desc.layout = blur_bgl_;
            desc.entryCount = 4;
            desc.entries = entries;
            blur_h_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Blur V: uniform + sampler + blur_h result + coc
        {
            WGPUBindGroupEntry entries[4]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(DoFUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = blur_h_view_;
            entries[3].binding = 3;
            entries[3].textureView = coc_view_;

            WGPUBindGroupDescriptor desc{};
            desc.label = vivid_sv("DoF Blur V BG");
            desc.layout = blur_bgl_;
            desc.entryCount = 4;
            desc.entries = entries;
            blur_v_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }
    }

    bool lazy_init(VividGpuState* gpu) {
        coc_shader_    = vivid::gpu::create_shader(gpu->device, kDoFCoCFragment, "DoF CoC Shader");
        blur_h_shader_ = vivid::gpu::create_shader(gpu->device, kDoFBlurHFragment, "DoF Blur H Shader");
        blur_v_shader_ = vivid::gpu::create_shader(gpu->device, kDoFBlurVFragment, "DoF Blur V Shader");
        if (!coc_shader_ || !blur_h_shader_ || !blur_v_shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(DoFUniforms), "DoF Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "DoF Sampler");

        // --- CoC BGL: uniform(0) + depth_tex(1, unfilterable float) ---
        {
            WGPUBindGroupLayoutEntry entries[2]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(DoFUniforms);

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].texture.sampleType    = WGPUTextureSampleType_UnfilterableFloat;
            entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label = vivid_sv("DoF CoC BGL");
            bgl_desc.entryCount = 2;
            bgl_desc.entries = entries;
            coc_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // --- Blur BGL: uniform(0) + sampler(1) + color/blur(2) + coc(3) ---
        {
            WGPUBindGroupLayoutEntry entries[4]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(DoFUniforms);

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[2].binding    = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

            entries[3].binding    = 3;
            entries[3].visibility = WGPUShaderStage_Fragment;
            entries[3].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label = vivid_sv("DoF Blur BGL");
            bgl_desc.entryCount = 4;
            bgl_desc.entries = entries;
            blur_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // --- Pipeline layouts ---
        {
            WGPUPipelineLayoutDescriptor pl{};
            pl.label = vivid_sv("DoF CoC PL");
            pl.bindGroupLayoutCount = 1;
            pl.bindGroupLayouts = &coc_bgl_;
            coc_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);
        }
        {
            WGPUPipelineLayoutDescriptor pl{};
            pl.label = vivid_sv("DoF Blur PL");
            pl.bindGroupLayoutCount = 1;
            pl.bindGroupLayouts = &blur_bgl_;
            blur_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);
        }

        // --- Pipelines ---
        // CoC → R16Float
        {
            WGPUColorTargetState ct{};
            ct.format = WGPUTextureFormat_R16Float;
            ct.writeMask = WGPUColorWriteMask_All;

            WGPUFragmentState frag{};
            frag.module = coc_shader_;
            frag.entryPoint = vivid_sv("fs_main");
            frag.targetCount = 1;
            frag.targets = &ct;

            WGPURenderPipelineDescriptor rp{};
            rp.label = vivid_sv("DoF CoC Pipeline");
            rp.layout = coc_pipe_layout_;
            rp.vertex.module = coc_shader_;
            rp.vertex.entryPoint = vivid_sv("vs_main");
            rp.primitive.topology = WGPUPrimitiveTopology_TriangleList;
            rp.primitive.cullMode = WGPUCullMode_None;
            rp.multisample.count = 1;
            rp.multisample.mask = 0xFFFFFFFF;
            rp.fragment = &frag;

            coc_pipeline_ = wgpuDeviceCreateRenderPipeline(gpu->device, &rp);
        }

        // Blur H → output format
        blur_h_pipeline_ = vivid::gpu::create_pipeline(gpu->device, blur_h_shader_,
                                                         blur_pipe_layout_,
                                                         gpu->output_format,
                                                         "DoF Blur H Pipeline");

        // Blur V → output format
        blur_v_pipeline_ = vivid::gpu::create_pipeline(gpu->device, blur_v_shader_,
                                                         blur_pipe_layout_,
                                                         gpu->output_format,
                                                         "DoF Blur V Pipeline");

        if (!coc_pipeline_ || !blur_h_pipeline_ || !blur_v_pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(DepthOfField3D)
