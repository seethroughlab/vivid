// IBL (Image-Based Lighting) Implementation
// Ported from vivid_v1/runtime/src/cubemap.cpp

#include <vivid/render3d/ibl_environment.h>
#include <vivid/io/image_loader.h>
#include <vivid/context.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
#include <glm/gtc/packing.hpp>

namespace vivid::render3d {

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
        0.5 - theta / PI  // Flip V so top of image = looking up
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
@group(0) @binding(0) var outputLUT: texture_storage_2d<rgba16float, write>;

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
// Helper to create a shader module
// ============================================================================

static WGPUShaderModule createShaderModule(WGPUDevice device, const char* source) {
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{source, strlen(source)};

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);

    return wgpuDeviceCreateShaderModule(device, &moduleDesc);
}

// Helper for string views
static WGPUStringView toStringView(const char* str) {
    return WGPUStringView{str, strlen(str)};
}

// ============================================================================
// IBLEnvironment Implementation
// ============================================================================

IBLEnvironment::IBLEnvironment() = default;

IBLEnvironment::~IBLEnvironment() {
    cleanup();
}

bool IBLEnvironment::init(Context& ctx) {
    if (m_initialized) return true;

    m_device = ctx.device();
    m_queue = ctx.queue();

    if (!createPipelines()) {
        std::cerr << "IBLEnvironment: Failed to create compute pipelines\n";
        return false;
    }

    m_initialized = true;
    std::cout << "IBLEnvironment: Initialized\n";
    return true;
}

void IBLEnvironment::cleanup() {
    destroyCubemap(m_irradianceMap);
    destroyCubemap(m_prefilteredMap);

    if (m_brdfLUTView) {
        wgpuTextureViewRelease(m_brdfLUTView);
        m_brdfLUTView = nullptr;
    }
    if (m_brdfLUT) {
        wgpuTextureDestroy(m_brdfLUT);
        m_brdfLUT = nullptr;
    }

    destroyPipelines();

    m_device = nullptr;
    m_queue = nullptr;
    m_initialized = false;
}

bool IBLEnvironment::loadHDR(Context& ctx, const std::string& hdrPath) {
    if (!m_initialized && !init(ctx)) {
        return false;
    }

    // Load HDR image via vivid-io
    auto hdrImage = vivid::io::loadImageHDR(hdrPath);
    if (!hdrImage.valid()) {
        std::cerr << "IBLEnvironment: Failed to load HDR file: " << hdrPath << "\n";
        return false;
    }

    int width = hdrImage.width;
    int height = hdrImage.height;
    const float* hdrData = hdrImage.pixels.data();

    std::cout << "IBLEnvironment: Loaded HDR " << width << "x" << height << "\n";

    // Convert equirectangular to cubemap
    CubemapData envCubemap = equirectangularToCubemap(hdrData, width, height, CUBEMAP_SIZE);

    if (!envCubemap.valid()) {
        std::cerr << "IBLEnvironment: Failed to convert to cubemap\n";
        return false;
    }

    // Clean up old maps if reloading
    destroyCubemap(m_irradianceMap);
    destroyCubemap(m_prefilteredMap);

    // Compute irradiance map (diffuse IBL)
    m_irradianceMap = computeIrradiance(envCubemap, IRRADIANCE_SIZE);

    // Compute prefiltered radiance map (specular IBL)
    m_prefilteredMap = computeRadiance(envCubemap, PREFILTER_SIZE, PREFILTER_MIP_LEVELS);

    // Create BRDF LUT if not already created
    if (!m_brdfLUT) {
        createBRDFLUT(BRDF_LUT_SIZE);
    }

    // Clean up source cubemap
    destroyCubemap(envCubemap);

    if (isLoaded()) {
        std::cout << "IBLEnvironment: Environment loaded successfully\n";
        return true;
    }

    std::cerr << "IBLEnvironment: Failed to generate IBL maps\n";
    return false;
}

bool IBLEnvironment::loadDefault(Context& ctx) {
    if (!m_initialized && !init(ctx)) {
        return false;
    }

    // Generate a simple procedural HDR environment (studio lighting)
    // Gradient sky transitioning from warm horizon to cool zenith
    // with a subtle ground plane

    const int width = 512;
    const int height = 256;
    std::vector<float> hdrData(width * height * 3);

    for (int y = 0; y < height; y++) {
        // Normalized v coordinate: 0 = top (zenith), 1 = bottom (nadir)
        float v = static_cast<float>(y) / static_cast<float>(height - 1);

        // Sky gradient: cool blue at zenith, warm at horizon
        glm::vec3 skyColor;
        if (v < 0.5f) {
            // Upper hemisphere (sky)
            float t = v * 2.0f;  // 0 at zenith, 1 at horizon

            // Zenith color (cool blue)
            glm::vec3 zenith(0.3f, 0.5f, 0.9f);
            // Horizon color (warm white/cream)
            glm::vec3 horizon(1.0f, 0.95f, 0.85f);

            // Smooth gradient with easing
            float blend = std::pow(t, 0.7f);
            skyColor = glm::mix(zenith, horizon, blend);

            // Add slight brightness boost near horizon
            float horizonGlow = std::exp(-std::pow((t - 1.0f) * 3.0f, 2.0f));
            skyColor += glm::vec3(0.3f, 0.25f, 0.2f) * horizonGlow;
        } else {
            // Lower hemisphere (ground plane reflection)
            float t = (v - 0.5f) * 2.0f;  // 0 at horizon, 1 at nadir

            // Ground color (neutral gray with slight warmth)
            glm::vec3 ground(0.15f, 0.15f, 0.14f);
            // Horizon reflection (brighter)
            glm::vec3 horizonReflect(0.4f, 0.38f, 0.35f);

            float blend = std::pow(t, 0.5f);
            skyColor = glm::mix(horizonReflect, ground, blend);
        }

        // Apply HDR scaling (values > 1 for bright areas)
        skyColor *= 1.5f;

        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            hdrData[idx + 0] = skyColor.r;
            hdrData[idx + 1] = skyColor.g;
            hdrData[idx + 2] = skyColor.b;
        }
    }

    std::cout << "IBLEnvironment: Generated default procedural environment\n";

    // Convert equirectangular to cubemap
    CubemapData envCubemap = equirectangularToCubemap(hdrData.data(), width, height, CUBEMAP_SIZE);

    if (!envCubemap.valid()) {
        std::cerr << "IBLEnvironment: Failed to convert default environment to cubemap\n";
        return false;
    }

    // Clean up old maps if reloading
    destroyCubemap(m_irradianceMap);
    destroyCubemap(m_prefilteredMap);

    // Compute irradiance map (diffuse IBL)
    m_irradianceMap = computeIrradiance(envCubemap, IRRADIANCE_SIZE);

    // Compute prefiltered radiance map (specular IBL)
    m_prefilteredMap = computeRadiance(envCubemap, PREFILTER_SIZE, PREFILTER_MIP_LEVELS);

    // Create BRDF LUT if not already created
    if (!m_brdfLUT) {
        createBRDFLUT(BRDF_LUT_SIZE);
    }

    // Clean up source cubemap
    destroyCubemap(envCubemap);

    if (isLoaded()) {
        std::cout << "IBLEnvironment: Default environment loaded successfully\n";
        return true;
    }

    std::cerr << "IBLEnvironment: Failed to generate default IBL maps\n";
    return false;
}

WGPUTextureView IBLEnvironment::irradianceView() const {
    return m_irradianceMap.view;
}

WGPUTextureView IBLEnvironment::prefilteredView() const {
    return m_prefilteredMap.view;
}

WGPUTextureView IBLEnvironment::brdfLUTView() const {
    return m_brdfLUTView;
}

// ============================================================================
// Pipeline Creation
// ============================================================================

bool IBLEnvironment::createPipelines() {
    if (!m_device) return false;

    // Create sampler for cubemap processing
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    m_cubemapSampler = wgpuDeviceCreateSampler(m_device, &samplerDesc);

    // -------------------------------------------------------------------------
    // BRDF LUT Pipeline
    // -------------------------------------------------------------------------
    {
        WGPUShaderModule module = createShaderModule(m_device, BRDF_LUT_SHADER);
        if (!module) {
            std::cerr << "IBLEnvironment: Failed to create BRDF LUT shader\n";
            return false;
        }

        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Compute;
        entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entry.storageTexture.format = WGPUTextureFormat_RGBA16Float;
        entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        m_brdfLayout = wgpuDeviceCreateBindGroupLayout(m_device, &layoutDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &m_brdfLayout;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = toStringView("main");
        m_brdfPipeline = wgpuDeviceCreateComputePipeline(m_device, &pipelineDesc);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    // -------------------------------------------------------------------------
    // Equirectangular to Cubemap Pipeline
    // -------------------------------------------------------------------------
    {
        WGPUShaderModule module = createShaderModule(m_device, EQUIRECT_TO_CUBEMAP_SHADER);
        if (!module) {
            std::cerr << "IBLEnvironment: Failed to create equirect shader\n";
            return false;
        }

        WGPUBindGroupLayoutEntry entries[3] = {};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Compute;
        entries[0].texture.sampleType = WGPUTextureSampleType_Float;
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
        m_equirectLayout = wgpuDeviceCreateBindGroupLayout(m_device, &layoutDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &m_equirectLayout;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = toStringView("main");
        m_equirectPipeline = wgpuDeviceCreateComputePipeline(m_device, &pipelineDesc);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    // -------------------------------------------------------------------------
    // Irradiance Convolution Pipeline
    // -------------------------------------------------------------------------
    {
        WGPUShaderModule module = createShaderModule(m_device, IRRADIANCE_SHADER);
        if (!module) {
            std::cerr << "IBLEnvironment: Failed to create irradiance shader\n";
            return false;
        }

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
        m_irradianceLayout = wgpuDeviceCreateBindGroupLayout(m_device, &layoutDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &m_irradianceLayout;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = toStringView("main");
        m_irradiancePipeline = wgpuDeviceCreateComputePipeline(m_device, &pipelineDesc);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    // -------------------------------------------------------------------------
    // Radiance Prefilter Pipeline
    // -------------------------------------------------------------------------
    {
        WGPUShaderModule module = createShaderModule(m_device, RADIANCE_SHADER);
        if (!module) {
            std::cerr << "IBLEnvironment: Failed to create radiance shader\n";
            return false;
        }

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
        entries[3].buffer.minBindingSize = 16;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 4;
        layoutDesc.entries = entries;
        m_radianceLayout = wgpuDeviceCreateBindGroupLayout(m_device, &layoutDesc);

        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &m_radianceLayout;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = toStringView("main");
        m_radiancePipeline = wgpuDeviceCreateComputePipeline(m_device, &pipelineDesc);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(module);
    }

    return true;
}

void IBLEnvironment::destroyPipelines() {
    if (m_cubemapSampler) {
        wgpuSamplerRelease(m_cubemapSampler);
        m_cubemapSampler = nullptr;
    }

    if (m_equirectPipeline) wgpuComputePipelineRelease(m_equirectPipeline);
    if (m_irradiancePipeline) wgpuComputePipelineRelease(m_irradiancePipeline);
    if (m_radiancePipeline) wgpuComputePipelineRelease(m_radiancePipeline);
    if (m_brdfPipeline) wgpuComputePipelineRelease(m_brdfPipeline);

    m_equirectPipeline = nullptr;
    m_irradiancePipeline = nullptr;
    m_radiancePipeline = nullptr;
    m_brdfPipeline = nullptr;

    if (m_equirectLayout) wgpuBindGroupLayoutRelease(m_equirectLayout);
    if (m_irradianceLayout) wgpuBindGroupLayoutRelease(m_irradianceLayout);
    if (m_radianceLayout) wgpuBindGroupLayoutRelease(m_radianceLayout);
    if (m_brdfLayout) wgpuBindGroupLayoutRelease(m_brdfLayout);

    m_equirectLayout = nullptr;
    m_irradianceLayout = nullptr;
    m_radianceLayout = nullptr;
    m_brdfLayout = nullptr;
}

// ============================================================================
// Cubemap Operations
// ============================================================================

CubemapData IBLEnvironment::createCubemap(int size, int mipLevels, bool hdr) {
    CubemapData result;
    if (!m_device) return result;

    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = size;
    texDesc.size.height = size;
    texDesc.size.depthOrArrayLayers = 6;
    texDesc.mipLevelCount = mipLevels;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = hdr ? WGPUTextureFormat_RGBA16Float : WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst;

    result.texture = wgpuDeviceCreateTexture(m_device, &texDesc);
    if (!result.texture) return result;

    // Create cube view for sampling
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = texDesc.format;
    viewDesc.dimension = WGPUTextureViewDimension_Cube;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = mipLevels;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 6;
    result.view = wgpuTextureCreateView(result.texture, &viewDesc);

    result.size = size;
    result.mipLevels = mipLevels;
    return result;
}

void IBLEnvironment::destroyCubemap(CubemapData& cubemap) {
    if (cubemap.view) {
        wgpuTextureViewRelease(cubemap.view);
    }
    if (cubemap.texture) {
        wgpuTextureDestroy(cubemap.texture);
    }
    cubemap = CubemapData();
}

CubemapData IBLEnvironment::equirectangularToCubemap(const float* hdrPixels, int width, int height, int cubemapSize) {
    CubemapData output = createCubemap(cubemapSize, 1, true);
    if (!output.valid() || !m_equirectPipeline) return {};

    // Upload HDR data to a 2D texture (RGBA16Float for filtering)
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = width;
    texDesc.size.height = height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA16Float;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    WGPUTexture hdrTexture = wgpuDeviceCreateTexture(m_device, &texDesc);

    // Convert RGB float32 to RGBA float16
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
    wgpuQueueWriteTexture(m_queue, &dest, rgbaData.data(), rgbaData.size() * sizeof(uint16_t), &layout, &copySize);

    // Create views
    WGPUTextureViewDescriptor hdrViewDesc = {};
    hdrViewDesc.format = WGPUTextureFormat_RGBA16Float;
    hdrViewDesc.dimension = WGPUTextureViewDimension_2D;
    hdrViewDesc.mipLevelCount = 1;
    hdrViewDesc.arrayLayerCount = 1;
    WGPUTextureView hdrView = wgpuTextureCreateView(hdrTexture, &hdrViewDesc);

    WGPUTextureViewDescriptor outputViewDesc = {};
    outputViewDesc.format = WGPUTextureFormat_RGBA16Float;
    outputViewDesc.dimension = WGPUTextureViewDimension_2DArray;
    outputViewDesc.baseMipLevel = 0;
    outputViewDesc.mipLevelCount = 1;
    outputViewDesc.baseArrayLayer = 0;
    outputViewDesc.arrayLayerCount = 6;
    WGPUTextureView outputView = wgpuTextureCreateView(output.texture, &outputViewDesc);

    // Create bind group
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].textureView = hdrView;
    entries[1].binding = 1;
    entries[1].sampler = m_cubemapSampler;
    entries[2].binding = 2;
    entries[2].textureView = outputView;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = m_equirectLayout;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(m_device, &bindGroupDesc);

    // Dispatch compute
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encDesc);

    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, m_equirectPipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

    uint32_t workgroupsX = (cubemapSize + 15) / 16;
    uint32_t workgroupsY = (cubemapSize + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 6);

    wgpuComputePassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(m_queue, 1, &cmdBuffer);

    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
    wgpuTextureViewRelease(outputView);
    wgpuTextureViewRelease(hdrView);
    wgpuTextureDestroy(hdrTexture);

    std::cout << "IBLEnvironment: Converted equirect to cubemap (" << cubemapSize << "x" << cubemapSize << ")\n";
    return output;
}

CubemapData IBLEnvironment::computeIrradiance(const CubemapData& envCubemap, int size) {
    CubemapData output = createCubemap(size, 1, true);
    if (!output.valid() || !m_irradiancePipeline) return {};

    // Create 2D array view for output
    WGPUTextureViewDescriptor outputViewDesc = {};
    outputViewDesc.format = WGPUTextureFormat_RGBA16Float;
    outputViewDesc.dimension = WGPUTextureViewDimension_2DArray;
    outputViewDesc.mipLevelCount = 1;
    outputViewDesc.arrayLayerCount = 6;
    WGPUTextureView outputView = wgpuTextureCreateView(output.texture, &outputViewDesc);

    // Bind group
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].textureView = envCubemap.view;
    entries[1].binding = 1;
    entries[1].sampler = m_cubemapSampler;
    entries[2].binding = 2;
    entries[2].textureView = outputView;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = m_irradianceLayout;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(m_device, &bindGroupDesc);

    // Dispatch
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encDesc);

    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, m_irradiancePipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

    uint32_t workgroupsX = (size + 15) / 16;
    uint32_t workgroupsY = (size + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 6);

    wgpuComputePassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(m_queue, 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
    wgpuTextureViewRelease(outputView);

    std::cout << "IBLEnvironment: Computed irradiance map (" << size << "x" << size << ")\n";
    return output;
}

CubemapData IBLEnvironment::computeRadiance(const CubemapData& envCubemap, int size, int mipLevels) {
    CubemapData output = createCubemap(size, mipLevels, true);
    if (!output.valid() || !m_radiancePipeline) return {};

    // Params buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = 16;
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer paramsBuffer = wgpuDeviceCreateBuffer(m_device, &bufDesc);

    // Process each mip level
    for (int mip = 0; mip < mipLevels; mip++) {
        int mipSize = size >> mip;
        float roughness = (float)mip / (float)(mipLevels - 1);

        float params[4] = { roughness, (float)mipSize, 0.0f, 0.0f };
        wgpuQueueWriteBuffer(m_queue, paramsBuffer, 0, params, sizeof(params));

        // View for this mip
        WGPUTextureViewDescriptor outputViewDesc = {};
        outputViewDesc.format = WGPUTextureFormat_RGBA16Float;
        outputViewDesc.dimension = WGPUTextureViewDimension_2DArray;
        outputViewDesc.baseMipLevel = mip;
        outputViewDesc.mipLevelCount = 1;
        outputViewDesc.baseArrayLayer = 0;
        outputViewDesc.arrayLayerCount = 6;
        WGPUTextureView outputView = wgpuTextureCreateView(output.texture, &outputViewDesc);

        // Bind group
        WGPUBindGroupEntry entries[4] = {};
        entries[0].binding = 0;
        entries[0].textureView = envCubemap.view;
        entries[1].binding = 1;
        entries[1].sampler = m_cubemapSampler;
        entries[2].binding = 2;
        entries[2].textureView = outputView;
        entries[3].binding = 3;
        entries[3].buffer = paramsBuffer;
        entries[3].size = 16;

        WGPUBindGroupDescriptor bindGroupDesc = {};
        bindGroupDesc.layout = m_radianceLayout;
        bindGroupDesc.entryCount = 4;
        bindGroupDesc.entries = entries;
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(m_device, &bindGroupDesc);

        // Dispatch
        WGPUCommandEncoderDescriptor encDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encDesc);

        WGPUComputePassDescriptor passDesc = {};
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

        wgpuComputePassEncoderSetPipeline(pass, m_radiancePipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

        uint32_t workgroupsX = (mipSize + 15) / 16;
        uint32_t workgroupsY = (mipSize + 15) / 16;
        wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 6);

        wgpuComputePassEncoderEnd(pass);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(m_queue, 1, &cmdBuffer);

        wgpuCommandBufferRelease(cmdBuffer);
        wgpuComputePassEncoderRelease(pass);
        wgpuCommandEncoderRelease(encoder);
        wgpuBindGroupRelease(bindGroup);
        wgpuTextureViewRelease(outputView);
    }

    wgpuBufferDestroy(paramsBuffer);
    wgpuBufferRelease(paramsBuffer);

    std::cout << "IBLEnvironment: Computed radiance map (" << size << "x" << size << ", " << mipLevels << " mips)\n";
    return output;
}

bool IBLEnvironment::createBRDFLUT(int size) {
    if (!m_brdfPipeline) return false;

    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = size;
    texDesc.size.height = size;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA16Float;  // Use RGBA for storage support, sample RG
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;

    m_brdfLUT = wgpuDeviceCreateTexture(m_device, &texDesc);
    if (!m_brdfLUT) return false;

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA16Float;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    m_brdfLUTView = wgpuTextureCreateView(m_brdfLUT, &viewDesc);

    // Bind group
    WGPUBindGroupEntry entry = {};
    entry.binding = 0;
    entry.textureView = m_brdfLUTView;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = m_brdfLayout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &entry;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(m_device, &bindGroupDesc);

    // Dispatch
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encDesc);

    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, m_brdfPipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

    uint32_t workgroupsX = (size + 15) / 16;
    uint32_t workgroupsY = (size + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroupsX, workgroupsY, 1);

    wgpuComputePassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(m_queue, 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuComputePassEncoderRelease(pass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);

    std::cout << "IBLEnvironment: Generated BRDF LUT (" << size << "x" << size << ")\n";
    return true;
}

} // namespace vivid::render3d
