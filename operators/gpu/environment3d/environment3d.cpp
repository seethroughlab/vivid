#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include "operator_api/gpu_3d.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

// =============================================================================
// Equirect → Cubemap shader (per-face rendering)
// =============================================================================

static const char* kEquirectToCubeShader = R"(
struct FaceParams {
    face: u32,
    _pad0: u32,
    _pad1: u32,
    _pad2: u32,
}

@group(0) @binding(0) var equirect_sampler: sampler;
@group(0) @binding(1) var equirect_map: texture_2d<f32>;
@group(0) @binding(2) var<uniform> params: FaceParams;

struct FullscreenOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

fn fullscreenTriangle(vertexIndex: u32) -> FullscreenOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: FullscreenOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    return out;
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> FullscreenOutput {
    return fullscreenTriangle(vid);
}

const PI: f32 = 3.14159265358979323846;

fn face_uv_to_dir(face: u32, uv: vec2f) -> vec3f {
    // Map [0,1] UV to [-1,1]
    let u = uv.x * 2.0 - 1.0;
    let v = uv.y * 2.0 - 1.0;
    switch (face) {
        case 0u: { return normalize(vec3f( 1.0, -v, -u)); } // +X
        case 1u: { return normalize(vec3f(-1.0, -v,  u)); } // -X
        case 2u: { return normalize(vec3f( u,  1.0,  v)); } // +Y
        case 3u: { return normalize(vec3f( u, -1.0, -v)); } // -Y
        case 4u: { return normalize(vec3f( u,   -v, 1.0)); } // +Z
        default: { return normalize(vec3f(-u,   -v, -1.0)); } // -Z
    }
}

fn dir_to_equirect_uv(dir: vec3f) -> vec2f {
    let phi = atan2(dir.z, dir.x);
    let theta = asin(clamp(dir.y, -1.0, 1.0));
    return vec2f(
        phi / (2.0 * PI) + 0.5,
        -theta / PI + 0.5
    );
}

@fragment
fn fs_equirect_to_cube(in: FullscreenOutput) -> @location(0) vec4f {
    let dir = face_uv_to_dir(params.face, in.uv);
    let uv = dir_to_equirect_uv(dir);
    return textureSample(equirect_map, equirect_sampler, uv);
}
)";

// =============================================================================
// Irradiance convolution shader
// =============================================================================

static const char* kIrradianceShader = R"(
struct FaceParams {
    face: u32,
    _pad0: u32,
    _pad1: u32,
    _pad2: u32,
}

@group(0) @binding(0) var env_sampler: sampler;
@group(0) @binding(1) var env_cubemap: texture_cube<f32>;
@group(0) @binding(2) var<uniform> params: FaceParams;

struct FullscreenOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

fn fullscreenTriangle(vertexIndex: u32) -> FullscreenOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: FullscreenOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    return out;
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> FullscreenOutput {
    return fullscreenTriangle(vid);
}

const PI: f32 = 3.14159265358979323846;

fn face_uv_to_dir(face: u32, uv: vec2f) -> vec3f {
    let u = uv.x * 2.0 - 1.0;
    let v = uv.y * 2.0 - 1.0;
    switch (face) {
        case 0u: { return normalize(vec3f( 1.0, -v, -u)); }
        case 1u: { return normalize(vec3f(-1.0, -v,  u)); }
        case 2u: { return normalize(vec3f( u,  1.0,  v)); }
        case 3u: { return normalize(vec3f( u, -1.0, -v)); }
        case 4u: { return normalize(vec3f( u,   -v, 1.0)); }
        default: { return normalize(vec3f(-u,   -v, -1.0)); }
    }
}

@fragment
fn fs_irradiance(in: FullscreenOutput) -> @location(0) vec4f {
    let N = face_uv_to_dir(params.face, in.uv);

    // Build tangent space
    var up = vec3f(0.0, 1.0, 0.0);
    if (abs(N.y) > 0.999) { up = vec3f(0.0, 0.0, 1.0); }
    let right = normalize(cross(up, N));
    let up2 = cross(N, right);

    var irradiance = vec3f(0.0);
    let sample_delta = 0.05;
    var count: f32 = 0.0;

    var phi: f32 = 0.0;
    while (phi < 2.0 * PI) {
        var theta: f32 = 0.0;
        while (theta < 0.5 * PI) {
            let tangent_sample = vec3f(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            let sample_dir = tangent_sample.x * right + tangent_sample.y * up2 + tangent_sample.z * N;

            irradiance += textureSample(env_cubemap, env_sampler, sample_dir).rgb * cos(theta) * sin(theta);
            count += 1.0;
            theta += sample_delta;
        }
        phi += sample_delta;
    }

    irradiance = PI * irradiance / count;
    return vec4f(irradiance, 1.0);
}
)";

// =============================================================================
// Pre-filtered specular convolution shader
// =============================================================================

static const char* kPrefilterShader = R"(
struct FilterParams {
    face: u32,
    roughness_bits: u32,  // float bits via bitcast
    _pad0: u32,
    _pad1: u32,
}

@group(0) @binding(0) var env_sampler: sampler;
@group(0) @binding(1) var env_cubemap: texture_cube<f32>;
@group(0) @binding(2) var<uniform> params: FilterParams;

struct FullscreenOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

fn fullscreenTriangle(vertexIndex: u32) -> FullscreenOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: FullscreenOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    return out;
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> FullscreenOutput {
    return fullscreenTriangle(vid);
}

const PI: f32 = 3.14159265358979323846;

fn face_uv_to_dir(face: u32, uv: vec2f) -> vec3f {
    let u = uv.x * 2.0 - 1.0;
    let v = uv.y * 2.0 - 1.0;
    switch (face) {
        case 0u: { return normalize(vec3f( 1.0, -v, -u)); }
        case 1u: { return normalize(vec3f(-1.0, -v,  u)); }
        case 2u: { return normalize(vec3f( u,  1.0,  v)); }
        case 3u: { return normalize(vec3f( u, -1.0, -v)); }
        case 4u: { return normalize(vec3f( u,   -v, 1.0)); }
        default: { return normalize(vec3f(-u,   -v, -1.0)); }
    }
}

// Radical inverse (Van der Corput sequence)
fn radical_inverse_vdc(bits_in: u32) -> f32 {
    var bits = bits_in;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10;
}

fn hammersley(i: u32, N: u32) -> vec2f {
    return vec2f(f32(i) / f32(N), radical_inverse_vdc(i));
}

fn importance_sample_ggx(Xi: vec2f, N: vec3f, roughness: f32) -> vec3f {
    let a = roughness * roughness;

    let phi = 2.0 * PI * Xi.x;
    let cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    let sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    let H = vec3f(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);

    var up = vec3f(0.0, 1.0, 0.0);
    if (abs(N.y) > 0.999) { up = vec3f(1.0, 0.0, 0.0); }
    let tangent = normalize(cross(up, N));
    let bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

@fragment
fn fs_prefilter(in: FullscreenOutput) -> @location(0) vec4f {
    let roughness = bitcast<f32>(params.roughness_bits);
    let N = face_uv_to_dir(params.face, in.uv);
    let R = N;
    let V = R;

    var prefiltered = vec3f(0.0);
    var total_weight: f32 = 0.0;
    let sample_count = 1024u;

    for (var i: u32 = 0u; i < sample_count; i++) {
        let Xi = hammersley(i, sample_count);
        let H = importance_sample_ggx(Xi, N, roughness);
        let L = normalize(2.0 * dot(V, H) * H - V);

        let NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefiltered += textureSample(env_cubemap, env_sampler, L).rgb * NdotL;
            total_weight += NdotL;
        }
    }

    prefiltered /= max(total_weight, 0.001);
    return vec4f(prefiltered, 1.0);
}
)";

// =============================================================================
// BRDF Integration LUT shader
// =============================================================================

static const char* kBrdfLutShader = R"(
struct FullscreenOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

fn fullscreenTriangle(vertexIndex: u32) -> FullscreenOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: FullscreenOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    return out;
}

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> FullscreenOutput {
    return fullscreenTriangle(vid);
}

const PI: f32 = 3.14159265358979323846;

fn radical_inverse_vdc(bits_in: u32) -> f32 {
    var bits = bits_in;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10;
}

fn hammersley(i: u32, N: u32) -> vec2f {
    return vec2f(f32(i) / f32(N), radical_inverse_vdc(i));
}

fn importance_sample_ggx(Xi: vec2f, N: vec3f, roughness: f32) -> vec3f {
    let a = roughness * roughness;

    let phi = 2.0 * PI * Xi.x;
    let cos_theta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    let sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    let H = vec3f(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);

    var up = vec3f(0.0, 1.0, 0.0);
    if (abs(N.y) > 0.999) { up = vec3f(1.0, 0.0, 0.0); }
    let tangent = normalize(cross(up, N));
    let bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

fn geometry_schlick_ggx(NdotV: f32, roughness: f32) -> f32 {
    let a = roughness;
    let k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometry_smith(N: vec3f, V: vec3f, L: vec3f, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    return geometry_schlick_ggx(NdotV, roughness) * geometry_schlick_ggx(NdotL, roughness);
}

@fragment
fn fs_brdf_lut(in: FullscreenOutput) -> @location(0) vec2f {
    let NdotV = max(in.uv.x, 0.001);
    let roughness = in.uv.y;

    let V = vec3f(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    let N = vec3f(0.0, 0.0, 1.0);

    var A: f32 = 0.0;
    var B: f32 = 0.0;

    let sample_count = 1024u;
    for (var i: u32 = 0u; i < sample_count; i++) {
        let Xi = hammersley(i, sample_count);
        let H = importance_sample_ggx(Xi, N, roughness);
        let L = normalize(2.0 * dot(V, H) * H - V);

        let NdotL = max(L.z, 0.0);
        let NdotH = max(H.z, 0.0);
        let VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            let G = geometry_smith(N, V, L, roughness);
            let G_Vis = (G * VdotH) / (NdotH * NdotV);
            let Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    A /= f32(sample_count);
    B /= f32(sample_count);
    return vec2f(A, B);
}
)";

// =============================================================================
// Environment3D Operator
// =============================================================================

static constexpr uint32_t kCubeFaceSize = 128;
static constexpr uint32_t kIrradianceSize = 32;
static constexpr uint32_t kPrefilteredMips = 5;
static constexpr uint32_t kBrdfLutSize = 256;
static constexpr WGPUTextureFormat kHdrFormat = WGPUTextureFormat_RGBA16Float;
static constexpr WGPUTextureFormat kBrdfFormat = WGPUTextureFormat_RG16Float;

struct Environment3D : vivid::OperatorBase {
    static constexpr const char* kName   = "Environment3D";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> intensity {"intensity", 1.0f, 0.0f, 10.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(intensity, "Environment");
        out.push_back(&intensity);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"hdri",  VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"scene", VIVID_PORT_GPU_SCENE,   VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        // Check for input texture (equirectangular panorama)
        WGPUTextureView input_view = nullptr;
        if (gpu->input_texture_count > 0 && gpu->input_texture_views)
            input_view = gpu->input_texture_views[0];

        if (!input_view) {
            // No input — output empty environment fragment
            fragment_.fragment_type = vivid::gpu::VividSceneFragment::ENVIRONMENT;
            fragment_.ibl_irradiance  = nullptr;
            fragment_.ibl_prefiltered = nullptr;
            fragment_.ibl_brdf_lut    = nullptr;
            fragment_.ibl_sampler     = nullptr;
            fragment_.ibl_intensity   = 0.0f;
            gpu->output_scene = &fragment_;
            return;
        }

        // Lazy init GPU resources
        if (!initialized_ && !lazy_init(gpu)) {
            std::fprintf(stderr, "[environment3d] lazy_init FAILED\n");
            return;
        }

        // Re-compute IBL if input changed
        if (input_view != cached_input_view_) {
            cached_input_view_ = input_view;
            precompute_ibl(gpu, input_view);
        }

        // Output fragment
        fragment_.fragment_type   = vivid::gpu::VividSceneFragment::ENVIRONMENT;
        fragment_.ibl_irradiance  = irradiance_cube_view_;
        fragment_.ibl_prefiltered = prefiltered_cube_view_;
        fragment_.ibl_brdf_lut    = brdf_lut_view_;
        fragment_.ibl_sampler     = linear_clamp_sampler_;
        fragment_.ibl_intensity   = intensity.value;

        // No geometry
        fragment_.vertex_buffer   = nullptr;
        fragment_.vertex_buf_size = 0;
        fragment_.index_buffer    = nullptr;
        fragment_.index_count     = 0;
        fragment_.pipeline        = nullptr;
        fragment_.material_binds  = nullptr;
        fragment_.children        = nullptr;
        fragment_.child_count     = 0;

        gpu->output_scene = &fragment_;
    }

    ~Environment3D() override {
        vivid::gpu::release(equirect_shader_);
        vivid::gpu::release(irradiance_shader_);
        vivid::gpu::release(prefilter_shader_);
        vivid::gpu::release(brdf_shader_);
        vivid::gpu::release(equirect_pipeline_);
        vivid::gpu::release(irradiance_pipeline_);
        vivid::gpu::release(prefilter_pipeline_);
        vivid::gpu::release(brdf_pipeline_);
        vivid::gpu::release(equirect_pipe_layout_);
        vivid::gpu::release(cube_conv_pipe_layout_);
        vivid::gpu::release(brdf_pipe_layout_);
        vivid::gpu::release(equirect_bgl_);
        vivid::gpu::release(cube_conv_bgl_);
        vivid::gpu::release(brdf_bgl_);
        vivid::gpu::release(face_params_ubo_);
        vivid::gpu::release(linear_clamp_sampler_);
        vivid::gpu::release(unfiltered_cube_tex_);
        vivid::gpu::release(unfiltered_cube_view_);
        vivid::gpu::release(irradiance_cube_tex_);
        vivid::gpu::release(irradiance_cube_view_);
        vivid::gpu::release(prefiltered_cube_tex_);
        vivid::gpu::release(prefiltered_cube_view_);
        vivid::gpu::release(brdf_lut_tex_);
        vivid::gpu::release(brdf_lut_view_);
    }

private:
    vivid::gpu::VividSceneFragment fragment_{};
    bool initialized_ = false;
    WGPUTextureView cached_input_view_ = nullptr;

    // Shader modules
    WGPUShaderModule equirect_shader_   = nullptr;
    WGPUShaderModule irradiance_shader_ = nullptr;
    WGPUShaderModule prefilter_shader_  = nullptr;
    WGPUShaderModule brdf_shader_       = nullptr;

    // Pipelines
    WGPURenderPipeline equirect_pipeline_   = nullptr;
    WGPURenderPipeline irradiance_pipeline_ = nullptr;
    WGPURenderPipeline prefilter_pipeline_  = nullptr;
    WGPURenderPipeline brdf_pipeline_       = nullptr;

    // Pipeline layouts
    WGPUPipelineLayout equirect_pipe_layout_  = nullptr;
    WGPUPipelineLayout cube_conv_pipe_layout_ = nullptr;
    WGPUPipelineLayout brdf_pipe_layout_      = nullptr;

    // Bind group layouts
    WGPUBindGroupLayout equirect_bgl_  = nullptr;  // sampler + tex_2d + ubo
    WGPUBindGroupLayout cube_conv_bgl_ = nullptr;  // sampler + tex_cube + ubo
    WGPUBindGroupLayout brdf_bgl_      = nullptr;  // (empty — no bindings)

    // Resources
    WGPUBuffer  face_params_ubo_    = nullptr;
    WGPUSampler linear_clamp_sampler_ = nullptr;

    // Cubemaps
    WGPUTexture     unfiltered_cube_tex_  = nullptr;
    WGPUTextureView unfiltered_cube_view_ = nullptr;
    WGPUTexture     irradiance_cube_tex_  = nullptr;
    WGPUTextureView irradiance_cube_view_ = nullptr;
    WGPUTexture     prefiltered_cube_tex_  = nullptr;
    WGPUTextureView prefiltered_cube_view_ = nullptr;

    // BRDF LUT
    WGPUTexture     brdf_lut_tex_  = nullptr;
    WGPUTextureView brdf_lut_view_ = nullptr;

    bool lazy_init(VividGpuState* gpu) {
        // Sampler
        linear_clamp_sampler_ = vivid::gpu::create_clamp_linear_sampler(
            gpu->device, "Env3D Linear Clamp Sampler");

        // Face params UBO (16 bytes)
        face_params_ubo_ = vivid::gpu::create_uniform_buffer(
            gpu->device, 16, "Env3D Face Params UBO");

        // ---- Equirect → Cube bind group layout ----
        {
            WGPUBindGroupLayoutEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].texture.sampleType = WGPUTextureSampleType_Float;
            entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].buffer.type = WGPUBufferBindingType_Uniform;
            entries[2].buffer.minBindingSize = 16;

            WGPUBindGroupLayoutDescriptor desc{};
            desc.label = vivid_sv("Env3D Equirect BGL");
            desc.entryCount = 3;
            desc.entries = entries;
            equirect_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &desc);
        }

        // ---- Cube convolution bind group layout ----
        {
            WGPUBindGroupLayoutEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].visibility = WGPUShaderStage_Fragment;
            entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

            entries[1].binding = 1;
            entries[1].visibility = WGPUShaderStage_Fragment;
            entries[1].texture.sampleType = WGPUTextureSampleType_Float;
            entries[1].texture.viewDimension = WGPUTextureViewDimension_Cube;

            entries[2].binding = 2;
            entries[2].visibility = WGPUShaderStage_Fragment;
            entries[2].buffer.type = WGPUBufferBindingType_Uniform;
            entries[2].buffer.minBindingSize = 16;

            WGPUBindGroupLayoutDescriptor desc{};
            desc.label = vivid_sv("Env3D Cube Conv BGL");
            desc.entryCount = 3;
            desc.entries = entries;
            cube_conv_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &desc);
        }

        // ---- BRDF LUT bind group layout (empty — shader has no bindings) ----
        {
            WGPUBindGroupLayoutDescriptor desc{};
            desc.label = vivid_sv("Env3D BRDF BGL");
            desc.entryCount = 0;
            desc.entries = nullptr;
            brdf_bgl_ = wgpuDeviceCreateBindGroupLayout(gpu->device, &desc);
        }

        // ---- Pipeline layouts ----
        {
            WGPUPipelineLayoutDescriptor desc{};
            desc.label = vivid_sv("Env3D Equirect PL");
            desc.bindGroupLayoutCount = 1;
            desc.bindGroupLayouts = &equirect_bgl_;
            equirect_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &desc);
        }
        {
            WGPUPipelineLayoutDescriptor desc{};
            desc.label = vivid_sv("Env3D Cube Conv PL");
            desc.bindGroupLayoutCount = 1;
            desc.bindGroupLayouts = &cube_conv_bgl_;
            cube_conv_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &desc);
        }
        {
            WGPUPipelineLayoutDescriptor desc{};
            desc.label = vivid_sv("Env3D BRDF PL");
            desc.bindGroupLayoutCount = 1;
            desc.bindGroupLayouts = &brdf_bgl_;
            brdf_pipe_layout_ = wgpuDeviceCreatePipelineLayout(gpu->device, &desc);
        }

        // ---- Compile shaders ----
        equirect_shader_ = vivid::gpu::create_wgsl_shader(
            gpu->device, kEquirectToCubeShader, "Env3D Equirect Shader");
        if (!equirect_shader_) return false;

        irradiance_shader_ = vivid::gpu::create_wgsl_shader(
            gpu->device, kIrradianceShader, "Env3D Irradiance Shader");
        if (!irradiance_shader_) return false;

        prefilter_shader_ = vivid::gpu::create_wgsl_shader(
            gpu->device, kPrefilterShader, "Env3D Prefilter Shader");
        if (!prefilter_shader_) return false;

        brdf_shader_ = vivid::gpu::create_wgsl_shader(
            gpu->device, kBrdfLutShader, "Env3D BRDF LUT Shader");
        if (!brdf_shader_) return false;

        // ---- Create pipelines ----
        equirect_pipeline_ = create_fullscreen_pipeline(
            gpu->device, equirect_shader_, equirect_pipe_layout_,
            kHdrFormat, "fs_equirect_to_cube", "Env3D Equirect Pipeline");
        if (!equirect_pipeline_) return false;

        irradiance_pipeline_ = create_fullscreen_pipeline(
            gpu->device, irradiance_shader_, cube_conv_pipe_layout_,
            kHdrFormat, "fs_irradiance", "Env3D Irradiance Pipeline");
        if (!irradiance_pipeline_) return false;

        prefilter_pipeline_ = create_fullscreen_pipeline(
            gpu->device, prefilter_shader_, cube_conv_pipe_layout_,
            kHdrFormat, "fs_prefilter", "Env3D Prefilter Pipeline");
        if (!prefilter_pipeline_) return false;

        brdf_pipeline_ = create_fullscreen_pipeline(
            gpu->device, brdf_shader_, brdf_pipe_layout_,
            kBrdfFormat, "fs_brdf_lut", "Env3D BRDF LUT Pipeline");
        if (!brdf_pipeline_) return false;

        // ---- Create cubemap textures ----
        unfiltered_cube_tex_ = vivid::gpu::create_cubemap_texture(
            gpu->device, kCubeFaceSize, 1, kHdrFormat, "Env3D Unfiltered Cube");
        unfiltered_cube_view_ = vivid::gpu::create_cubemap_view(
            unfiltered_cube_tex_, kHdrFormat, 1, "Env3D Unfiltered Cube View");

        irradiance_cube_tex_ = vivid::gpu::create_cubemap_texture(
            gpu->device, kIrradianceSize, 1, kHdrFormat, "Env3D Irradiance Cube");
        irradiance_cube_view_ = vivid::gpu::create_cubemap_view(
            irradiance_cube_tex_, kHdrFormat, 1, "Env3D Irradiance Cube View");

        prefiltered_cube_tex_ = vivid::gpu::create_cubemap_texture(
            gpu->device, kCubeFaceSize, kPrefilteredMips, kHdrFormat, "Env3D Prefiltered Cube");
        prefiltered_cube_view_ = vivid::gpu::create_cubemap_view(
            prefiltered_cube_tex_, kHdrFormat, kPrefilteredMips, "Env3D Prefiltered Cube View");

        // ---- BRDF LUT texture ----
        {
            WGPUTextureDescriptor td{};
            td.label = vivid_sv("Env3D BRDF LUT");
            td.size = { kBrdfLutSize, kBrdfLutSize, 1 };
            td.mipLevelCount = 1;
            td.sampleCount = 1;
            td.dimension = WGPUTextureDimension_2D;
            td.format = kBrdfFormat;
            td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
            brdf_lut_tex_ = wgpuDeviceCreateTexture(gpu->device, &td);

            WGPUTextureViewDescriptor vd{};
            vd.label = vivid_sv("Env3D BRDF LUT View");
            vd.format = kBrdfFormat;
            vd.dimension = WGPUTextureViewDimension_2D;
            vd.mipLevelCount = 1;
            vd.arrayLayerCount = 1;
            brdf_lut_view_ = wgpuTextureCreateView(brdf_lut_tex_, &vd);
        }

        // ---- Compute BRDF LUT (one-time, input-independent) ----
        compute_brdf_lut(gpu);

        initialized_ = true;
        return true;
    }

    static WGPURenderPipeline create_fullscreen_pipeline(
            WGPUDevice device, WGPUShaderModule shader, WGPUPipelineLayout layout,
            WGPUTextureFormat format, const char* fs_entry, const char* label) {
        WGPUColorTargetState ct{};
        ct.format = format;
        ct.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState frag{};
        frag.module = shader;
        frag.entryPoint = vivid_sv(fs_entry);
        frag.targetCount = 1;
        frag.targets = &ct;

        WGPURenderPipelineDescriptor rp{};
        rp.label = vivid_sv(label);
        rp.layout = layout;
        rp.vertex.module = shader;
        rp.vertex.entryPoint = vivid_sv("vs_main");
        rp.vertex.bufferCount = 0;
        rp.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        rp.primitive.frontFace = WGPUFrontFace_CCW;
        rp.primitive.cullMode = WGPUCullMode_None;
        rp.multisample.count = 1;
        rp.multisample.mask = 0xFFFFFFFF;
        rp.fragment = &frag;

        return wgpuDeviceCreateRenderPipeline(device, &rp);
    }

    void render_face_pass(VividGpuState* gpu, WGPURenderPipeline pipeline,
                          WGPUBindGroup bg, WGPUTextureView target,
                          uint32_t size, const char* label) {
        WGPURenderPassColorAttachment ca{};
        ca.view = target;
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Clear;
        ca.storeOp = WGPUStoreOp_Store;
        ca.clearValue = {0, 0, 0, 1};

        WGPURenderPassDescriptor rp{};
        rp.label = vivid_sv(label);
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            gpu->command_encoder, &rp);
        wgpuRenderPassEncoderSetViewport(pass, 0, 0,
            static_cast<float>(size), static_cast<float>(size), 0, 1);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    void precompute_ibl(VividGpuState* gpu, WGPUTextureView input_view) {
        // Step 1: Equirect → Unfiltered cubemap
        for (uint32_t face = 0; face < 6; ++face) {
            uint32_t params[4] = { face, 0, 0, 0 };
            wgpuQueueWriteBuffer(gpu->queue, face_params_ubo_, 0, params, 16);

            // Create bind group for this pass
            WGPUBindGroupEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].sampler = linear_clamp_sampler_;
            entries[1].binding = 1;
            entries[1].textureView = input_view;
            entries[2].binding = 2;
            entries[2].buffer = face_params_ubo_;
            entries[2].offset = 0;
            entries[2].size = 16;

            WGPUBindGroupDescriptor bg_desc{};
            bg_desc.label = vivid_sv("Env3D Equirect BG");
            bg_desc.layout = equirect_bgl_;
            bg_desc.entryCount = 3;
            bg_desc.entries = entries;
            WGPUBindGroup bg = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

            WGPUTextureView face_view = vivid::gpu::create_cubemap_face_view(
                unfiltered_cube_tex_, kHdrFormat, face, 0, "Env3D Equirect Face");

            render_face_pass(gpu, equirect_pipeline_, bg, face_view, kCubeFaceSize,
                           "Env3D Equirect Pass");

            wgpuTextureViewRelease(face_view);
            wgpuBindGroupRelease(bg);
        }

        // Step 2: Irradiance convolution
        for (uint32_t face = 0; face < 6; ++face) {
            uint32_t params[4] = { face, 0, 0, 0 };
            wgpuQueueWriteBuffer(gpu->queue, face_params_ubo_, 0, params, 16);

            WGPUBindGroupEntry entries[3]{};
            entries[0].binding = 0;
            entries[0].sampler = linear_clamp_sampler_;
            entries[1].binding = 1;
            entries[1].textureView = unfiltered_cube_view_;
            entries[2].binding = 2;
            entries[2].buffer = face_params_ubo_;
            entries[2].offset = 0;
            entries[2].size = 16;

            WGPUBindGroupDescriptor bg_desc{};
            bg_desc.label = vivid_sv("Env3D Irradiance BG");
            bg_desc.layout = cube_conv_bgl_;
            bg_desc.entryCount = 3;
            bg_desc.entries = entries;
            WGPUBindGroup bg = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

            WGPUTextureView face_view = vivid::gpu::create_cubemap_face_view(
                irradiance_cube_tex_, kHdrFormat, face, 0, "Env3D Irradiance Face");

            render_face_pass(gpu, irradiance_pipeline_, bg, face_view, kIrradianceSize,
                           "Env3D Irradiance Pass");

            wgpuTextureViewRelease(face_view);
            wgpuBindGroupRelease(bg);
        }

        // Step 3: Pre-filtered specular (per mip level)
        float mip_roughness[kPrefilteredMips] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        for (uint32_t mip = 0; mip < kPrefilteredMips; ++mip) {
            uint32_t face_size = kCubeFaceSize >> mip;
            if (face_size < 1) face_size = 1;

            for (uint32_t face = 0; face < 6; ++face) {
                uint32_t roughness_bits;
                std::memcpy(&roughness_bits, &mip_roughness[mip], 4);
                uint32_t params[4] = { face, roughness_bits, 0, 0 };
                wgpuQueueWriteBuffer(gpu->queue, face_params_ubo_, 0, params, 16);

                WGPUBindGroupEntry entries[3]{};
                entries[0].binding = 0;
                entries[0].sampler = linear_clamp_sampler_;
                entries[1].binding = 1;
                entries[1].textureView = unfiltered_cube_view_;
                entries[2].binding = 2;
                entries[2].buffer = face_params_ubo_;
                entries[2].offset = 0;
                entries[2].size = 16;

                WGPUBindGroupDescriptor bg_desc{};
                bg_desc.label = vivid_sv("Env3D Prefilter BG");
                bg_desc.layout = cube_conv_bgl_;
                bg_desc.entryCount = 3;
                bg_desc.entries = entries;
                WGPUBindGroup bg = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

                WGPUTextureView face_view = vivid::gpu::create_cubemap_face_view(
                    prefiltered_cube_tex_, kHdrFormat, face, mip, "Env3D Prefilter Face");

                render_face_pass(gpu, prefilter_pipeline_, bg, face_view, face_size,
                               "Env3D Prefilter Pass");

                wgpuTextureViewRelease(face_view);
                wgpuBindGroupRelease(bg);
            }
        }
    }

    void compute_brdf_lut(VividGpuState* gpu) {
        // Empty bind group (BRDF LUT has no external inputs)
        WGPUBindGroupDescriptor bg_desc{};
        bg_desc.label = vivid_sv("Env3D BRDF BG");
        bg_desc.layout = brdf_bgl_;
        bg_desc.entryCount = 0;
        bg_desc.entries = nullptr;
        WGPUBindGroup bg = wgpuDeviceCreateBindGroup(gpu->device, &bg_desc);

        WGPURenderPassColorAttachment ca{};
        ca.view = brdf_lut_view_;
        ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Clear;
        ca.storeOp = WGPUStoreOp_Store;
        ca.clearValue = {0, 0, 0, 1};

        WGPURenderPassDescriptor rp{};
        rp.label = vivid_sv("Env3D BRDF LUT Pass");
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &ca;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
            gpu->command_encoder, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, brdf_pipeline_);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        wgpuBindGroupRelease(bg);
    }
};

VIVID_REGISTER(Environment3D)
