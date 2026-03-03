#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

// =============================================================================
// SSAO Shaders
// =============================================================================

static const char* kSSAORawFragment = R"(
struct SSAOParams {
    radius: f32,
    intensity: f32,
    bias: f32,
    near_plane: f32,
    far_plane: f32,
    texel_w: f32,
    texel_h: f32,
    _pad: f32,
    kernel: array<vec4f, 16>,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> params: SSAOParams;
@group(0) @binding(1) var depth_tex: texture_2d<f32>;
@group(0) @binding(2) var noise_tex: texture_2d<f32>;

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

    // Background: no occlusion
    if (raw_depth >= 0.999) {
        return vec4f(1.0, 1.0, 1.0, 1.0);
    }

    let linear_d = linearize_depth(raw_depth, params.near_plane, params.far_plane);

    // Reconstruct normal from depth gradients
    let dx_px = clamp(px + vec2i(1, 0), vec2i(0), vec2i(dims) - vec2i(1));
    let dy_px = clamp(px + vec2i(0, 1), vec2i(0), vec2i(dims) - vec2i(1));
    let dx_nx = clamp(px - vec2i(1, 0), vec2i(0), vec2i(dims) - vec2i(1));
    let dy_ny = clamp(px - vec2i(0, 1), vec2i(0), vec2i(dims) - vec2i(1));

    let d_right = linearize_depth(textureLoad(depth_tex, dx_px, 0).r, params.near_plane, params.far_plane);
    let d_left  = linearize_depth(textureLoad(depth_tex, dx_nx, 0).r, params.near_plane, params.far_plane);
    let d_up    = linearize_depth(textureLoad(depth_tex, dy_px, 0).r, params.near_plane, params.far_plane);
    let d_down  = linearize_depth(textureLoad(depth_tex, dy_ny, 0).r, params.near_plane, params.far_plane);

    let ddx = (d_right - d_left) * 0.5;
    let ddy = (d_up - d_down) * 0.5;
    let normal = normalize(vec3f(-ddx, -ddy, 1.0));

    // Random rotation from 4x4 noise texture
    let noise_px = vec2i(px.x % 4, px.y % 4);
    let noise = textureLoad(noise_tex, noise_px, 0).xy * 2.0 - 1.0;
    let tangent = normalize(vec3f(noise.x, noise.y, 0.0) - normal * dot(vec3f(noise.x, noise.y, 0.0), normal));
    let bitangent = cross(normal, tangent);
    let tbn = mat3x3f(tangent, bitangent, normal);

    // Sample hemisphere kernel
    var occlusion: f32 = 0.0;
    let sample_radius = params.radius / linear_d;

    for (var i: u32 = 0u; i < 16u; i++) {
        let sample_offset = tbn * params.kernel[i].xyz;
        let sample_uv = input.uv + sample_offset.xy * sample_radius * vec2f(params.texel_w, params.texel_h) * 100.0;
        let sample_px = vec2i(sample_uv * vec2f(f32(dims.x), f32(dims.y)));
        let clamped_px = clamp(sample_px, vec2i(0), vec2i(dims) - vec2i(1));
        let sample_depth = textureLoad(depth_tex, clamped_px, 0).r;
        let sample_linear = linearize_depth(sample_depth, params.near_plane, params.far_plane);

        let range_check = smoothstep(0.0, 1.0, params.radius / abs(linear_d - sample_linear));
        if (sample_linear < linear_d - params.bias) {
            occlusion += range_check;
        }
    }

    let ao = 1.0 - (occlusion / 16.0) * params.intensity;
    return vec4f(ao, ao, ao, 1.0);
}
)";

static const char* kSSAOBlurFragment = R"(
struct SSAOParams {
    radius: f32,
    intensity: f32,
    bias: f32,
    near_plane: f32,
    far_plane: f32,
    texel_w: f32,
    texel_h: f32,
    _pad: f32,
    kernel: array<vec4f, 16>,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> params: SSAOParams;
@group(0) @binding(1) var ao_tex: texture_2d<f32>;
@group(0) @binding(2) var depth_tex: texture_2d<f32>;

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
    let dims = textureDimensions(ao_tex);
    let px = vec2i(input.uv * vec2f(f32(dims.x), f32(dims.y)));
    let center_ao = textureLoad(ao_tex, clamp(px, vec2i(0), vec2i(dims) - vec2i(1)), 0).r;
    let center_depth = textureLoad(depth_tex, clamp(px, vec2i(0), vec2i(dims) - vec2i(1)), 0).r;
    let center_linear = linearize_depth(center_depth, params.near_plane, params.far_plane);

    var total: f32 = center_ao;
    var weight: f32 = 1.0;

    for (var y: i32 = -2; y <= 2; y++) {
        for (var x: i32 = -2; x <= 2; x++) {
            if (x == 0 && y == 0) { continue; }
            let sample_px = clamp(px + vec2i(x, y), vec2i(0), vec2i(dims) - vec2i(1));
            let s_ao = textureLoad(ao_tex, sample_px, 0).r;
            let s_depth = textureLoad(depth_tex, sample_px, 0).r;
            let s_linear = linearize_depth(s_depth, params.near_plane, params.far_plane);

            // Depth-aware bilateral weight
            let depth_diff = abs(center_linear - s_linear);
            let w = exp(-depth_diff * 10.0);
            total += s_ao * w;
            weight += w;
        }
    }

    let blurred = total / weight;
    return vec4f(blurred, blurred, blurred, 1.0);
}
)";

static const char* kSSAOCompositeFragment = R"(
struct SSAOParams {
    radius: f32,
    intensity: f32,
    bias: f32,
    near_plane: f32,
    far_plane: f32,
    texel_w: f32,
    texel_h: f32,
    _pad: f32,
    kernel: array<vec4f, 16>,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> params: SSAOParams;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var color_tex: texture_2d<f32>;
@group(0) @binding(3) var ao_tex: texture_2d<f32>;

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
    let color = textureSample(color_tex, texSampler, input.uv);
    let ao = textureSample(ao_tex, texSampler, input.uv).r;
    return vec4f(color.rgb * ao, color.a);
}
)";

// =============================================================================
// Uniform struct (must match WGSL SSAOParams)
// =============================================================================

struct SSAOUniforms {
    float radius;
    float intensity;
    float bias;
    float near_plane;
    float far_plane;
    float texel_w;
    float texel_h;
    float _pad;
    float kernel[16][4];  // 16 vec4f hemisphere samples
};

// =============================================================================
// SSAO3D Operator
// =============================================================================

struct SSAO3D : vivid::OperatorBase {
    static constexpr const char* kName   = "SSAO3D";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> radius     {"radius",     0.5f,  0.01f, 5.0f};
    vivid::Param<float> intensity  {"intensity",  1.0f,  0.0f,  3.0f};
    vivid::Param<float> bias       {"bias",       0.025f, 0.0f, 0.1f};
    vivid::Param<float> near_plane {"near_plane", 0.1f,  0.001f, 10.0f};
    vivid::Param<float> far_plane  {"far_plane",  100.0f, 1.0f, 10000.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&radius);
        out.push_back(&intensity);
        out.push_back(&bias);
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

        if (!raw_pipeline_) {
            if (!lazy_init(gpu)) {
                std::fprintf(stderr, "[ssao3d] lazy_init FAILED\n");
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

        if (!color_input || !depth_input) {
            // Pass through or clear
            return;
        }

        // Resize intermediates
        if (gpu->output_width != cached_w_ || gpu->output_height != cached_h_) {
            recreate_intermediates(gpu);
            cached_w_ = gpu->output_width;
            cached_h_ = gpu->output_height;
        }

        // Update uniforms
        SSAOUniforms u{};
        u.radius     = radius.value;
        u.intensity  = intensity.value;
        u.bias       = bias.value;
        u.near_plane = near_plane.value;
        u.far_plane  = far_plane.value;
        u.texel_w    = 1.0f / static_cast<float>(gpu->output_width);
        u.texel_h    = 1.0f / static_cast<float>(gpu->output_height);
        std::memcpy(u.kernel, kernel_, sizeof(kernel_));
        wgpuQueueWriteBuffer(gpu->queue, uniform_buf_, 0, &u, sizeof(u));

        // Rebuild bind groups if inputs changed
        if (color_input != cached_color_ || depth_input != cached_depth_ || bg_dirty_) {
            rebuild_bind_groups(gpu, color_input, depth_input);
            cached_color_ = color_input;
            cached_depth_ = depth_input;
            bg_dirty_ = false;
        }

        static constexpr WGPUColor kClear{0, 0, 0, 0};

        // Pass 1: Raw SSAO → inter_a (R8Unorm)
        vivid::gpu::run_pass(gpu->command_encoder, raw_pipeline_, raw_bg_,
                             inter_view_a_, "SSAO Raw", kClear);

        // Pass 2: Bilateral blur → inter_b (R8Unorm)
        vivid::gpu::run_pass(gpu->command_encoder, blur_pipeline_, blur_bg_,
                             inter_view_b_, "SSAO Blur", kClear);

        // Pass 3: Composite → output
        vivid::gpu::run_pass(gpu->command_encoder, composite_pipeline_, composite_bg_,
                             gpu->output_texture_view, "SSAO Composite", kClear);
    }

    ~SSAO3D() override {
        vivid::gpu::release(raw_pipeline_);
        vivid::gpu::release(blur_pipeline_);
        vivid::gpu::release(composite_pipeline_);
        vivid::gpu::release(raw_bgl_);
        vivid::gpu::release(blur_bgl_);
        vivid::gpu::release(composite_bgl_);
        vivid::gpu::release(raw_pipe_layout_);
        vivid::gpu::release(blur_pipe_layout_);
        vivid::gpu::release(composite_pipe_layout_);
        vivid::gpu::release(raw_shader_);
        vivid::gpu::release(blur_shader_);
        vivid::gpu::release(composite_shader_);
        vivid::gpu::release(uniform_buf_);
        vivid::gpu::release(sampler_);
        vivid::gpu::release(raw_bg_);
        vivid::gpu::release(blur_bg_);
        vivid::gpu::release(composite_bg_);
        vivid::gpu::release(inter_tex_a_);
        vivid::gpu::release(inter_view_a_);
        vivid::gpu::release(inter_tex_b_);
        vivid::gpu::release(inter_view_b_);
        vivid::gpu::release(noise_tex_);
        vivid::gpu::release(noise_view_);
    }

private:
    WGPURenderPipeline raw_pipeline_       = nullptr;
    WGPURenderPipeline blur_pipeline_      = nullptr;
    WGPURenderPipeline composite_pipeline_ = nullptr;

    WGPUBindGroupLayout raw_bgl_       = nullptr;
    WGPUBindGroupLayout blur_bgl_      = nullptr;
    WGPUBindGroupLayout composite_bgl_ = nullptr;
    WGPUPipelineLayout  raw_pipe_layout_       = nullptr;
    WGPUPipelineLayout  blur_pipe_layout_      = nullptr;
    WGPUPipelineLayout  composite_pipe_layout_ = nullptr;

    WGPUShaderModule raw_shader_       = nullptr;
    WGPUShaderModule blur_shader_      = nullptr;
    WGPUShaderModule composite_shader_ = nullptr;

    WGPUBuffer  uniform_buf_ = nullptr;
    WGPUSampler sampler_     = nullptr;

    WGPUBindGroup raw_bg_       = nullptr;
    WGPUBindGroup blur_bg_      = nullptr;
    WGPUBindGroup composite_bg_ = nullptr;

    WGPUTexture     inter_tex_a_  = nullptr;
    WGPUTextureView inter_view_a_ = nullptr;
    WGPUTexture     inter_tex_b_  = nullptr;
    WGPUTextureView inter_view_b_ = nullptr;

    WGPUTexture     noise_tex_  = nullptr;
    WGPUTextureView noise_view_ = nullptr;

    WGPUTextureView cached_color_ = nullptr;
    WGPUTextureView cached_depth_ = nullptr;
    uint32_t cached_w_ = 0;
    uint32_t cached_h_ = 0;
    bool bg_dirty_ = true;

    // Pre-computed hemisphere kernel
    float kernel_[16][4];

    void generate_kernel() {
        // Simple hemisphere kernel with progressive scaling
        // Using a fixed seed for determinism
        uint32_t seed = 0x12345678;
        auto rng = [&]() -> float {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return static_cast<float>(seed & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
        };

        for (int i = 0; i < 16; ++i) {
            float x = rng() * 2.0f - 1.0f;
            float y = rng() * 2.0f - 1.0f;
            float z = rng();  // hemisphere: z in [0, 1]

            float len = std::sqrt(x*x + y*y + z*z);
            if (len < 0.001f) len = 1.0f;
            x /= len; y /= len; z /= len;

            // Scale: more samples closer to the center
            float scale = static_cast<float>(i) / 16.0f;
            scale = 0.1f + scale * scale * 0.9f;
            kernel_[i][0] = x * scale;
            kernel_[i][1] = y * scale;
            kernel_[i][2] = z * scale;
            kernel_[i][3] = 0.0f;
        }
    }

    void create_noise_texture(VividGpuState* gpu) {
        // 4x4 random rotation vectors
        uint32_t seed = 0xDEADBEEF;
        auto rng = [&]() -> float {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            return static_cast<float>(seed & 0xFFFFFF) / static_cast<float>(0xFFFFFF);
        };

        uint8_t noise_data[4 * 4 * 4];  // 4x4 RGBA8
        for (int i = 0; i < 16; ++i) {
            noise_data[i * 4 + 0] = static_cast<uint8_t>(rng() * 255.0f);
            noise_data[i * 4 + 1] = static_cast<uint8_t>(rng() * 255.0f);
            noise_data[i * 4 + 2] = 0;
            noise_data[i * 4 + 3] = 255;
        }

        WGPUTextureDescriptor td{};
        td.label = vivid_sv("SSAO Noise");
        td.size = { 4, 4, 1 };
        td.mipLevelCount = 1;
        td.sampleCount = 1;
        td.dimension = WGPUTextureDimension_2D;
        td.format = WGPUTextureFormat_RGBA8Unorm;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        noise_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

        WGPUTexelCopyTextureInfo dst{};
        dst.texture = noise_tex_;
        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = 4 * 4;
        layout.rowsPerImage = 4;
        WGPUExtent3D extent = {4, 4, 1};
        wgpuQueueWriteTexture(gpu->queue, &dst, noise_data, sizeof(noise_data), &layout, &extent);

        WGPUTextureViewDescriptor vd{};
        vd.format = WGPUTextureFormat_RGBA8Unorm;
        vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1;
        vd.arrayLayerCount = 1;
        noise_view_ = wgpuTextureCreateView(noise_tex_, &vd);
    }

    void recreate_intermediates(VividGpuState* gpu) {
        vivid::gpu::release(inter_tex_a_);
        vivid::gpu::release(inter_view_a_);
        vivid::gpu::release(inter_tex_b_);
        vivid::gpu::release(inter_view_b_);

        for (int i = 0; i < 2; ++i) {
            WGPUTextureDescriptor td{};
            td.label = vivid_sv(i == 0 ? "SSAO Inter A" : "SSAO Inter B");
            td.size = { gpu->output_width, gpu->output_height, 1 };
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D;
            td.format = WGPUTextureFormat_RGBA8Unorm;
            td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;

            WGPUTexture tex = wgpuDeviceCreateTexture(gpu->device, &td);

            WGPUTextureViewDescriptor vd{};
            vd.format = WGPUTextureFormat_RGBA8Unorm;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            WGPUTextureView view = wgpuTextureCreateView(tex, &vd);

            if (i == 0) { inter_tex_a_ = tex; inter_view_a_ = view; }
            else        { inter_tex_b_ = tex; inter_view_b_ = view; }
        }
        bg_dirty_ = true;
    }

    void rebuild_bind_groups(VividGpuState* gpu, WGPUTextureView color_input,
                              WGPUTextureView depth_input) {
        vivid::gpu::release(raw_bg_);
        vivid::gpu::release(blur_bg_);
        vivid::gpu::release(composite_bg_);

        // Raw: uniform + depth + noise
        {
            WGPUBindGroupEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(SSAOUniforms);
            entries[1].binding = 1;
            entries[1].textureView = depth_input;
            entries[2].binding = 2;
            entries[2].textureView = noise_view_;

            WGPUBindGroupDescriptor desc{};
            desc.label = vivid_sv("SSAO Raw BG");
            desc.layout = raw_bgl_;
            desc.entryCount = 3;
            desc.entries = entries;
            raw_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Blur: uniform + ao_tex(inter_a) + depth
        {
            WGPUBindGroupEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(SSAOUniforms);
            entries[1].binding = 1;
            entries[1].textureView = inter_view_a_;
            entries[2].binding = 2;
            entries[2].textureView = depth_input;

            WGPUBindGroupDescriptor desc{};
            desc.label = vivid_sv("SSAO Blur BG");
            desc.layout = blur_bgl_;
            desc.entryCount = 3;
            desc.entries = entries;
            blur_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }

        // Composite: uniform + sampler + color + ao(inter_b)
        {
            WGPUBindGroupEntry entries[4]{};
            entries[0].binding = 0;
            entries[0].buffer  = uniform_buf_;
            entries[0].size    = sizeof(SSAOUniforms);
            entries[1].binding = 1;
            entries[1].sampler = sampler_;
            entries[2].binding = 2;
            entries[2].textureView = color_input;
            entries[3].binding = 3;
            entries[3].textureView = inter_view_b_;

            WGPUBindGroupDescriptor desc{};
            desc.label = vivid_sv("SSAO Composite BG");
            desc.layout = composite_bgl_;
            desc.entryCount = 4;
            desc.entries = entries;
            composite_bg_ = wgpuDeviceCreateBindGroup(gpu->device, &desc);
        }
    }

    bool lazy_init(VividGpuState* gpu) {
        generate_kernel();
        create_noise_texture(gpu);

        raw_shader_       = vivid::gpu::create_shader(gpu->device, kSSAORawFragment, "SSAO Raw Shader");
        blur_shader_      = vivid::gpu::create_shader(gpu->device, kSSAOBlurFragment, "SSAO Blur Shader");
        composite_shader_ = vivid::gpu::create_shader(gpu->device, kSSAOCompositeFragment, "SSAO Composite Shader");
        if (!raw_shader_ || !blur_shader_ || !composite_shader_) return false;

        uniform_buf_ = vivid::gpu::create_uniform_buffer(gpu->device, sizeof(SSAOUniforms), "SSAO Uniforms");
        sampler_ = vivid::gpu::create_linear_sampler(gpu->device, "SSAO Sampler");

        // --- Raw BGL: uniform(0) + depth_tex(1, unfilterable float) + noise_tex(2) ---
        {
            WGPUBindGroupLayoutEntry entries[3]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(SSAOUniforms);

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].texture.sampleType    = WGPUTextureSampleType_UnfilterableFloat;
            entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

            entries[2].binding    = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label = vivid_sv("SSAO Raw BGL");
            bgl_desc.entryCount = 3;
            bgl_desc.entries = entries;
            raw_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // --- Blur BGL: uniform(0) + ao_tex(1) + depth_tex(2, unfilterable float) ---
        {
            WGPUBindGroupLayoutEntry entries[3]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(SSAOUniforms);

            entries[1].binding    = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].texture.sampleType    = WGPUTextureSampleType_Float;
            entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

            entries[2].binding    = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].texture.sampleType    = WGPUTextureSampleType_UnfilterableFloat;
            entries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutDescriptor bgl_desc{};
            bgl_desc.label = vivid_sv("SSAO Blur BGL");
            bgl_desc.entryCount = 3;
            bgl_desc.entries = entries;
            blur_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // --- Composite BGL: uniform(0) + sampler(1) + color(2) + ao(3) ---
        {
            WGPUBindGroupLayoutEntry entries[4]{};
            entries[0].binding    = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].buffer.type           = WGPUBufferBindingType_Uniform;
            entries[0].buffer.minBindingSize = sizeof(SSAOUniforms);

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
            bgl_desc.label = vivid_sv("SSAO Composite BGL");
            bgl_desc.entryCount = 4;
            bgl_desc.entries = entries;
            composite_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &bgl_desc);
        }

        // --- Pipeline layouts ---
        {
            WGPUPipelineLayoutDescriptor pl{};
            pl.label = vivid_sv("SSAO Raw PL");
            pl.bindGroupLayoutCount = 1;
            pl.bindGroupLayouts = &raw_bgl_;
            raw_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);
        }
        {
            WGPUPipelineLayoutDescriptor pl{};
            pl.label = vivid_sv("SSAO Blur PL");
            pl.bindGroupLayoutCount = 1;
            pl.bindGroupLayouts = &blur_bgl_;
            blur_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);
        }
        {
            WGPUPipelineLayoutDescriptor pl{};
            pl.label = vivid_sv("SSAO Composite PL");
            pl.bindGroupLayoutCount = 1;
            pl.bindGroupLayouts = &composite_bgl_;
            composite_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &pl);
        }

        // --- Pipelines ---
        // Raw → RGBA8Unorm (stores AO in R channel, but using RGBA for simplicity)
        raw_pipeline_ = vivid::gpu::create_pipeline(gpu->device, raw_shader_,
                                                      raw_pipe_layout_,
                                                      WGPUTextureFormat_RGBA8Unorm,
                                                      "SSAO Raw Pipeline");

        // Blur → RGBA8Unorm
        blur_pipeline_ = vivid::gpu::create_pipeline(gpu->device, blur_shader_,
                                                       blur_pipe_layout_,
                                                       WGPUTextureFormat_RGBA8Unorm,
                                                       "SSAO Blur Pipeline");

        // Composite → output format
        composite_pipeline_ = vivid::gpu::create_pipeline(gpu->device, composite_shader_,
                                                            composite_pipe_layout_,
                                                            gpu->output_format,
                                                            "SSAO Composite Pipeline");

        if (!raw_pipeline_ || !blur_pipeline_ || !composite_pipeline_) return false;

        return true;
    }
};

VIVID_REGISTER(SSAO3D)
