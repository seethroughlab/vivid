#include "cubemap.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <glm/gtc/packing.hpp>

// stb_image for HDR loading (implementation is in image_loader.cpp)
#include "stb_image.h"

namespace vivid {

// ============================================================================
// WGSL Compute Shaders for IBL Processing
// ============================================================================

static const char* EQUIRECT_TO_CUBEMAP_SHADER = R"(
@group(0) @binding(0) var equirectMap: texture_2d<f32>;
@group(0) @binding(1) var equirectSampler: sampler;
@group(0) @binding(2) var outputCube: texture_storage_2d_array<rgba16float, write>;

const PI: f32 = 3.14159265359;

// Convert cubemap face + UV to 3D direction
fn getCubeDirection(face: u32, uv: vec2f) -> vec3f {
    let u = uv.x * 2.0 - 1.0;
    let v = uv.y * 2.0 - 1.0;

    switch face {
        case 0u: { return normalize(vec3f( 1.0,   -v,   -u)); }  // +X
        case 1u: { return normalize(vec3f(-1.0,   -v,    u)); }  // -X
        case 2u: { return normalize(vec3f(   u,  1.0,    v)); }  // +Y
        case 3u: { return normalize(vec3f(   u, -1.0,   -v)); }  // -Y
        case 4u: { return normalize(vec3f(   u,   -v,  1.0)); }  // +Z
        default: { return normalize(vec3f(  -u,   -v, -1.0)); }  // -Z
    }
}

// Convert 3D direction to equirectangular UV
fn dirToEquirectUV(dir: vec3f) -> vec2f {
    let phi = atan2(dir.z, dir.x);
    let theta = asin(clamp(dir.y, -1.0, 1.0));
    return vec2f(
        phi / (2.0 * PI) + 0.5,
        theta / PI + 0.5
    );
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(outputCube).x;
    if (id.x >= size || id.y >= size || id.z >= 6u) {
        return;
    }

    let uv = (vec2f(id.xy) + 0.5) / f32(size);
    let dir = getCubeDirection(id.z, uv);
    let equirectUV = dirToEquirectUV(dir);
    let color = textureSampleLevel(equirectMap, equirectSampler, equirectUV, 0.0);

    textureStore(outputCube, vec2i(id.xy), i32(id.z), color);
}
)";

static const char* IRRADIANCE_SHADER = R"(
@group(0) @binding(0) var envCube: texture_cube<f32>;
@group(0) @binding(1) var envSampler: sampler;
@group(0) @binding(2) var outputCube: texture_storage_2d_array<rgba16float, write>;

const PI: f32 = 3.14159265359;
const SAMPLE_DELTA: f32 = 0.025;

fn getCubeDirection(face: u32, uv: vec2f) -> vec3f {
    let u = uv.x * 2.0 - 1.0;
    let v = uv.y * 2.0 - 1.0;

    switch face {
        case 0u: { return normalize(vec3f( 1.0,   -v,   -u)); }
        case 1u: { return normalize(vec3f(-1.0,   -v,    u)); }
        case 2u: { return normalize(vec3f(   u,  1.0,    v)); }
        case 3u: { return normalize(vec3f(   u, -1.0,   -v)); }
        case 4u: { return normalize(vec3f(   u,   -v,  1.0)); }
        default: { return normalize(vec3f(  -u,   -v, -1.0)); }
    }
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(outputCube).x;
    if (id.x >= size || id.y >= size || id.z >= 6u) {
        return;
    }

    let uv = (vec2f(id.xy) + 0.5) / f32(size);
    let N = getCubeDirection(id.z, uv);

    // Create tangent space basis
    var up = vec3f(0.0, 1.0, 0.0);
    if (abs(N.y) > 0.999) {
        up = vec3f(0.0, 0.0, 1.0);
    }
    let right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    // Convolve over hemisphere
    var irradiance = vec3f(0.0);
    var nrSamples = 0.0;

    for (var phi: f32 = 0.0; phi < 2.0 * PI; phi += SAMPLE_DELTA) {
        for (var theta: f32 = 0.0; theta < 0.5 * PI; theta += SAMPLE_DELTA) {
            // Spherical to Cartesian (in tangent space)
            let tangentSample = vec3f(
                sin(theta) * cos(phi),
                sin(theta) * sin(phi),
                cos(theta)
            );
            // Tangent space to world
            let sampleDir = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N;

            let sampleColor = textureSampleLevel(envCube, envSampler, sampleDir, 0.0).rgb;
            irradiance += sampleColor * cos(theta) * sin(theta);
            nrSamples += 1.0;
        }
    }

    irradiance = PI * irradiance / nrSamples;
    textureStore(outputCube, vec2i(id.xy), i32(id.z), vec4f(irradiance, 1.0));
}
)";

static const char* RADIANCE_SHADER = R"(
struct Params {
    roughness: f32,
    resolution: f32,
    _pad0: f32,
    _pad1: f32,
}

@group(0) @binding(0) var envCube: texture_cube<f32>;
@group(0) @binding(1) var envSampler: sampler;
@group(0) @binding(2) var outputCube: texture_storage_2d_array<rgba16float, write>;
@group(0) @binding(3) var<uniform> params: Params;

const PI: f32 = 3.14159265359;
const SAMPLE_COUNT: u32 = 1024u;

fn getCubeDirection(face: u32, uv: vec2f) -> vec3f {
    let u = uv.x * 2.0 - 1.0;
    let v = uv.y * 2.0 - 1.0;

    switch face {
        case 0u: { return normalize(vec3f( 1.0,   -v,   -u)); }
        case 1u: { return normalize(vec3f(-1.0,   -v,    u)); }
        case 2u: { return normalize(vec3f(   u,  1.0,    v)); }
        case 3u: { return normalize(vec3f(   u, -1.0,   -v)); }
        case 4u: { return normalize(vec3f(   u,   -v,  1.0)); }
        default: { return normalize(vec3f(  -u,   -v, -1.0)); }
    }
}

fn radicalInverse_VdC(bits_in: u32) -> f32 {
    var bits = bits_in;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10;
}

fn hammersley(i: u32, N: u32) -> vec2f {
    return vec2f(f32(i) / f32(N), radicalInverse_VdC(i));
}

fn importanceSampleGGX(Xi: vec2f, N: vec3f, roughness: f32) -> vec3f {
    let a = roughness * roughness;

    let phi = 2.0 * PI * Xi.x;
    let cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    let sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to Cartesian
    let H = vec3f(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );

    // Tangent space to world
    var up = vec3f(0.0, 1.0, 0.0);
    if (abs(N.y) > 0.999) {
        up = vec3f(0.0, 0.0, 1.0);
    }
    let tangent = normalize(cross(up, N));
    let bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let size = u32(params.resolution);
    if (id.x >= size || id.y >= size || id.z >= 6u) {
        return;
    }

    let roughness = params.roughness;
    let uv = (vec2f(id.xy) + 0.5) / f32(size);
    let N = getCubeDirection(id.z, uv);
    let R = N;
    let V = R;

    var prefilteredColor = vec3f(0.0);
    var totalWeight = 0.0;

    for (var i: u32 = 0u; i < SAMPLE_COUNT; i++) {
        let Xi = hammersley(i, SAMPLE_COUNT);
        let H = importanceSampleGGX(Xi, N, roughness);
        let L = normalize(2.0 * dot(V, H) * H - V);

        let NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            prefilteredColor += textureSampleLevel(envCube, envSampler, L, 0.0).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefilteredColor = prefilteredColor / totalWeight;
    textureStore(outputCube, vec2i(id.xy), i32(id.z), vec4f(prefilteredColor, 1.0));
}
)";

static const char* BRDF_LUT_SHADER = R"(
@group(0) @binding(0) var outputLUT: texture_storage_2d<rg32float, write>;

const PI: f32 = 3.14159265359;
const SAMPLE_COUNT: u32 = 1024u;

fn radicalInverse_VdC(bits_in: u32) -> f32 {
    var bits = bits_in;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10;
}

fn hammersley(i: u32, N: u32) -> vec2f {
    return vec2f(f32(i) / f32(N), radicalInverse_VdC(i));
}

fn importanceSampleGGX(Xi: vec2f, N: vec3f, roughness: f32) -> vec3f {
    let a = roughness * roughness;

    let phi = 2.0 * PI * Xi.x;
    let cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    let sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    let H = vec3f(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );

    var up = vec3f(0.0, 1.0, 0.0);
    if (abs(N.y) > 0.999) {
        up = vec3f(0.0, 0.0, 1.0);
    }
    let tangent = normalize(cross(up, N));
    let bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let a = roughness;
    let k = (a * a) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometrySmith(N: vec3f, V: vec3f, L: vec3f, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    let ggx1 = geometrySchlickGGX(NdotV, roughness);
    let ggx2 = geometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

fn integrateBRDF(NdotV: f32, roughness: f32) -> vec2f {
    let V = vec3f(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    let N = vec3f(0.0, 0.0, 1.0);

    var A = 0.0;
    var B = 0.0;

    for (var i: u32 = 0u; i < SAMPLE_COUNT; i++) {
        let Xi = hammersley(i, SAMPLE_COUNT);
        let H = importanceSampleGGX(Xi, N, roughness);
        let L = normalize(2.0 * dot(V, H) * H - V);

        let NdotL = max(L.z, 0.0);
        let NdotH = max(H.z, 0.0);
        let VdotH = max(dot(V, H), 0.0);

        if (NdotL > 0.0) {
            let G = geometrySmith(N, V, L, roughness);
            let G_Vis = (G * VdotH) / (NdotH * NdotV);
            let Fc = pow(1.0 - VdotH, 5.0);

            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    return vec2f(A, B) / f32(SAMPLE_COUNT);
}

@compute @workgroup_size(16, 16, 1)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let size = textureDimensions(outputLUT);
    if (id.x >= size.x || id.y >= size.y) {
        return;
    }

    let uv = (vec2f(id.xy) + 0.5) / vec2f(size);
    let NdotV = uv.x;
    let roughness = uv.y;

    let result = integrateBRDF(max(NdotV, 0.001), max(roughness, 0.001));
    textureStore(outputLUT, vec2i(id.xy), vec4f(result, 0.0, 1.0));
}
)";

// ============================================================================
// CubemapProcessor Implementation
// ============================================================================

CubemapProcessor::~CubemapProcessor() {
    destroy();
}

bool CubemapProcessor::init(Renderer& renderer) {
    renderer_ = &renderer;
    return createPipelines();
}

void CubemapProcessor::destroy() {
    destroyPipelines();

    if (brdfLUT_) {
        wgpuTextureViewRelease(brdfLUTView_);
        wgpuTextureDestroy(brdfLUT_);
        brdfLUT_ = nullptr;
        brdfLUTView_ = nullptr;
    }

    renderer_ = nullptr;
}

// Helper to create a shader module from WGSL source
static WGPUShaderModule createShaderModule(WGPUDevice device, const char* source) {
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{.data = source, .length = strlen(source)};

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);

    return wgpuDeviceCreateShaderModule(device, &moduleDesc);
}

bool CubemapProcessor::createPipelines() {
    if (!renderer_) return false;

    WGPUDevice device = renderer_->device();

    // Create sampler for cubemap processing
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    cubemapSampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);

    // -------------------------------------------------------------------------
    // BRDF LUT Pipeline - Single output texture
    // -------------------------------------------------------------------------
    {
        WGPUShaderModule module = createShaderModule(device, BRDF_LUT_SHADER);
        if (!module) {
            std::cerr << "Failed to create BRDF LUT shader module\n";
            return false;
        }

        // Bind group layout: @binding(0) = storage texture (write)
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Compute;
        entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entry.storageTexture.format = WGPUTextureFormat_RG32Float;
        entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        brdfLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &brdfLayout_;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        // Compute pipeline
        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = WGPUStringView{.data = "main", .length = 4};
        brdfPipeline_ = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    // -------------------------------------------------------------------------
    // Equirectangular to Cubemap Pipeline
    // -------------------------------------------------------------------------
    {
        WGPUShaderModule module = createShaderModule(device, EQUIRECT_TO_CUBEMAP_SHADER);
        if (!module) {
            std::cerr << "Failed to create equirect shader module\n";
            return false;
        }

        // Bind group layout:
        // @binding(0) = input texture 2D
        // @binding(1) = sampler
        // @binding(2) = output cubemap (storage, 2D array)
        WGPUBindGroupLayoutEntry entries[3] = {};

        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].texture.sampleType = WGPUTextureSampleType_Float;  // RGBA16Float is filterable
        entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entries[2].storageTexture.format = WGPUTextureFormat_RGBA16Float;
        entries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 3;
        layoutDesc.entries = entries;
        equirectLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &equirectLayout_;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = WGPUStringView{.data = "main", .length = 4};
        equirectPipeline_ = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    // -------------------------------------------------------------------------
    // Irradiance Convolution Pipeline
    // -------------------------------------------------------------------------
    {
        WGPUShaderModule module = createShaderModule(device, IRRADIANCE_SHADER);
        if (!module) {
            std::cerr << "Failed to create irradiance shader module\n";
            return false;
        }

        // Bind group layout:
        // @binding(0) = input cubemap
        // @binding(1) = sampler
        // @binding(2) = output cubemap (storage)
        WGPUBindGroupLayoutEntry entries[3] = {};

        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].texture.sampleType = WGPUTextureSampleType_Float;
        entries[0].texture.viewDimension = WGPUTextureViewDimension_Cube;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entries[2].storageTexture.format = WGPUTextureFormat_RGBA16Float;
        entries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 3;
        layoutDesc.entries = entries;
        irradianceLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &irradianceLayout_;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = WGPUStringView{.data = "main", .length = 4};
        irradiancePipeline_ = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    // -------------------------------------------------------------------------
    // Radiance Prefilter Pipeline (with uniform buffer for roughness)
    // -------------------------------------------------------------------------
    {
        WGPUShaderModule module = createShaderModule(device, RADIANCE_SHADER);
        if (!module) {
            std::cerr << "Failed to create radiance shader module\n";
            return false;
        }

        // Bind group layout:
        // @binding(0) = input cubemap
        // @binding(1) = sampler
        // @binding(2) = output cubemap (storage)
        // @binding(3) = params uniform
        WGPUBindGroupLayoutEntry entries[4] = {};

        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].texture.sampleType = WGPUTextureSampleType_Float;
        entries[0].texture.viewDimension = WGPUTextureViewDimension_Cube;

        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Compute;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Compute;
        entries[2].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entries[2].storageTexture.format = WGPUTextureFormat_RGBA16Float;
        entries[2].storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;

        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Compute;
        entries[3].buffer.type = WGPUBufferBindingType_Uniform;
        entries[3].buffer.minBindingSize = 16;  // 4 floats

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 4;
        layoutDesc.entries = entries;
        radianceLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &radianceLayout_;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = WGPUStringView{.data = "main", .length = 4};
        radiancePipeline_ = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    return true;
}

void CubemapProcessor::destroyPipelines() {
    if (cubemapSampler_) {
        wgpuSamplerRelease(cubemapSampler_);
        cubemapSampler_ = nullptr;
    }

    // Release pipelines
    if (equirectPipeline_) wgpuComputePipelineRelease(equirectPipeline_);
    if (irradiancePipeline_) wgpuComputePipelineRelease(irradiancePipeline_);
    if (radiancePipeline_) wgpuComputePipelineRelease(radiancePipeline_);
    if (brdfPipeline_) wgpuComputePipelineRelease(brdfPipeline_);

    equirectPipeline_ = nullptr;
    irradiancePipeline_ = nullptr;
    radiancePipeline_ = nullptr;
    brdfPipeline_ = nullptr;

    // Release bind group layouts
    if (equirectLayout_) wgpuBindGroupLayoutRelease(equirectLayout_);
    if (irradianceLayout_) wgpuBindGroupLayoutRelease(irradianceLayout_);
    if (radianceLayout_) wgpuBindGroupLayoutRelease(radianceLayout_);
    if (brdfLayout_) wgpuBindGroupLayoutRelease(brdfLayout_);

    equirectLayout_ = nullptr;
    irradianceLayout_ = nullptr;
    radianceLayout_ = nullptr;
    brdfLayout_ = nullptr;
}

Cubemap CubemapProcessor::createCubemap(int size, int mipLevels, bool hdr) {
    if (!renderer_) return {};

    WGPUDevice device = renderer_->device();

    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = size;
    texDesc.size.height = size;
    texDesc.size.depthOrArrayLayers = 6;  // 6 faces
    texDesc.mipLevelCount = mipLevels;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = hdr ? WGPUTextureFormat_RGBA16Float : WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst;

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &texDesc);
    if (!texture) {
        std::cerr << "Failed to create cubemap texture\n";
        return {};
    }

    // Create cube view (for sampling)
    WGPUTextureViewDescriptor cubeViewDesc = {};
    cubeViewDesc.format = texDesc.format;
    cubeViewDesc.dimension = WGPUTextureViewDimension_Cube;
    cubeViewDesc.baseMipLevel = 0;
    cubeViewDesc.mipLevelCount = mipLevels;
    cubeViewDesc.baseArrayLayer = 0;
    cubeViewDesc.arrayLayerCount = 6;
    WGPUTextureView cubeView = wgpuTextureCreateView(texture, &cubeViewDesc);

    // Create face views (for rendering to individual faces)
    CubemapData* data = new CubemapData();
    data->texture = texture;
    data->view = cubeView;
    data->size = size;
    data->mipLevels = mipLevels;

    for (int i = 0; i < 6; i++) {
        WGPUTextureViewDescriptor faceViewDesc = {};
        faceViewDesc.format = texDesc.format;
        faceViewDesc.dimension = WGPUTextureViewDimension_2D;
        faceViewDesc.baseMipLevel = 0;
        faceViewDesc.mipLevelCount = 1;
        faceViewDesc.baseArrayLayer = i;
        faceViewDesc.arrayLayerCount = 1;
        data->faceViews[i] = wgpuTextureCreateView(texture, &faceViewDesc);
    }

    Cubemap result;
    result.handle = data;
    result.size = size;
    result.mipLevels = mipLevels;
    return result;
}

void CubemapProcessor::destroyCubemap(Cubemap& cubemap) {
    if (!cubemap.handle) return;

    CubemapData* data = getCubemapData(cubemap);

    for (int i = 0; i < 6; i++) {
        if (data->faceViews[i]) {
            wgpuTextureViewRelease(data->faceViews[i]);
        }
    }
    if (data->view) wgpuTextureViewRelease(data->view);
    if (data->texture) wgpuTextureDestroy(data->texture);

    delete data;
    cubemap.handle = nullptr;
    cubemap.size = 0;
    cubemap.mipLevels = 0;
}

Environment CubemapProcessor::loadEnvironment(const std::string& path) {
    Environment env;

    if (!renderer_) {
        std::cerr << "CubemapProcessor not initialized\n";
        return env;
    }

    // Load file into memory first (matching image_loader pattern since STBI_NO_STDIO is used)
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        std::cerr << "Failed to open HDR file: " << path << "\n";
        return env;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> fileData(fileSize);
    fread(fileData.data(), 1, fileSize, f);
    fclose(f);

    // Load HDR image from memory
    int width, height, channels;
    float* hdrData = stbi_loadf_from_memory(fileData.data(), static_cast<int>(fileSize), &width, &height, &channels, 3);
    if (!hdrData) {
        std::cerr << "Failed to decode HDR image: " << path << "\n";
        return env;
    }

    std::cout << "Loaded HDR: " << width << "x" << height << "\n";

    // Convert equirectangular to cubemap
    Cubemap envCubemap = equirectangularToCubemap(hdrData, width, height, 512);
    stbi_image_free(hdrData);

    if (!envCubemap.valid()) {
        std::cerr << "Failed to convert equirectangular to cubemap\n";
        return env;
    }

    // Compute irradiance map (diffuse IBL)
    env.irradianceMap = computeIrradiance(envCubemap, 64);

    // Compute radiance map (specular IBL)
    env.radianceMap = computeRadiance(envCubemap, 256, 5);

    // Get BRDF LUT
    env.brdfLUT = getBRDFLUT(256);

    // Clean up source cubemap (we only need the processed maps)
    destroyCubemap(envCubemap);

    if (env.valid()) {
        std::cout << "IBL environment loaded successfully\n";
    }

    return env;
}

Cubemap CubemapProcessor::equirectangularToCubemap(const float* hdrPixels, int width, int height, int cubemapSize) {
    if (!renderer_ || !hdrPixels || !equirectPipeline_) return {};

    // Create output cubemap
    Cubemap output = createCubemap(cubemapSize, 1, true);
    if (!output.valid()) return {};

    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();
    CubemapData* outputData = getCubemapData(output);

    // Upload HDR data to a 2D texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = width;
    texDesc.size.height = height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA16Float;  // Use 16-bit float (filterable)
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    WGPUTexture hdrTexture = wgpuDeviceCreateTexture(device, &texDesc);

    // Convert RGB float32 to RGBA float16 and upload
    // glm has a packHalf function but we'll use a simple conversion
    std::vector<uint16_t> rgbaData(width * height * 4);
    for (int i = 0; i < width * height; i++) {
        rgbaData[i * 4 + 0] = glm::packHalf1x16(hdrPixels[i * 3 + 0]);
        rgbaData[i * 4 + 1] = glm::packHalf1x16(hdrPixels[i * 3 + 1]);
        rgbaData[i * 4 + 2] = glm::packHalf1x16(hdrPixels[i * 3 + 2]);
        rgbaData[i * 4 + 3] = glm::packHalf1x16(1.0f);
    }

    WGPUTexelCopyTextureInfo dest = {};
    dest.texture = hdrTexture;
    dest.mipLevel = 0;

    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = width * 4 * sizeof(uint16_t);
    layout.rowsPerImage = height;

    WGPUExtent3D copySize = {(uint32_t)width, (uint32_t)height, 1};
    wgpuQueueWriteTexture(queue, &dest, rgbaData.data(), rgbaData.size() * sizeof(uint16_t), &layout, &copySize);

    // Create texture view for sampling the HDR input
    WGPUTextureViewDescriptor hdrViewDesc = {};
    hdrViewDesc.format = WGPUTextureFormat_RGBA16Float;
    hdrViewDesc.dimension = WGPUTextureViewDimension_2D;
    hdrViewDesc.mipLevelCount = 1;
    hdrViewDesc.arrayLayerCount = 1;
    WGPUTextureView hdrView = wgpuTextureCreateView(hdrTexture, &hdrViewDesc);

    // Create 2D array view for storage output (all 6 faces)
    WGPUTextureViewDescriptor outputViewDesc = {};
    outputViewDesc.format = WGPUTextureFormat_RGBA16Float;
    outputViewDesc.dimension = WGPUTextureViewDimension_2DArray;
    outputViewDesc.baseMipLevel = 0;
    outputViewDesc.mipLevelCount = 1;
    outputViewDesc.baseArrayLayer = 0;
    outputViewDesc.arrayLayerCount = 6;
    WGPUTextureView outputView = wgpuTextureCreateView(outputData->texture, &outputViewDesc);

    // Create bind group
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].textureView = hdrView;
    entries[1].binding = 1;
    entries[1].sampler = cubemapSampler_;
    entries[2].binding = 2;
    entries[2].textureView = outputView;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = equirectLayout_;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);

    // Create command encoder and compute pass
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, equirectPipeline_);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

    // Dispatch - workgroup size is 16x16, dispatch for all 6 faces
    uint32_t workgroupsX = (cubemapSize + 15) / 16;
    uint32_t workgroupsY = (cubemapSize + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 6);

    wgpuComputePassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);

    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
    wgpuTextureViewRelease(outputView);
    wgpuTextureViewRelease(hdrView);
    wgpuTextureDestroy(hdrTexture);

    std::cout << "Converted equirectangular to cubemap (" << cubemapSize << "x" << cubemapSize << ")\n";
    return output;
}

Cubemap CubemapProcessor::computeIrradiance(const Cubemap& envCubemap, int size) {
    if (!renderer_ || !envCubemap.valid() || !irradiancePipeline_) return {};

    // Create output irradiance cubemap
    Cubemap output = createCubemap(size, 1, true);
    if (!output.valid()) return {};

    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();
    CubemapData* inputData = getCubemapData(envCubemap);
    CubemapData* outputData = getCubemapData(output);

    // Create 2D array view for storage output (all 6 faces)
    WGPUTextureViewDescriptor outputViewDesc = {};
    outputViewDesc.format = WGPUTextureFormat_RGBA16Float;
    outputViewDesc.dimension = WGPUTextureViewDimension_2DArray;
    outputViewDesc.baseMipLevel = 0;
    outputViewDesc.mipLevelCount = 1;
    outputViewDesc.baseArrayLayer = 0;
    outputViewDesc.arrayLayerCount = 6;
    WGPUTextureView outputView = wgpuTextureCreateView(outputData->texture, &outputViewDesc);

    // Create bind group - input cube view already exists in inputData
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].textureView = inputData->view;  // Cube view for sampling
    entries[1].binding = 1;
    entries[1].sampler = cubemapSampler_;
    entries[2].binding = 2;
    entries[2].textureView = outputView;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = irradianceLayout_;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);

    // Create command encoder and compute pass
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, irradiancePipeline_);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

    // Dispatch - workgroup size is 16x16, dispatch for all 6 faces
    uint32_t workgroupsX = (size + 15) / 16;
    uint32_t workgroupsY = (size + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 6);

    wgpuComputePassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);

    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
    wgpuTextureViewRelease(outputView);

    std::cout << "Computed irradiance map (" << size << "x" << size << ")\n";
    return output;
}

Cubemap CubemapProcessor::computeRadiance(const Cubemap& envCubemap, int size, int mipLevels) {
    if (!renderer_ || !envCubemap.valid() || !radiancePipeline_) return {};

    // Create output radiance cubemap with mip levels
    Cubemap output = createCubemap(size, mipLevels, true);
    if (!output.valid()) return {};

    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();
    CubemapData* inputData = getCubemapData(envCubemap);
    CubemapData* outputData = getCubemapData(output);

    // Create uniform buffer for params (roughness, resolution, pad, pad)
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 16;  // 4 floats
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer paramsBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);

    // Process each mip level with increasing roughness
    for (int mip = 0; mip < mipLevels; mip++) {
        int mipSize = size >> mip;
        float roughness = (float)mip / (float)(mipLevels - 1);

        // Update params buffer
        float params[4] = { roughness, (float)mipSize, 0.0f, 0.0f };
        wgpuQueueWriteBuffer(queue, paramsBuffer, 0, params, sizeof(params));

        // Create 2D array view for this mip level
        WGPUTextureViewDescriptor outputViewDesc = {};
        outputViewDesc.format = WGPUTextureFormat_RGBA16Float;
        outputViewDesc.dimension = WGPUTextureViewDimension_2DArray;
        outputViewDesc.baseMipLevel = mip;
        outputViewDesc.mipLevelCount = 1;
        outputViewDesc.baseArrayLayer = 0;
        outputViewDesc.arrayLayerCount = 6;
        WGPUTextureView outputView = wgpuTextureCreateView(outputData->texture, &outputViewDesc);

        // Create bind group
        WGPUBindGroupEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].textureView = inputData->view;  // Cube view for sampling
        entries[1].binding = 1;
        entries[1].sampler = cubemapSampler_;
        entries[2].binding = 2;
        entries[2].textureView = outputView;
        entries[3].binding = 3;
        entries[3].buffer = paramsBuffer;
        entries[3].size = 16;

        WGPUBindGroupDescriptor bindGroupDesc = {};
        bindGroupDesc.layout = radianceLayout_;
        bindGroupDesc.entryCount = 4;
        bindGroupDesc.entries = entries;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);

        // Create command encoder and compute pass
        WGPUCommandEncoderDescriptor encDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

        WGPUComputePassDescriptor passDesc = {};
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

        wgpuComputePassEncoderSetPipeline(pass, radiancePipeline_);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

        // Dispatch - workgroup size is 16x16, dispatch for all 6 faces
        uint32_t workgroupsX = (mipSize + 15) / 16;
        uint32_t workgroupsY = (mipSize + 15) / 16;
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 6);

        wgpuComputePassEncoderEnd(pass);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(queue, 1, &cmdBuffer);

        // Cleanup per-mip resources
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bindGroup);
        wgpuTextureViewRelease(outputView);
    }

    // Cleanup shared resources
    wgpuBufferDestroy(paramsBuffer);
    wgpuBufferRelease(paramsBuffer);

    std::cout << "Computed radiance map (" << size << "x" << size << ", " << mipLevels << " mips)\n";
    return output;
}

bool CubemapProcessor::createBRDFLUT(int size) {
    if (!renderer_ || !brdfPipeline_) return false;

    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();

    // Create BRDF LUT texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = size;
    texDesc.size.height = size;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RG32Float;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;

    brdfLUT_ = wgpuDeviceCreateTexture(device, &texDesc);
    if (!brdfLUT_) return false;

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = texDesc.format;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    brdfLUTView_ = wgpuTextureCreateView(brdfLUT_, &viewDesc);

    brdfLUTSize_ = size;

    // Create bind group
    WGPUBindGroupEntry entry = {};
    entry.binding = 0;
    entry.textureView = brdfLUTView_;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = brdfLayout_;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &entry;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindGroupDesc);

    // Create command encoder and compute pass
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, brdfPipeline_);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

    // Dispatch - workgroup size is 16x16, so divide texture size by 16
    uint32_t workgroupsX = (size + 15) / 16;
    uint32_t workgroupsY = (size + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

    wgpuComputePassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);

    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);

    std::cout << "Generated BRDF LUT (" << size << "x" << size << ")\n";
    return true;
}

void* CubemapProcessor::getBRDFLUT(int size) {
    if (!brdfLUT_ || brdfLUTSize_ != size) {
        if (brdfLUT_) {
            wgpuTextureViewRelease(brdfLUTView_);
            wgpuTextureDestroy(brdfLUT_);
        }
        createBRDFLUT(size);
    }
    return brdfLUTView_;
}

} // namespace vivid
