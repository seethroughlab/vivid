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
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var colorTexture: texture_2d<f32>;
@group(0) @binding(2) var depthTexture: texture_2d<f32>;
@group(0) @binding(3) var texSampler: sampler;

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

        // Beer-Lambert extinction
        let extinction = uniforms.density * stepSize;
        let sampleTransmittance = exp(-extinction);

        // Accumulate in-scattered light
        scatteredLight += lightContrib * transmittance * (1.0 - sampleTransmittance);
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

    // Blend with scene color (additive for god rays)
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
};

} // namespace

VolumetricLighting::VolumetricLighting() {
    registerParam(raySteps);
    registerParam(maxDistance);
    registerParam(density);
    registerParam(intensity);
    registerParam(anisotropy);
    registerParam(debugMode);
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

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(VolumetricUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[4] = {};

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

    // Sampler
    layoutEntries[3].binding = 3;
    layoutEntries[3].visibility = WGPUShaderStage_Fragment;
    layoutEntries[3].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 4;
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

void VolumetricLighting::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    // Match input resolution
    matchInputResolution(0);

    if (!m_render3d || !m_render3d->hasDepthOutput()) {
        // No depth output available - can't apply volumetric lighting
        return;
    }

    if (!needsCook()) return;

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

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group
    WGPUBindGroupEntry bindEntries[4] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(VolumetricUniforms);

    bindEntries[1].binding = 1;
    bindEntries[1].textureView = colorView;

    bindEntries[2].binding = 2;
    bindEntries[2].textureView = depthView;

    bindEntries[3].binding = 3;
    bindEntries[3].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 4;
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

    didCook();
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
        debugMode.decl()
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
    if (name == "debugMode") { out[0] = static_cast<float>(static_cast<int>(debugMode)); return true; }
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
    if (name == "debugMode") { debugMode = static_cast<int>(value[0]); markDirty(); return true; }
    return false;
}

} // namespace vivid::render3d
