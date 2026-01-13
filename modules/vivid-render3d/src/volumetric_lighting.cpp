// Volumetric Lighting Post-Processing Effect Implementation

#include <vivid/render3d/volumetric_lighting.h>
#include <vivid/render3d/renderer.h>
#include <vivid/render3d/light_operators.h>
#include <vivid/render3d/camera_operator.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <cstring>

namespace vivid::render3d {

REGISTER_OPERATOR(VolumetricLighting, "3D Post-Processing", "Volumetric lighting with god rays", true);

using namespace vivid::effects;

namespace {

const char* VOLUMETRIC_SHADER_SOURCE = R"(
struct Uniforms {
    // Camera
    invViewProj: mat4x4f,
    cameraPos: vec3f,
    nearPlane: f32,
    farPlane: f32,

    // Light
    lightType: i32,         // 0=Directional, 1=Point, 2=Spot
    lightPos: vec3f,
    lightDir: vec3f,
    lightColor: vec3f,
    lightIntensity: f32,
    lightRange: f32,
    spotAngle: f32,         // cos of outer angle
    spotBlend: f32,

    // Volumetric params
    raySteps: i32,
    maxDistance: f32,
    density: f32,
    intensity: f32,
    anisotropy: f32,
    fogColor: vec3f,
    debugMode: i32,         // 0=off, 1=depth, 2=worldPos, 3=distance, 4=light

    // Shadow data (Phase 2)
    lightViewProj: mat4x4f,
    shadowBias: f32,
    shadowStrength: f32,
    useShadows: i32,

    // Output mode (Phase 3): 0 = composite, 1 = volumetric only
    outputMode: i32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var colorTexture: texture_2d<f32>;
@group(0) @binding(2) var depthTexture: texture_2d<f32>;
@group(0) @binding(3) var texSampler: sampler;
@group(0) @binding(4) var shadowMap: texture_depth_2d;
@group(0) @binding(5) var shadowSampler: sampler_comparison;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;
    let x = f32(i32(vertexIndex & 1u) * 4 - 1);
    let y = f32(i32(vertexIndex >> 1u) * 4 - 1);
    output.position = vec4f(x, y, 0.0, 1.0);
    output.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return output;
}

// Henyey-Greenstein phase function for anisotropic scattering
// g: anisotropy (-1 = back scatter, 0 = isotropic, 1 = forward scatter)
// Henyey-Greenstein phase function (artistic version - no 4π normalization)
fn phaseHG(cosTheta: f32, g: f32) -> f32 {
    let g2 = g * g;
    let denom = 1.0 + g2 - 2.0 * g * cosTheta;
    // Removed 4π normalization for more visible artistic effect
    return (1.0 - g2) / pow(denom, 1.5);
}

// Reconstruct world position from depth and UV
fn worldPosFromDepth(uv: vec2f, normalizedDepth: f32) -> vec3f {
    // Convert UV to clip space
    let clipX = uv.x * 2.0 - 1.0;
    let clipY = (1.0 - uv.y) * 2.0 - 1.0;

    // Normalized depth is 0-1, convert to clip space Z
    // Note: WebGPU uses 0-1 depth range
    let clipZ = normalizedDepth;

    let clipPos = vec4f(clipX, clipY, clipZ, 1.0);
    var worldPos = uniforms.invViewProj * clipPos;
    worldPos = worldPos / worldPos.w;

    return worldPos.xyz;
}

// Light attenuation for point/spot lights
fn lightAttenuation(dist: f32) -> f32 {
    if (uniforms.lightType == 0) {
        return 1.0;  // Directional light has no falloff
    }
    let range = uniforms.lightRange;
    let attenuation = saturate(1.0 - (dist / range));
    return attenuation * attenuation;
}

// Spot light cone attenuation
fn spotAttenuation(lightToPoint: vec3f) -> f32 {
    if (uniforms.lightType != 2) {
        return 1.0;  // Not a spot light
    }
    let cosAngle = dot(normalize(lightToPoint), uniforms.lightDir);
    let outerCos = uniforms.spotAngle;
    let innerCos = outerCos + uniforms.spotBlend * (1.0 - outerCos);
    return saturate((cosAngle - outerCos) / (innerCos - outerCos));
}

// Sample shadow map at world position (Phase 2)
// Returns 1.0 if lit, 0.0 if in shadow
fn sampleShadow(worldPos: vec3f) -> f32 {
    // Skip shadow sampling if disabled
    if (uniforms.useShadows == 0) {
        return 1.0;
    }

    // Transform world position to light clip space
    let lightSpacePos = uniforms.lightViewProj * vec4f(worldPos, 1.0);
    let projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Convert to shadow map UV coordinates
    // Clip space: [-1, 1] -> UV: [0, 1]
    let shadowUV = vec2f(
        projCoords.x * 0.5 + 0.5,
        projCoords.y * -0.5 + 0.5  // Flip Y for texture coordinates
    );

    // Current depth in light space (WebGPU uses 0-1 depth range)
    let currentDepth = projCoords.z;

    // Out of shadow map bounds = lit (no shadow data available)
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
        currentDepth < 0.0 || currentDepth > 1.0) {
        return 1.0;
    }

    // Sample shadow map with comparison sampler
    // Returns 1.0 if currentDepth - bias <= shadowMapDepth (lit)
    // Returns 0.0 if currentDepth - bias > shadowMapDepth (shadowed)
    let shadow = textureSampleCompare(
        shadowMap, shadowSampler,
        shadowUV, currentDepth - uniforms.shadowBias
    );

    // Mix based on shadow strength (0 = ignore shadows, 1 = full shadows)
    return mix(1.0, shadow, uniforms.shadowStrength);
}

// Get light contribution at a point in space
fn getLightContribution(pos: vec3f, viewDir: vec3f) -> vec3f {
    var lightDir: vec3f;
    var attenuation: f32 = 1.0;

    if (uniforms.lightType == 0) {
        // Directional light
        lightDir = -normalize(uniforms.lightDir);
    } else {
        // Point or spot light
        let toLight = uniforms.lightPos - pos;
        let dist = length(toLight);
        lightDir = toLight / dist;
        attenuation = lightAttenuation(dist) * spotAttenuation(-toLight);
    }

    // Compute phase function (scattering direction preference)
    let cosTheta = dot(viewDir, lightDir);
    let phase = phaseHG(cosTheta, uniforms.anisotropy);

    return uniforms.lightColor * uniforms.lightIntensity * attenuation * phase;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(colorTexture, texSampler, input.uv);
    let normalizedDepth = textureSample(depthTexture, texSampler, input.uv).r;

    // Debug mode 5: Pass-through color only (verify color sampling works)
    if (uniforms.debugMode == 5) {
        return color;
    }

    // Debug mode 6: Show light color/intensity (verify light data)
    if (uniforms.debugMode == 6) {
        return vec4f(uniforms.lightColor * uniforms.lightIntensity * 0.5, 1.0);
    }

    // Debug mode 7: Show light type as color (R=0 dir, G=1 point, B=2 spot)
    if (uniforms.debugMode == 7) {
        var typeColor = vec3f(0.0);
        if (uniforms.lightType == 0) { typeColor = vec3f(1.0, 0.0, 0.0); }
        if (uniforms.lightType == 1) { typeColor = vec3f(0.0, 1.0, 0.0); }
        if (uniforms.lightType == 2) { typeColor = vec3f(0.0, 0.0, 1.0); }
        return vec4f(typeColor, 1.0);
    }

    // Debug mode 8: Show light attenuation at midpoint of ray
    if (uniforms.debugMode == 8) {
        let worldPos = worldPosFromDepth(input.uv, normalizedDepth);
        let toSurface = worldPos - uniforms.cameraPos;
        let midpoint = uniforms.cameraPos + toSurface * 0.5;
        let toLight = uniforms.lightPos - midpoint;
        let dist = length(toLight);
        let atten = lightAttenuation(dist);
        return vec4f(vec3f(atten), 1.0);
    }

    // Debug mode 9: Show light range / distance ratio
    if (uniforms.debugMode == 9) {
        let worldPos = worldPosFromDepth(input.uv, normalizedDepth);
        let toSurface = worldPos - uniforms.cameraPos;
        let midpoint = uniforms.cameraPos + toSurface * 0.5;
        let dist = length(uniforms.lightPos - midpoint);
        let ratio = dist / uniforms.lightRange;
        return vec4f(vec3f(ratio), 1.0);
    }

    // Debug mode 10: Show raySteps as grayscale (scaled to 0-1, assuming max 128)
    if (uniforms.debugMode == 10) {
        let stepsViz = f32(uniforms.raySteps) / 128.0;
        return vec4f(vec3f(stepsViz), 1.0);
    }

    // Debug mode 11: Show single sample light contribution at midpoint (scaled up for visibility)
    if (uniforms.debugMode == 11) {
        let worldPos = worldPosFromDepth(input.uv, normalizedDepth);
        let toSurface = worldPos - uniforms.cameraPos;
        let midpoint = uniforms.cameraPos + toSurface * 0.5;
        let viewDir = -normalize(toSurface);
        let contrib = getLightContribution(midpoint, viewDir);
        return vec4f(contrib * 10.0, 1.0);  // Scale up 10x for visibility
    }

    // Debug mode 1: Show normalized depth (inverted so close=white, far=black)
    if (uniforms.debugMode == 1) {
        let invDepth = 1.0 - normalizedDepth;
        return vec4f(vec3f(invDepth), 1.0);
    }

    // Reconstruct world position
    let worldPos = worldPosFromDepth(input.uv, normalizedDepth);

    // Debug mode 2: Show world position (mapped to color, scaled)
    if (uniforms.debugMode == 2) {
        let scaledPos = (worldPos + vec3f(10.0)) / 20.0;  // Map [-10,10] to [0,1]
        return vec4f(scaledPos, 1.0);
    }

    // Calculate distance from camera to surface
    let toSurface = worldPos - uniforms.cameraPos;
    let surfaceDist = length(toSurface);

    // Debug mode 3: Show distance from camera (scaled)
    if (uniforms.debugMode == 3) {
        let distViz = surfaceDist / uniforms.farPlane;
        return vec4f(vec3f(distViz), 1.0);
    }

    // Ray marching setup
    // For sky pixels, still ray march up to maxDistance to show light in empty air
    let isSky = normalizedDepth > 0.999;
    let rayOrigin = uniforms.cameraPos;
    let rayDir = normalize(toSurface);
    let rayLength = select(min(surfaceDist, uniforms.maxDistance), uniforms.maxDistance, isSky);
    let steps = uniforms.raySteps;
    let stepSize = rayLength / f32(steps);

    // Accumulate scattered light using ray marching
    var scatteredLight = vec3f(0.0);
    var transmittance = 1.0;

    for (var i = 0; i < steps; i = i + 1) {
        let t = (f32(i) + 0.5) * stepSize;
        let samplePos = rayOrigin + rayDir * t;

        // Get light contribution at this sample point
        let lightContrib = getLightContribution(samplePos, -rayDir);

        // Sample shadow map to check if this point is occluded (Phase 2)
        let shadowFactor = sampleShadow(samplePos);

        // Beer-Lambert extinction
        let extinction = uniforms.density * stepSize;
        let sampleTransmittance = exp(-extinction);

        // Accumulate in-scattered light (modulated by shadow)
        scatteredLight += lightContrib * shadowFactor * transmittance * (1.0 - sampleTransmittance);
        transmittance *= sampleTransmittance;
    }

    // Debug mode 4: Show accumulated light contribution
    if (uniforms.debugMode == 4) {
        return vec4f(scatteredLight * uniforms.intensity, 1.0);
    }

    // Debug mode 12: Show loop iteration check (green if loop ran, red if not)
    if (uniforms.debugMode == 12) {
        var loopRan = 0.0;
        for (var j = 0; j < uniforms.raySteps; j = j + 1) {
            loopRan = 1.0;
            break;
        }
        if (loopRan > 0.5) {
            return vec4f(0.0, 1.0, 0.0, 1.0);  // Green = loop ran
        } else {
            return vec4f(1.0, 0.0, 0.0, 1.0);  // Red = loop didn't run
        }
    }

    // Apply intensity and add fog color tint
    scatteredLight *= uniforms.intensity;
    scatteredLight += uniforms.fogColor * (1.0 - transmittance);

    // Clamp to prevent overflow
    scatteredLight = clamp(scatteredLight, vec3f(0.0), vec3f(2.0));

    // Output mode: 1 = volumetric only (for low-res pass)
    if (uniforms.outputMode == 1) {
        return vec4f(scatteredLight, transmittance);
    }

    // Output mode: 0 = composite (default, full-res pass)
    let finalColor = color.rgb + scatteredLight;
    return vec4f(finalColor, color.a);
}
)";

// Uniform struct matching WGSL layout
// WGSL alignment: vec3f = align 16 / size 12, mat4x4f = align 16 / size 64
// Next field after vec3f can start at any properly-aligned offset
struct VolumetricUniforms {
    // invViewProj: mat4x4f (offset 0, size 64)
    float invViewProj[16];  // 0-63

    // cameraPos: vec3f (align 16, size 12) - offset 64 is 16-aligned
    float cameraPosX;       // 64
    float cameraPosY;       // 68
    float cameraPosZ;       // 72

    // nearPlane: f32 (align 4) - can start at 76
    float nearPlane;        // 76

    // farPlane: f32 (align 4)
    float farPlane;         // 80

    // lightType: i32 (align 4)
    int lightType;          // 84

    // padding to align lightPos: vec3f to 16-byte boundary (96)
    float _pad1[2];         // 88, 92 (fills 88-95, 2 floats = 8 bytes)

    // lightPos: vec3f (align 16) - offset 96
    float lightPosX;        // 96
    float lightPosY;        // 100
    float lightPosZ;        // 104

    // padding to align lightDir to 16-byte boundary (112)
    float _pad2;            // 108

    // lightDir: vec3f (align 16) - offset 112
    float lightDirX;        // 112
    float lightDirY;        // 116
    float lightDirZ;        // 120

    // padding to align lightColor to 16-byte boundary (128)
    float _pad3;            // 124

    // lightColor: vec3f (align 16) - offset 128
    float lightColorR;      // 128
    float lightColorG;      // 132
    float lightColorB;      // 136

    // f32 fields (align 4)
    float lightIntensity;   // 140
    float lightRange;       // 144
    float spotAngle;        // 148
    float spotBlend;        // 152

    // raySteps: i32 (align 4)
    int raySteps;           // 156

    // f32 fields
    float maxDistance;      // 160
    float density;          // 164
    float intensity;        // 168
    float anisotropy;       // 172

    // fogColor: vec3f (align 16) - next 16-byte boundary is 176
    float fogColorR;        // 176
    float fogColorG;        // 180
    float fogColorB;        // 184

    int debugMode;          // 188 (debugMode: i32)

    // Shadow data (Phase 2)
    // padding to align lightViewProj to 16-byte boundary (192)
    // 188 + 4 bytes = 192, but we need to pad from 192 to mat4x4f alignment
    // debugMode ends at 192, which is already 16-aligned

    // lightViewProj: mat4x4f (align 16, size 64) - offset 192
    float lightViewProj[16];    // 192-255

    // Shadow parameters
    float shadowBias;           // 256
    float shadowStrength;       // 260
    int useShadows;             // 264

    // Output mode (Phase 3): 0 = composite, 1 = volumetric only
    int outputMode;             // 268
};  // Total size: 272 bytes (already 16-byte aligned)

// Bilinear upsample shader (Phase 3)
// Upsamples low-res volumetric and composites with full-res scene
const char* BILATERAL_UPSAMPLE_SHADER_SOURCE = R"(
struct UpsampleUniforms {
    lowResSize: vec2f,      // Size of low-res texture
    fullResSize: vec2f,     // Size of full-res texture
    depthWeight: f32,       // Reserved for future bilateral filtering
    _pad: vec3f,
};

@group(0) @binding(0) var<uniform> uniforms: UpsampleUniforms;
@group(0) @binding(1) var lowResVolumetric: texture_2d<f32>;
@group(0) @binding(2) var lowResDepth: texture_2d<f32>;
@group(0) @binding(3) var fullResColor: texture_2d<f32>;
@group(0) @binding(4) var fullResDepth: texture_2d<f32>;
@group(0) @binding(5) var texSampler: sampler;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;
    let x = f32(i32(vertexIndex & 1u) * 4 - 1);
    let y = f32(i32(vertexIndex >> 1u) * 4 - 1);
    output.position = vec4f(x, y, 0.0, 1.0);
    output.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let sceneColor = textureSample(fullResColor, texSampler, input.uv);

    // Bilinear upsample of low-res volumetric
    // The sampler handles bilinear filtering automatically
    let volumetric = textureSample(lowResVolumetric, texSampler, input.uv).rgb;

    // Composite: add volumetric to scene color
    return vec4f(sceneColor.rgb + volumetric, sceneColor.a);
}
)";

// Upsample uniforms struct (C++ side)
struct UpsampleUniforms {
    float lowResWidth;      // 0
    float lowResHeight;     // 4
    float fullResWidth;     // 8
    float fullResHeight;    // 12
    float depthWeight;      // 16
    float _pad[3];          // 20, 24, 28 (align to 32)
};  // Total: 32 bytes

} // namespace

VolumetricLighting::VolumetricLighting() {
    registerParam(raySteps);
    registerParam(maxDistance);
    registerParam(density);
    registerParam(intensity);
    registerParam(anisotropy);
    registerParam(useShadows);
    registerParam(shadowBias);
    registerParam(shadowStrength);
    registerParam(debugMode);
    registerParam(resolutionScale);
}

VolumetricLighting::~VolumetricLighting() {
    cleanup();
}

void VolumetricLighting::input(Render3D* render) {
    m_render3d = render;
    setInput(0, render);
}

void VolumetricLighting::lightInput(LightOperator* light) {
    m_lightOp = light;
    setInput(1, light);
}

void VolumetricLighting::cameraInput(CameraOperator* camera) {
    m_cameraOp = camera;
    setInput(2, camera);
}

void VolumetricLighting::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void VolumetricLighting::createPipeline(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(VOLUMETRIC_SHADER_SOURCE);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create sampler for color/depth textures
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    // Create shadow comparison sampler (Phase 2)
    WGPUSamplerDescriptor shadowSamplerDesc = {};
    shadowSamplerDesc.compare = WGPUCompareFunction_LessEqual;
    shadowSamplerDesc.magFilter = WGPUFilterMode_Linear;
    shadowSamplerDesc.minFilter = WGPUFilterMode_Linear;
    shadowSamplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    shadowSamplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    shadowSamplerDesc.maxAnisotropy = 1;
    m_shadowSampler = wgpuDeviceCreateSampler(device, &shadowSamplerDesc);

    // Create dummy shadow texture (1x1 depth texture) for when shadows are disabled
    WGPUTextureDescriptor dummyTexDesc = {};
    dummyTexDesc.size.width = 1;
    dummyTexDesc.size.height = 1;
    dummyTexDesc.size.depthOrArrayLayers = 1;
    dummyTexDesc.mipLevelCount = 1;
    dummyTexDesc.sampleCount = 1;
    dummyTexDesc.dimension = WGPUTextureDimension_2D;
    dummyTexDesc.format = WGPUTextureFormat_Depth32Float;
    dummyTexDesc.usage = WGPUTextureUsage_TextureBinding;
    m_dummyShadowTexture = wgpuDeviceCreateTexture(device, &dummyTexDesc);

    WGPUTextureViewDescriptor dummyViewDesc = {};
    dummyViewDesc.format = WGPUTextureFormat_Depth32Float;
    dummyViewDesc.dimension = WGPUTextureViewDimension_2D;
    dummyViewDesc.baseMipLevel = 0;
    dummyViewDesc.mipLevelCount = 1;
    dummyViewDesc.baseArrayLayer = 0;
    dummyViewDesc.arrayLayerCount = 1;
    m_dummyShadowView = wgpuTextureCreateView(m_dummyShadowTexture, &dummyViewDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(VolumetricUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Bind group layout (6 entries: uniforms, color, depth, sampler, shadow map, shadow sampler)
    WGPUBindGroupLayoutEntry layoutEntries[6] = {};

    // Uniforms
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    // Color texture
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Depth texture
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Sampler (for color/depth)
    layoutEntries[3].binding = 3;
    layoutEntries[3].visibility = WGPUShaderStage_Fragment;
    layoutEntries[3].sampler.type = WGPUSamplerBindingType_Filtering;

    // Shadow map texture (Phase 2)
    layoutEntries[4].binding = 4;
    layoutEntries[4].visibility = WGPUShaderStage_Fragment;
    layoutEntries[4].texture.sampleType = WGPUTextureSampleType_Depth;
    layoutEntries[4].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Shadow comparison sampler (Phase 2)
    layoutEntries[5].binding = 5;
    layoutEntries[5].visibility = WGPUShaderStage_Fragment;
    layoutEntries[5].sampler.type = WGPUSamplerBindingType_Comparison;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 6;
    layoutDesc.entries = layoutEntries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Color target - use RGBA16Float to match the chain's HDR format
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA16Float;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void VolumetricLighting::createUpsamplePipeline(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(BILATERAL_UPSAMPLE_SHADER_SOURCE);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(UpsampleUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_upsampleUniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Bind group layout (6 entries: uniforms, lowResVolumetric, lowResDepth, fullResColor, fullResDepth, sampler)
    WGPUBindGroupLayoutEntry layoutEntries[6] = {};

    // Uniforms
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;

    // Low-res volumetric texture
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Low-res depth texture (R32Float is unfilterable)
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Full-res color texture
    layoutEntries[3].binding = 3;
    layoutEntries[3].visibility = WGPUShaderStage_Fragment;
    layoutEntries[3].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[3].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Full-res depth texture (depth formats may be unfilterable)
    layoutEntries[4].binding = 4;
    layoutEntries[4].visibility = WGPUShaderStage_Fragment;
    layoutEntries[4].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    layoutEntries[4].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Sampler
    layoutEntries[5].binding = 5;
    layoutEntries[5].visibility = WGPUShaderStage_Fragment;
    layoutEntries[5].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 6;
    layoutDesc.entries = layoutEntries;
    m_upsampleBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_upsampleBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Color target - use RGBA16Float to match the chain's HDR format
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA16Float;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_upsamplePipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void VolumetricLighting::ensureLowResTextures(Context& ctx, int lowW, int lowH) {
    // Check if textures need to be recreated
    if (m_lowResWidth == lowW && m_lowResHeight == lowH && m_lowResTexture && m_lowResDepthTexture) {
        return;
    }

    // Clean up old textures
    cleanupLowResTextures();

    m_lowResWidth = lowW;
    m_lowResHeight = lowH;

    WGPUDevice device = ctx.device();

    // Create low-res volumetric texture (RGBA16Float for HDR)
    WGPUTextureDescriptor texDesc = {};
    texDesc.size.width = lowW;
    texDesc.size.height = lowH;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA16Float;
    texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    m_lowResTexture = wgpuDeviceCreateTexture(device, &texDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA16Float;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    m_lowResView = wgpuTextureCreateView(m_lowResTexture, &viewDesc);

    // Create low-res depth texture (R32Float - unfilterable, so we copy depth values)
    texDesc.format = WGPUTextureFormat_R32Float;
    m_lowResDepthTexture = wgpuDeviceCreateTexture(device, &texDesc);

    viewDesc.format = WGPUTextureFormat_R32Float;
    m_lowResDepthView = wgpuTextureCreateView(m_lowResDepthTexture, &viewDesc);
}

void VolumetricLighting::cleanupLowResTextures() {
    if (m_lowResView) {
        wgpuTextureViewRelease(m_lowResView);
        m_lowResView = nullptr;
    }
    if (m_lowResTexture) {
        wgpuTextureRelease(m_lowResTexture);
        m_lowResTexture = nullptr;
    }
    if (m_lowResDepthView) {
        wgpuTextureViewRelease(m_lowResDepthView);
        m_lowResDepthView = nullptr;
    }
    if (m_lowResDepthTexture) {
        wgpuTextureRelease(m_lowResDepthTexture);
        m_lowResDepthTexture = nullptr;
    }
    m_lowResWidth = 0;
    m_lowResHeight = 0;
}

void VolumetricLighting::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    // Match input resolution
    matchInputResolution(0);

    if (!m_render3d || !m_render3d->hasDepthOutput()) {
        // No depth output available - can't apply volumetric lighting
        return;
    }

    if (!needsCook()) return;

    int scale = static_cast<int>(resolutionScale);

    if (scale <= 1) {
        // Full resolution - single pass
        renderFullRes(ctx);
    } else {
        // Half/quarter resolution - two-pass
        int lowW = std::max(1, m_width / scale);
        int lowH = std::max(1, m_height / scale);

        // Create upsample pipeline if not yet created
        if (!m_upsamplePipeline) {
            createUpsamplePipeline(ctx);
        }

        ensureLowResTextures(ctx, lowW, lowH);

        // Pass 1: Low-res volumetric
        renderLowRes(ctx);

        // Pass 2: Upsample + composite
        renderUpsample(ctx);
    }

    didCook();
}

void VolumetricLighting::renderFullRes(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Get input textures
    WGPUTextureView colorView = m_render3d->outputView();
    WGPUTextureView depthView = m_render3d->depthOutputView();

    if (!colorView || !depthView) return;

    // Get camera data
    float nearPlane = m_render3d->getNearPlane();
    float farPlane = m_render3d->getFarPlane();

    // Get camera matrices from the camera operator
    glm::mat4 invViewProj = glm::mat4(1.0f);
    glm::vec3 cameraPos = glm::vec3(0, 2, 5);

    if (m_cameraOp) {
        const Camera3D& cam = m_cameraOp->outputCamera();
        glm::mat4 viewProj = cam.viewProjectionMatrix();
        invViewProj = glm::inverse(viewProj);
        cameraPos = cam.getPosition();
    }

    // Get light data
    LightData lightData;
    if (m_lightOp) {
        lightData = m_lightOp->outputLight();
    }

    // Update uniforms
    VolumetricUniforms uniforms = {};

    // Camera
    memcpy(uniforms.invViewProj, &invViewProj[0][0], sizeof(float) * 16);
    uniforms.cameraPosX = cameraPos.x;
    uniforms.cameraPosY = cameraPos.y;
    uniforms.cameraPosZ = cameraPos.z;
    uniforms.nearPlane = nearPlane;
    uniforms.farPlane = farPlane;

    // Light
    uniforms.lightType = static_cast<int>(lightData.type);
    uniforms.lightPosX = lightData.position.x;
    uniforms.lightPosY = lightData.position.y;
    uniforms.lightPosZ = lightData.position.z;
    uniforms.lightDirX = lightData.direction.x;
    uniforms.lightDirY = lightData.direction.y;
    uniforms.lightDirZ = lightData.direction.z;
    uniforms.lightColorR = lightData.color.r;
    uniforms.lightColorG = lightData.color.g;
    uniforms.lightColorB = lightData.color.b;
    uniforms.lightIntensity = lightData.intensity;
    uniforms.lightRange = lightData.range;
    uniforms.spotAngle = std::cos(glm::radians(lightData.spotAngle));
    uniforms.spotBlend = lightData.spotBlend;

    // Volumetric params
    uniforms.raySteps = static_cast<int>(raySteps);
    uniforms.maxDistance = static_cast<float>(maxDistance);
    uniforms.density = static_cast<float>(density);
    uniforms.intensity = static_cast<float>(intensity);
    uniforms.anisotropy = static_cast<float>(anisotropy);
    uniforms.fogColorR = fogColor[0];
    uniforms.fogColorG = fogColor[1];
    uniforms.fogColorB = fogColor[2];
    uniforms.debugMode = static_cast<int>(debugMode);

    // Shadow data
    WGPUTextureView shadowMapView = m_render3d->getShadowMapView();
    bool shadowsAvailable = static_cast<bool>(useShadows) && m_render3d->hasShadows() && shadowMapView != nullptr;

    if (shadowsAvailable) {
        const glm::mat4& lightViewProj = m_render3d->getLightViewProjection();
        memcpy(uniforms.lightViewProj, &lightViewProj[0][0], sizeof(float) * 16);
        uniforms.shadowBias = static_cast<float>(shadowBias);
        uniforms.shadowStrength = static_cast<float>(shadowStrength);
        uniforms.useShadows = 1;
    } else {
        glm::mat4 identity(1.0f);
        memcpy(uniforms.lightViewProj, &identity[0][0], sizeof(float) * 16);
        uniforms.shadowBias = 0.0f;
        uniforms.shadowStrength = 0.0f;
        uniforms.useShadows = 0;
    }

    // Output mode: composite (full-res path)
    uniforms.outputMode = 0;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUTextureView shadowView = shadowsAvailable ? shadowMapView : m_dummyShadowView;

    // Create bind group
    WGPUBindGroupEntry bindEntries[6] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(VolumetricUniforms);
    bindEntries[1].binding = 1;
    bindEntries[1].textureView = colorView;
    bindEntries[2].binding = 2;
    bindEntries[2].textureView = depthView;
    bindEntries[3].binding = 3;
    bindEntries[3].sampler = m_sampler;
    bindEntries[4].binding = 4;
    bindEntries[4].textureView = shadowView;
    bindEntries[5].binding = 5;
    bindEntries[5].sampler = m_shadowSampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 6;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Render volumetric lighting
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0, 0, 0, 1};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    wgpuBindGroupRelease(bindGroup);
}

void VolumetricLighting::renderLowRes(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Get input textures
    WGPUTextureView colorView = m_render3d->outputView();
    WGPUTextureView depthView = m_render3d->depthOutputView();

    if (!colorView || !depthView) return;

    // Get camera data
    float nearPlane = m_render3d->getNearPlane();
    float farPlane = m_render3d->getFarPlane();

    glm::mat4 invViewProj = glm::mat4(1.0f);
    glm::vec3 cameraPos = glm::vec3(0, 2, 5);

    if (m_cameraOp) {
        const Camera3D& cam = m_cameraOp->outputCamera();
        glm::mat4 viewProj = cam.viewProjectionMatrix();
        invViewProj = glm::inverse(viewProj);
        cameraPos = cam.getPosition();
    }

    LightData lightData;
    if (m_lightOp) {
        lightData = m_lightOp->outputLight();
    }

    // Update uniforms
    VolumetricUniforms uniforms = {};

    // Camera
    memcpy(uniforms.invViewProj, &invViewProj[0][0], sizeof(float) * 16);
    uniforms.cameraPosX = cameraPos.x;
    uniforms.cameraPosY = cameraPos.y;
    uniforms.cameraPosZ = cameraPos.z;
    uniforms.nearPlane = nearPlane;
    uniforms.farPlane = farPlane;

    // Light
    uniforms.lightType = static_cast<int>(lightData.type);
    uniforms.lightPosX = lightData.position.x;
    uniforms.lightPosY = lightData.position.y;
    uniforms.lightPosZ = lightData.position.z;
    uniforms.lightDirX = lightData.direction.x;
    uniforms.lightDirY = lightData.direction.y;
    uniforms.lightDirZ = lightData.direction.z;
    uniforms.lightColorR = lightData.color.r;
    uniforms.lightColorG = lightData.color.g;
    uniforms.lightColorB = lightData.color.b;
    uniforms.lightIntensity = lightData.intensity;
    uniforms.lightRange = lightData.range;
    uniforms.spotAngle = std::cos(glm::radians(lightData.spotAngle));
    uniforms.spotBlend = lightData.spotBlend;

    // Volumetric params
    uniforms.raySteps = static_cast<int>(raySteps);
    uniforms.maxDistance = static_cast<float>(maxDistance);
    uniforms.density = static_cast<float>(density);
    uniforms.intensity = static_cast<float>(intensity);
    uniforms.anisotropy = static_cast<float>(anisotropy);
    uniforms.fogColorR = fogColor[0];
    uniforms.fogColorG = fogColor[1];
    uniforms.fogColorB = fogColor[2];
    uniforms.debugMode = static_cast<int>(debugMode);

    // Shadow data
    WGPUTextureView shadowMapView = m_render3d->getShadowMapView();
    bool shadowsAvailable = static_cast<bool>(useShadows) && m_render3d->hasShadows() && shadowMapView != nullptr;

    if (shadowsAvailable) {
        const glm::mat4& lightViewProj = m_render3d->getLightViewProjection();
        memcpy(uniforms.lightViewProj, &lightViewProj[0][0], sizeof(float) * 16);
        uniforms.shadowBias = static_cast<float>(shadowBias);
        uniforms.shadowStrength = static_cast<float>(shadowStrength);
        uniforms.useShadows = 1;
    } else {
        glm::mat4 identity(1.0f);
        memcpy(uniforms.lightViewProj, &identity[0][0], sizeof(float) * 16);
        uniforms.shadowBias = 0.0f;
        uniforms.shadowStrength = 0.0f;
        uniforms.useShadows = 0;
    }

    // Output mode: volumetric only (low-res pass)
    uniforms.outputMode = 1;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUTextureView shadowView = shadowsAvailable ? shadowMapView : m_dummyShadowView;

    // Create bind group
    WGPUBindGroupEntry bindEntries[6] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(VolumetricUniforms);
    bindEntries[1].binding = 1;
    bindEntries[1].textureView = colorView;
    bindEntries[2].binding = 2;
    bindEntries[2].textureView = depthView;
    bindEntries[3].binding = 3;
    bindEntries[3].sampler = m_sampler;
    bindEntries[4].binding = 4;
    bindEntries[4].textureView = shadowView;
    bindEntries[5].binding = 5;
    bindEntries[5].sampler = m_shadowSampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 6;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Render to low-res texture
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_lowResView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0, 0, 0, 1};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetViewport(pass, 0, 0, static_cast<float>(m_lowResWidth), static_cast<float>(m_lowResHeight), 0, 1);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    wgpuBindGroupRelease(bindGroup);
}

void VolumetricLighting::renderUpsample(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Get input textures
    WGPUTextureView colorView = m_render3d->outputView();
    WGPUTextureView depthView = m_render3d->depthOutputView();

    if (!colorView || !depthView) return;

    // Update upsample uniforms
    UpsampleUniforms uniforms = {};
    uniforms.lowResWidth = static_cast<float>(m_lowResWidth);
    uniforms.lowResHeight = static_cast<float>(m_lowResHeight);
    uniforms.fullResWidth = static_cast<float>(m_width);
    uniforms.fullResHeight = static_cast<float>(m_height);
    uniforms.depthWeight = 100.0f;  // High weight for depth-aware filtering

    wgpuQueueWriteBuffer(ctx.queue(), m_upsampleUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group
    WGPUBindGroupEntry bindEntries[6] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_upsampleUniformBuffer;
    bindEntries[0].size = sizeof(UpsampleUniforms);
    bindEntries[1].binding = 1;
    bindEntries[1].textureView = m_lowResView;
    bindEntries[2].binding = 2;
    bindEntries[2].textureView = m_lowResDepthView;
    bindEntries[3].binding = 3;
    bindEntries[3].textureView = colorView;
    bindEntries[4].binding = 4;
    bindEntries[4].textureView = depthView;
    bindEntries[5].binding = 5;
    bindEntries[5].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_upsampleBindGroupLayout;
    bindDesc.entryCount = 6;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Render to output texture
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0, 0, 0, 1};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, m_upsamplePipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    wgpuBindGroupRelease(bindGroup);
}

void VolumetricLighting::cleanup() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_uniformBuffer) {
        wgpuBufferRelease(m_uniformBuffer);
        m_uniformBuffer = nullptr;
    }
    if (m_sampler) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }
    if (m_shadowSampler) {
        wgpuSamplerRelease(m_shadowSampler);
        m_shadowSampler = nullptr;
    }
    if (m_dummyShadowView) {
        wgpuTextureViewRelease(m_dummyShadowView);
        m_dummyShadowView = nullptr;
    }
    if (m_dummyShadowTexture) {
        wgpuTextureRelease(m_dummyShadowTexture);
        m_dummyShadowTexture = nullptr;
    }

    // Clean up upsample pipeline resources (Phase 3)
    if (m_upsamplePipeline) {
        wgpuRenderPipelineRelease(m_upsamplePipeline);
        m_upsamplePipeline = nullptr;
    }
    if (m_upsampleBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_upsampleBindGroupLayout);
        m_upsampleBindGroupLayout = nullptr;
    }
    if (m_upsampleUniformBuffer) {
        wgpuBufferRelease(m_upsampleUniformBuffer);
        m_upsampleUniformBuffer = nullptr;
    }

    // Clean up low-res textures
    cleanupLowResTextures();

    releaseOutput();
    m_initialized = false;
}

std::vector<ParamDecl> VolumetricLighting::params() {
    return {
        raySteps.decl(),
        maxDistance.decl(),
        density.decl(),
        intensity.decl(),
        anisotropy.decl(),
        useShadows.decl(),
        shadowBias.decl(),
        shadowStrength.decl(),
        debugMode.decl(),
        resolutionScale.decl()
    };
}

bool VolumetricLighting::getParam(const std::string& name, float out[4]) {
    if (name == "raySteps") { out[0] = static_cast<float>(static_cast<int>(raySteps)); return true; }
    if (name == "maxDistance") { out[0] = static_cast<float>(maxDistance); return true; }
    if (name == "density") { out[0] = static_cast<float>(density); return true; }
    if (name == "intensity") { out[0] = static_cast<float>(intensity); return true; }
    if (name == "anisotropy") { out[0] = static_cast<float>(anisotropy); return true; }
    if (name == "fogColorR") { out[0] = fogColor[0]; return true; }
    if (name == "fogColorG") { out[0] = fogColor[1]; return true; }
    if (name == "fogColorB") { out[0] = fogColor[2]; return true; }
    if (name == "useShadows") { out[0] = static_cast<bool>(useShadows) ? 1.0f : 0.0f; return true; }
    if (name == "shadowBias") { out[0] = static_cast<float>(shadowBias); return true; }
    if (name == "shadowStrength") { out[0] = static_cast<float>(shadowStrength); return true; }
    if (name == "debugMode") { out[0] = static_cast<float>(static_cast<int>(debugMode)); return true; }
    if (name == "resolutionScale") { out[0] = static_cast<float>(static_cast<int>(resolutionScale)); return true; }
    return false;
}

bool VolumetricLighting::setParam(const std::string& name, const float value[4]) {
    if (name == "raySteps") { raySteps = static_cast<int>(value[0]); markDirty(); return true; }
    if (name == "maxDistance") { maxDistance = value[0]; markDirty(); return true; }
    if (name == "density") { density = value[0]; markDirty(); return true; }
    if (name == "intensity") { intensity = value[0]; markDirty(); return true; }
    if (name == "anisotropy") { anisotropy = value[0]; markDirty(); return true; }
    if (name == "fogColorR") { fogColor[0] = value[0]; markDirty(); return true; }
    if (name == "fogColorG") { fogColor[1] = value[0]; markDirty(); return true; }
    if (name == "fogColorB") { fogColor[2] = value[0]; markDirty(); return true; }
    if (name == "useShadows") { useShadows = value[0] > 0.5f; markDirty(); return true; }
    if (name == "shadowBias") { shadowBias = value[0]; markDirty(); return true; }
    if (name == "shadowStrength") { shadowStrength = value[0]; markDirty(); return true; }
    if (name == "debugMode") { debugMode = static_cast<int>(value[0]); markDirty(); return true; }
    if (name == "resolutionScale") { resolutionScale = static_cast<int>(value[0]); markDirty(); return true; }
    return false;
}

} // namespace vivid::render3d
