#include <vivid/render3d/shadow_manager.h>
#include <vivid/render3d/light_operators.h>
#include <vivid/render3d/scene.h>
#include <vivid/render3d/mesh.h>
#include <vivid/context.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <cstring>
#include <algorithm>

namespace vivid::render3d {

// Helper to create WebGPU string views
static inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

// Shadow pass shader (depth only)
static const char* SHADOW_SHADER_SOURCE = R"(
struct ShadowUniforms {
    lightViewProj: mat4x4f,
    model: mat4x4f,
}
@group(0) @binding(0) var<uniform> uniforms: ShadowUniforms;
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
}
@vertex
fn vs_main(in: VertexInput) -> @builtin(position) vec4f {
    return uniforms.lightViewProj * uniforms.model * vec4f(in.position, 1.0);
}
@fragment
fn fs_main() {}
)";

// Point shadow pass shader (outputs linear depth)
static const char* POINT_SHADOW_SHADER_SOURCE = R"(
struct PointShadowUniforms {
    lightViewProj: mat4x4f,
    model: mat4x4f,
    lightPosAndFarPlane: vec4f,  // xyz = position, w = farPlane
}
@group(0) @binding(0) var<uniform> uniforms: PointShadowUniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPos: vec3f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos = (uniforms.model * vec4f(in.position, 1.0)).xyz;
    out.worldPos = worldPos;
    out.position = uniforms.lightViewProj * vec4f(worldPos, 1.0);
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) f32 {
    // Output linear distance from light, normalized to [0,1]
    let dist = length(in.worldPos - uniforms.lightPosAndFarPlane.xyz);
    return dist / uniforms.lightPosAndFarPlane.w;
}
)";

// Constants
static constexpr size_t SHADOW_UNIFORM_SIZE = 128;  // 2x mat4
static constexpr size_t SHADOW_UNIFORM_ALIGNMENT = 256;
static constexpr size_t MAX_SHADOW_OBJECTS = 64;
static constexpr size_t POINT_SHADOW_UNIFORM_SIZE = 144;  // 2x mat4 + vec4

ShadowManager::ShadowManager() = default;

ShadowManager::~ShadowManager() {
    destroyShadowResources();
}

void ShadowManager::setShadows(bool enabled) {
    m_shadowsEnabled = enabled;
}

void ShadowManager::setShadowMapResolution(int size) {
    int clamped = std::max(256, std::min(4096, size));
    if (m_shadowMapResolution != clamped) {
        m_shadowMapResolution = clamped;
        destroyShadowResources();
    }
}

// -------------------------------------------------------------------------
// Resource Initialization (base resources needed even when shadows disabled)
// -------------------------------------------------------------------------

void ShadowManager::initializeBaseResources(Context& ctx) {
    if (m_shadowSampleUniformBuffer) return;  // Already initialized

    WGPUDevice device = ctx.device();

    // Create shadow sample uniform buffer (for shadow data in main pass)
    // ShadowUniforms: mat4 (64) + bias (4) + mapSize (4) + enabled (4) + pointEnabled (4) + pointPos (12) + pointRange (4) = 96 bytes
    WGPUBufferDescriptor shadowSampleBufDesc = {};
    shadowSampleBufDesc.label = toStringView("Shadow Sample Uniforms");
    shadowSampleBufDesc.size = 96;
    shadowSampleBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_shadowSampleUniformBuffer = wgpuDeviceCreateBuffer(device, &shadowSampleBufDesc);

    // Create dummy 1x1 depth texture for when 2D shadows are disabled
    WGPUTextureDescriptor dummyDepthDesc = {};
    dummyDepthDesc.label = toStringView("Dummy Shadow Map");
    dummyDepthDesc.size = {1, 1, 1};
    dummyDepthDesc.mipLevelCount = 1;
    dummyDepthDesc.sampleCount = 1;
    dummyDepthDesc.dimension = WGPUTextureDimension_2D;
    dummyDepthDesc.format = WGPUTextureFormat_Depth32Float;
    dummyDepthDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    m_dummyShadowTexture = wgpuDeviceCreateTexture(device, &dummyDepthDesc);

    WGPUTextureViewDescriptor dummyViewDesc = {};
    dummyViewDesc.format = WGPUTextureFormat_Depth32Float;
    dummyViewDesc.dimension = WGPUTextureViewDimension_2D;
    dummyViewDesc.mipLevelCount = 1;
    dummyViewDesc.arrayLayerCount = 1;
    m_dummyShadowView = wgpuTextureCreateView(m_dummyShadowTexture, &dummyViewDesc);

    // Create 6 dummy 1x1 R32Float textures for when point shadows are disabled
    for (int i = 0; i < 6; i++) {
        WGPUTextureDescriptor dummyPointDepthDesc = {};
        dummyPointDepthDesc.label = toStringView("Dummy Point Shadow Face");
        dummyPointDepthDesc.size = {1, 1, 1};
        dummyPointDepthDesc.mipLevelCount = 1;
        dummyPointDepthDesc.sampleCount = 1;
        dummyPointDepthDesc.dimension = WGPUTextureDimension_2D;
        dummyPointDepthDesc.format = WGPUTextureFormat_R32Float;
        dummyPointDepthDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
        m_dummyPointShadowTextures[i] = wgpuDeviceCreateTexture(device, &dummyPointDepthDesc);

        WGPUTextureViewDescriptor dummyPointViewDesc = {};
        dummyPointViewDesc.format = WGPUTextureFormat_R32Float;
        dummyPointViewDesc.dimension = WGPUTextureViewDimension_2D;
        dummyPointViewDesc.mipLevelCount = 1;
        dummyPointViewDesc.arrayLayerCount = 1;
        m_dummyPointShadowViews[i] = wgpuTextureCreateView(m_dummyPointShadowTextures[i], &dummyPointViewDesc);
    }

    // Create comparison sampler for shadow mapping (for 2D directional/spot shadows)
    WGPUSamplerDescriptor shadowSamplerDesc = {};
    shadowSamplerDesc.label = toStringView("Shadow Comparison Sampler");
    shadowSamplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    shadowSamplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    shadowSamplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    shadowSamplerDesc.magFilter = WGPUFilterMode_Nearest;
    shadowSamplerDesc.minFilter = WGPUFilterMode_Nearest;
    shadowSamplerDesc.compare = WGPUCompareFunction_LessEqual;
    shadowSamplerDesc.maxAnisotropy = 1;
    m_shadowSampler = wgpuDeviceCreateSampler(device, &shadowSamplerDesc);

    // Create regular sampler for point shadow (manual comparison in shader)
    WGPUSamplerDescriptor pointShadowSamplerDesc = {};
    pointShadowSamplerDesc.label = toStringView("Point Shadow Regular Sampler");
    pointShadowSamplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    pointShadowSamplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    pointShadowSamplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    pointShadowSamplerDesc.magFilter = WGPUFilterMode_Nearest;
    pointShadowSamplerDesc.minFilter = WGPUFilterMode_Nearest;
    pointShadowSamplerDesc.maxAnisotropy = 1;
    m_pointShadowSampler = wgpuDeviceCreateSampler(device, &pointShadowSamplerDesc);

    // Create shadow sample bind group layout (10 bindings)
    WGPUBindGroupLayoutEntry shadowSampleLayoutEntries[10] = {};

    // Binding 0: Shadow sample uniforms
    shadowSampleLayoutEntries[0].binding = 0;
    shadowSampleLayoutEntries[0].visibility = WGPUShaderStage_Fragment;
    shadowSampleLayoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    shadowSampleLayoutEntries[0].buffer.minBindingSize = 96;

    // Binding 1: Shadow map texture (depth comparison)
    shadowSampleLayoutEntries[1].binding = 1;
    shadowSampleLayoutEntries[1].visibility = WGPUShaderStage_Fragment;
    shadowSampleLayoutEntries[1].texture.sampleType = WGPUTextureSampleType_Depth;
    shadowSampleLayoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Binding 2: Shadow comparison sampler
    shadowSampleLayoutEntries[2].binding = 2;
    shadowSampleLayoutEntries[2].visibility = WGPUShaderStage_Fragment;
    shadowSampleLayoutEntries[2].sampler.type = WGPUSamplerBindingType_Comparison;

    // Bindings 3-8: Point shadow face textures (6 separate 2D textures)
    for (int i = 0; i < 6; i++) {
        shadowSampleLayoutEntries[3 + i].binding = 3 + i;
        shadowSampleLayoutEntries[3 + i].visibility = WGPUShaderStage_Fragment;
        shadowSampleLayoutEntries[3 + i].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        shadowSampleLayoutEntries[3 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }

    // Binding 9: Point shadow sampler (regular sampler, manual comparison)
    shadowSampleLayoutEntries[9].binding = 9;
    shadowSampleLayoutEntries[9].visibility = WGPUShaderStage_Fragment;
    shadowSampleLayoutEntries[9].sampler.type = WGPUSamplerBindingType_NonFiltering;

    WGPUBindGroupLayoutDescriptor shadowSampleLayoutDesc = {};
    shadowSampleLayoutDesc.entryCount = 10;
    shadowSampleLayoutDesc.entries = shadowSampleLayoutEntries;
    m_shadowSampleBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &shadowSampleLayoutDesc);

    // Initialize shadow uniforms with shadows disabled
    struct ShadowSampleUniforms {
        float lightViewProj[16];
        float shadowBias;
        float shadowMapSize;
        uint32_t shadowEnabled;
        uint32_t pointShadowEnabled;
        float pointLightPosAndRange[4];
    } defaultShadowUniforms = {};
    defaultShadowUniforms.shadowEnabled = 0;
    defaultShadowUniforms.pointShadowEnabled = 0;
    defaultShadowUniforms.shadowBias = 0.001f;
    defaultShadowUniforms.shadowMapSize = 1.0f;
    defaultShadowUniforms.pointLightPosAndRange[3] = 50.0f;
    // Identity matrix for lightViewProj
    defaultShadowUniforms.lightViewProj[0] = 1.0f;
    defaultShadowUniforms.lightViewProj[5] = 1.0f;
    defaultShadowUniforms.lightViewProj[10] = 1.0f;
    defaultShadowUniforms.lightViewProj[15] = 1.0f;
    wgpuQueueWriteBuffer(ctx.queue(), m_shadowSampleUniformBuffer, 0, &defaultShadowUniforms, sizeof(defaultShadowUniforms));

    // Create default (disabled) shadow sample bind group
    WGPUBindGroupEntry shadowSampleBindEntries[10] = {};
    shadowSampleBindEntries[0].binding = 0;
    shadowSampleBindEntries[0].buffer = m_shadowSampleUniformBuffer;
    shadowSampleBindEntries[0].size = 96;

    shadowSampleBindEntries[1].binding = 1;
    shadowSampleBindEntries[1].textureView = m_dummyShadowView;

    shadowSampleBindEntries[2].binding = 2;
    shadowSampleBindEntries[2].sampler = m_shadowSampler;

    for (int i = 0; i < 6; i++) {
        shadowSampleBindEntries[3 + i].binding = 3 + i;
        shadowSampleBindEntries[3 + i].textureView = m_dummyPointShadowViews[i];
    }

    shadowSampleBindEntries[9].binding = 9;
    shadowSampleBindEntries[9].sampler = m_pointShadowSampler;

    WGPUBindGroupDescriptor shadowSampleBindDesc = {};
    shadowSampleBindDesc.layout = m_shadowSampleBindGroupLayout;
    shadowSampleBindDesc.entryCount = 10;
    shadowSampleBindDesc.entries = shadowSampleBindEntries;
    m_shadowSampleBindGroup = wgpuDeviceCreateBindGroup(device, &shadowSampleBindDesc);

    m_shadowBindGroupDirty = false;
}

// -------------------------------------------------------------------------
// Resource Creation
// -------------------------------------------------------------------------

void ShadowManager::createShadowResources(Context& ctx) {
    if (m_shadowMapTexture) return;  // Already created

    WGPUDevice device = ctx.device();

    // Create shadow map depth texture
    WGPUTextureDescriptor shadowTexDesc = {};
    shadowTexDesc.label = toStringView("Shadow Map");
    shadowTexDesc.size.width = m_shadowMapResolution;
    shadowTexDesc.size.height = m_shadowMapResolution;
    shadowTexDesc.size.depthOrArrayLayers = 1;
    shadowTexDesc.mipLevelCount = 1;
    shadowTexDesc.sampleCount = 1;
    shadowTexDesc.dimension = WGPUTextureDimension_2D;
    shadowTexDesc.format = WGPUTextureFormat_Depth32Float;
    shadowTexDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;

    m_shadowMapTexture = wgpuDeviceCreateTexture(device, &shadowTexDesc);

    WGPUTextureViewDescriptor shadowViewDesc = {};
    shadowViewDesc.format = WGPUTextureFormat_Depth32Float;
    shadowViewDesc.dimension = WGPUTextureViewDimension_2D;
    shadowViewDesc.mipLevelCount = 1;
    shadowViewDesc.arrayLayerCount = 1;
    m_shadowMapView = wgpuTextureCreateView(m_shadowMapTexture, &shadowViewDesc);

    // Note: m_shadowSampler is created in initializeBaseResources()

    // Create shadow uniform buffer
    WGPUBufferDescriptor shadowUniformDesc = {};
    shadowUniformDesc.label = toStringView("Shadow Pass Uniforms");
    shadowUniformDesc.size = SHADOW_UNIFORM_ALIGNMENT * MAX_SHADOW_OBJECTS;
    shadowUniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_shadowPassUniformBuffer = wgpuDeviceCreateBuffer(device, &shadowUniformDesc);

    // Create bind group layout for shadow pass (with dynamic offset)
    WGPUBindGroupLayoutEntry shadowLayoutEntry = {};
    shadowLayoutEntry.binding = 0;
    shadowLayoutEntry.visibility = WGPUShaderStage_Vertex;
    shadowLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    shadowLayoutEntry.buffer.hasDynamicOffset = true;
    shadowLayoutEntry.buffer.minBindingSize = SHADOW_UNIFORM_SIZE;

    WGPUBindGroupLayoutDescriptor shadowLayoutDesc = {};
    shadowLayoutDesc.entryCount = 1;
    shadowLayoutDesc.entries = &shadowLayoutEntry;
    m_shadowBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &shadowLayoutDesc);

    // Create shader module
    WGPUShaderSourceWGSL shadowWgsl = {};
    shadowWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    shadowWgsl.code = toStringView(SHADOW_SHADER_SOURCE);

    WGPUShaderModuleDescriptor shadowModuleDesc = {};
    shadowModuleDesc.nextInChain = &shadowWgsl.chain;
    WGPUShaderModule shadowModule = wgpuDeviceCreateShaderModule(device, &shadowModuleDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor shadowPipeLayoutDesc = {};
    shadowPipeLayoutDesc.bindGroupLayoutCount = 1;
    shadowPipeLayoutDesc.bindGroupLayouts = &m_shadowBindGroupLayout;
    WGPUPipelineLayout shadowPipeLayout = wgpuDeviceCreatePipelineLayout(device, &shadowPipeLayoutDesc);

    // Vertex attributes (same layout as main rendering)
    WGPUVertexAttribute attributes[5] = {};
    attributes[0].format = WGPUVertexFormat_Float32x3;  // position
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;  // normal
    attributes[1].offset = 12;
    attributes[1].shaderLocation = 1;
    attributes[2].format = WGPUVertexFormat_Float32x4;  // tangent
    attributes[2].offset = 24;
    attributes[2].shaderLocation = 2;
    attributes[3].format = WGPUVertexFormat_Float32x2;  // uv
    attributes[3].offset = 40;
    attributes[3].shaderLocation = 3;
    attributes[4].format = WGPUVertexFormat_Float32x4;  // color
    attributes[4].offset = 48;
    attributes[4].shaderLocation = 4;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(Vertex3D);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 5;
    vertexLayout.attributes = attributes;

    // Create depth-only render pipeline
    WGPURenderPipelineDescriptor shadowPipeDesc = {};
    shadowPipeDesc.label = toStringView("Shadow Pass Pipeline");
    shadowPipeDesc.layout = shadowPipeLayout;

    shadowPipeDesc.vertex.module = shadowModule;
    shadowPipeDesc.vertex.entryPoint = toStringView("vs_main");
    shadowPipeDesc.vertex.bufferCount = 1;
    shadowPipeDesc.vertex.buffers = &vertexLayout;

    // No color targets - depth only
    WGPUFragmentState fragState = {};
    fragState.module = shadowModule;
    fragState.entryPoint = toStringView("fs_main");
    fragState.targetCount = 0;
    shadowPipeDesc.fragment = &fragState;

    shadowPipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    shadowPipeDesc.primitive.cullMode = WGPUCullMode_None;
    shadowPipeDesc.primitive.frontFace = WGPUFrontFace_CCW;

    WGPUDepthStencilState depthState = {};
    depthState.format = WGPUTextureFormat_Depth32Float;
    depthState.depthWriteEnabled = WGPUOptionalBool_True;
    depthState.depthCompare = WGPUCompareFunction_Less;
    shadowPipeDesc.depthStencil = &depthState;

    shadowPipeDesc.multisample.count = 1;
    shadowPipeDesc.multisample.mask = ~0u;

    m_shadowPassPipeline = wgpuDeviceCreateRenderPipeline(device, &shadowPipeDesc);

    wgpuShaderModuleRelease(shadowModule);
    wgpuPipelineLayoutRelease(shadowPipeLayout);

    // Create cached bind group for shadow pass
    WGPUBindGroupEntry shadowPassEntry = {};
    shadowPassEntry.binding = 0;
    shadowPassEntry.buffer = m_shadowPassUniformBuffer;
    shadowPassEntry.size = SHADOW_UNIFORM_SIZE;

    WGPUBindGroupDescriptor shadowPassBindDesc = {};
    shadowPassBindDesc.layout = m_shadowBindGroupLayout;
    shadowPassBindDesc.entryCount = 1;
    shadowPassBindDesc.entries = &shadowPassEntry;
    m_shadowPassBindGroup = wgpuDeviceCreateBindGroup(device, &shadowPassBindDesc);

    m_shadowBindGroupDirty = true;
}

void ShadowManager::createPointShadowResources(Context& ctx) {
    if (m_pointShadowFaceTextures[0]) return;  // Already created

    WGPUDevice device = ctx.device();

    // WORKAROUND: Use 6 separate 2D textures instead of a 6-layer array texture
    // See: https://github.com/gfx-rs/wgpu/issues/1690
    static const char* faceNames[6] = {
        "Point Shadow +X", "Point Shadow -X",
        "Point Shadow +Y", "Point Shadow -Y",
        "Point Shadow +Z", "Point Shadow -Z"
    };

    for (int i = 0; i < 6; i++) {
        WGPUTextureDescriptor texDesc = {};
        texDesc.label = toStringView(faceNames[i]);
        texDesc.size.width = m_shadowMapResolution;
        texDesc.size.height = m_shadowMapResolution;
        texDesc.size.depthOrArrayLayers = 1;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_R32Float;  // Linear depth storage
        texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;

        m_pointShadowFaceTextures[i] = wgpuDeviceCreateTexture(device, &texDesc);

        WGPUTextureViewDescriptor viewDesc = {};
        viewDesc.format = WGPUTextureFormat_R32Float;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel = 0;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        m_pointShadowFaceViews[i] = wgpuTextureCreateView(m_pointShadowFaceTextures[i], &viewDesc);
    }

    // Create shared depth buffer for point shadow rendering
    WGPUTextureDescriptor depthTexDesc = {};
    depthTexDesc.label = toStringView("Point Shadow Depth Buffer");
    depthTexDesc.size.width = m_shadowMapResolution;
    depthTexDesc.size.height = m_shadowMapResolution;
    depthTexDesc.size.depthOrArrayLayers = 1;
    depthTexDesc.mipLevelCount = 1;
    depthTexDesc.sampleCount = 1;
    depthTexDesc.dimension = WGPUTextureDimension_2D;
    depthTexDesc.format = WGPUTextureFormat_Depth32Float;
    depthTexDesc.usage = WGPUTextureUsage_RenderAttachment;

    m_pointShadowDepthTexture = wgpuDeviceCreateTexture(device, &depthTexDesc);

    WGPUTextureViewDescriptor depthViewDesc = {};
    depthViewDesc.format = WGPUTextureFormat_Depth32Float;
    depthViewDesc.dimension = WGPUTextureViewDimension_2D;
    depthViewDesc.mipLevelCount = 1;
    depthViewDesc.arrayLayerCount = 1;
    m_pointShadowDepthView = wgpuTextureCreateView(m_pointShadowDepthTexture, &depthViewDesc);

    // Create uniform buffer for point shadow pass
    constexpr size_t POINT_SHADOW_UNIFORM_BUFFER_SIZE = 256 * 6 * MAX_SHADOW_OBJECTS;
    WGPUBufferDescriptor uniformBufDesc = {};
    uniformBufDesc.label = toStringView("Point Shadow Uniform Buffer");
    uniformBufDesc.size = POINT_SHADOW_UNIFORM_BUFFER_SIZE;
    uniformBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_pointShadowUniformBuffer = wgpuDeviceCreateBuffer(device, &uniformBufDesc);

    // Create shader module
    WGPUShaderSourceWGSL shaderWgsl = {};
    shaderWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
    shaderWgsl.code = toStringView(POINT_SHADOW_SHADER_SOURCE);

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &shaderWgsl.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    if (!shaderModule) {
        std::cerr << "[ShadowManager] ERROR: Failed to create point shadow shader module!" << std::endl;
        return;
    }

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.hasDynamicOffset = true;
    layoutEntry.buffer.minBindingSize = POINT_SHADOW_UNIFORM_SIZE;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    m_pointShadowBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipeLayoutDesc = {};
    pipeLayoutDesc.bindGroupLayoutCount = 1;
    pipeLayoutDesc.bindGroupLayouts = &m_pointShadowBindGroupLayout;
    WGPUPipelineLayout pipeLayout = wgpuDeviceCreatePipelineLayout(device, &pipeLayoutDesc);

    // Vertex attributes
    WGPUVertexAttribute attributes[5] = {};
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = 12;
    attributes[1].shaderLocation = 1;
    attributes[2].format = WGPUVertexFormat_Float32x4;
    attributes[2].offset = 24;
    attributes[2].shaderLocation = 2;
    attributes[3].format = WGPUVertexFormat_Float32x2;
    attributes[3].offset = 40;
    attributes[3].shaderLocation = 3;
    attributes[4].format = WGPUVertexFormat_Float32x4;
    attributes[4].offset = 48;
    attributes[4].shaderLocation = 4;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(Vertex3D);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 5;
    vertexLayout.attributes = attributes;

    // Color target (R32Float for linear depth)
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_R32Float;
    colorTarget.writeMask = WGPUColorWriteMask_Red;

    WGPUFragmentState fragState = {};
    fragState.module = shaderModule;
    fragState.entryPoint = toStringView("fs_main");
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPUDepthStencilState depthState = {};
    depthState.format = WGPUTextureFormat_Depth32Float;
    depthState.depthWriteEnabled = WGPUOptionalBool_True;
    depthState.depthCompare = WGPUCompareFunction_Less;

    WGPURenderPipelineDescriptor pipeDesc = {};
    pipeDesc.label = toStringView("Point Shadow Pipeline");
    pipeDesc.layout = pipeLayout;
    pipeDesc.vertex.module = shaderModule;
    pipeDesc.vertex.entryPoint = toStringView("vs_main");
    pipeDesc.vertex.bufferCount = 1;
    pipeDesc.vertex.buffers = &vertexLayout;
    pipeDesc.fragment = &fragState;
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.primitive.cullMode = WGPUCullMode_None;
    pipeDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipeDesc.depthStencil = &depthState;
    pipeDesc.multisample.count = 1;
    pipeDesc.multisample.mask = ~0u;

    m_pointShadowPipeline = wgpuDeviceCreateRenderPipeline(device, &pipeDesc);

    if (!m_pointShadowPipeline) {
        std::cerr << "[ShadowManager] ERROR: Failed to create point shadow pipeline!" << std::endl;
    }

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipeLayout);

    // Create cached bind group
    WGPUBindGroupEntry pointShadowEntry = {};
    pointShadowEntry.binding = 0;
    pointShadowEntry.buffer = m_pointShadowUniformBuffer;
    pointShadowEntry.size = POINT_SHADOW_UNIFORM_SIZE;

    WGPUBindGroupDescriptor pointShadowBindDesc = {};
    pointShadowBindDesc.layout = m_pointShadowBindGroupLayout;
    pointShadowBindDesc.entryCount = 1;
    pointShadowBindDesc.entries = &pointShadowEntry;
    m_pointShadowPassBindGroup = wgpuDeviceCreateBindGroup(device, &pointShadowBindDesc);

    // Note: m_pointShadowSampler is created in initializeBaseResources()

    m_shadowBindGroupDirty = true;
}

void ShadowManager::destroyShadowResources() {
    // Directional/spot shadow resources
    if (m_shadowPassPipeline) {
        wgpuRenderPipelineRelease(m_shadowPassPipeline);
        m_shadowPassPipeline = nullptr;
    }
    if (m_shadowPassBindGroup) {
        wgpuBindGroupRelease(m_shadowPassBindGroup);
        m_shadowPassBindGroup = nullptr;
    }
    if (m_shadowBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_shadowBindGroupLayout);
        m_shadowBindGroupLayout = nullptr;
    }
    if (m_shadowSampleBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_shadowSampleBindGroupLayout);
        m_shadowSampleBindGroupLayout = nullptr;
    }
    if (m_shadowSampleBindGroup) {
        wgpuBindGroupRelease(m_shadowSampleBindGroup);
        m_shadowSampleBindGroup = nullptr;
    }
    if (m_shadowPassUniformBuffer) {
        wgpuBufferRelease(m_shadowPassUniformBuffer);
        m_shadowPassUniformBuffer = nullptr;
    }
    if (m_shadowSampler) {
        wgpuSamplerRelease(m_shadowSampler);
        m_shadowSampler = nullptr;
    }
    if (m_shadowMapView) {
        wgpuTextureViewRelease(m_shadowMapView);
        m_shadowMapView = nullptr;
    }
    if (m_shadowMapTexture) {
        wgpuTextureDestroy(m_shadowMapTexture);
        wgpuTextureRelease(m_shadowMapTexture);
        m_shadowMapTexture = nullptr;
    }

    // Dummy shadow texture
    if (m_dummyShadowView) {
        wgpuTextureViewRelease(m_dummyShadowView);
        m_dummyShadowView = nullptr;
    }
    if (m_dummyShadowTexture) {
        wgpuTextureDestroy(m_dummyShadowTexture);
        wgpuTextureRelease(m_dummyShadowTexture);
        m_dummyShadowTexture = nullptr;
    }

    // Point light shadow resources
    if (m_pointShadowPipeline) {
        wgpuRenderPipelineRelease(m_pointShadowPipeline);
        m_pointShadowPipeline = nullptr;
    }
    if (m_pointShadowBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_pointShadowBindGroupLayout);
        m_pointShadowBindGroupLayout = nullptr;
    }
    if (m_pointShadowPassBindGroup) {
        wgpuBindGroupRelease(m_pointShadowPassBindGroup);
        m_pointShadowPassBindGroup = nullptr;
    }
    if (m_pointShadowSampler) {
        wgpuSamplerRelease(m_pointShadowSampler);
        m_pointShadowSampler = nullptr;
    }

    // 6 separate point shadow textures
    for (int i = 0; i < 6; i++) {
        if (m_pointShadowFaceViews[i]) {
            wgpuTextureViewRelease(m_pointShadowFaceViews[i]);
            m_pointShadowFaceViews[i] = nullptr;
        }
        if (m_pointShadowFaceTextures[i]) {
            wgpuTextureDestroy(m_pointShadowFaceTextures[i]);
            wgpuTextureRelease(m_pointShadowFaceTextures[i]);
            m_pointShadowFaceTextures[i] = nullptr;
        }
    }

    if (m_pointShadowDepthView) {
        wgpuTextureViewRelease(m_pointShadowDepthView);
        m_pointShadowDepthView = nullptr;
    }
    if (m_pointShadowDepthTexture) {
        wgpuTextureDestroy(m_pointShadowDepthTexture);
        wgpuTextureRelease(m_pointShadowDepthTexture);
        m_pointShadowDepthTexture = nullptr;
    }
    if (m_pointShadowSampleBindGroup) {
        wgpuBindGroupRelease(m_pointShadowSampleBindGroup);
        m_pointShadowSampleBindGroup = nullptr;
    }
    if (m_pointShadowUniformBuffer) {
        wgpuBufferRelease(m_pointShadowUniformBuffer);
        m_pointShadowUniformBuffer = nullptr;
    }

    // Dummy point shadow textures
    for (int i = 0; i < 6; i++) {
        if (m_dummyPointShadowViews[i]) {
            wgpuTextureViewRelease(m_dummyPointShadowViews[i]);
            m_dummyPointShadowViews[i] = nullptr;
        }
        if (m_dummyPointShadowTextures[i]) {
            wgpuTextureDestroy(m_dummyPointShadowTextures[i]);
            wgpuTextureRelease(m_dummyPointShadowTextures[i]);
            m_dummyPointShadowTextures[i] = nullptr;
        }
    }

    // Shadow sample uniform buffer (for main pass)
    if (m_shadowSampleUniformBuffer) {
        wgpuBufferRelease(m_shadowSampleUniformBuffer);
        m_shadowSampleUniformBuffer = nullptr;
    }

    m_shadowBindGroupDirty = true;
}

// -------------------------------------------------------------------------
// Rendering
// -------------------------------------------------------------------------

bool ShadowManager::renderShadowPass(Context& ctx, WGPUCommandEncoder encoder,
                                      const Scene& scene, const LightData& light) {
    if (!m_shadowPassPipeline || scene.empty()) return false;

    // Compute light-space matrix
    if (light.type == LightType::Directional) {
        m_lightViewProj = computeDirectionalLightMatrix(light, scene);
    } else if (light.type == LightType::Spot) {
        m_lightViewProj = computeSpotLightMatrix(light);
    } else {
        return false;  // Point lights use renderPointShadowPass
    }

    // Shadow uniform structure (must match shader)
    struct ShadowUniforms {
        float lightViewProj[16];
        float model[16];
    };

    const auto& objects = scene.objects();

    // Write uniforms for each shadow-casting object
    for (size_t i = 0; i < objects.size(); i++) {
        const auto& obj = objects[i];
        if (!obj.mesh || !obj.castShadow) continue;

        ShadowUniforms uniforms;
        memcpy(uniforms.lightViewProj, glm::value_ptr(m_lightViewProj), 64);
        memcpy(uniforms.model, glm::value_ptr(obj.transform), 64);

        size_t offset = i * SHADOW_UNIFORM_ALIGNMENT;
        wgpuQueueWriteBuffer(ctx.queue(), m_shadowPassUniformBuffer, offset, &uniforms, sizeof(uniforms));
    }

    // Begin shadow pass
    WGPURenderPassDepthStencilAttachment shadowDepthAttachment = {};
    shadowDepthAttachment.view = m_shadowMapView;
    shadowDepthAttachment.depthLoadOp = WGPULoadOp_Clear;
    shadowDepthAttachment.depthStoreOp = WGPUStoreOp_Store;
    shadowDepthAttachment.depthClearValue = 1.0f;

    WGPURenderPassDescriptor shadowPassDesc = {};
    shadowPassDesc.colorAttachmentCount = 0;
    shadowPassDesc.depthStencilAttachment = &shadowDepthAttachment;

    WGPURenderPassEncoder shadowPass = wgpuCommandEncoderBeginRenderPass(encoder, &shadowPassDesc);
    wgpuRenderPassEncoderSetPipeline(shadowPass, m_shadowPassPipeline);

    // Render each shadow-casting object
    for (size_t i = 0; i < objects.size(); i++) {
        const auto& obj = objects[i];
        if (!obj.mesh || !obj.castShadow) continue;

        uint32_t dynamicOffset = static_cast<uint32_t>(i * SHADOW_UNIFORM_ALIGNMENT);
        wgpuRenderPassEncoderSetBindGroup(shadowPass, 0, m_shadowPassBindGroup, 1, &dynamicOffset);

        wgpuRenderPassEncoderSetVertexBuffer(shadowPass, 0, obj.mesh->vertexBuffer(), 0,
                                              obj.mesh->vertexCount() * sizeof(Vertex3D));
        if (obj.mesh->indexBuffer()) {
            wgpuRenderPassEncoderSetIndexBuffer(shadowPass, obj.mesh->indexBuffer(),
                                                 WGPUIndexFormat_Uint32, 0,
                                                 obj.mesh->indexCount() * sizeof(uint32_t));
            wgpuRenderPassEncoderDrawIndexed(shadowPass, obj.mesh->indexCount(), 1, 0, 0, 0);
        } else {
            wgpuRenderPassEncoderDraw(shadowPass, obj.mesh->vertexCount(), 1, 0, 0);
        }
    }

    wgpuRenderPassEncoderEnd(shadowPass);
    wgpuRenderPassEncoderRelease(shadowPass);

    return true;
}

bool ShadowManager::renderPointShadowPass(Context& ctx, WGPUCommandEncoder encoder,
                                           const Scene& scene, const glm::vec3& lightPos, float range) {
    if (!m_pointShadowPipeline || scene.empty()) return false;

    const auto& objects = scene.objects();

    // Cache point light data for main shader
    m_pointLightPos = lightPos;
    m_pointLightRange = range;

    float nearPlane = 0.5f;
    float farPlane = range;

    // Point shadow uniform structure (must match shader)
    struct PointShadowUniforms {
        float lightViewProj[16];
        float model[16];
        float lightPosAndFarPlane[4];
    };

    // Render each of the 6 cube faces
    for (int face = 0; face < 6; face++) {
        glm::mat4 faceMatrix = computePointLightFaceMatrix(lightPos, face, nearPlane, farPlane);

        // Write uniforms for each object for this face
        for (size_t i = 0; i < objects.size(); i++) {
            const auto& obj = objects[i];
            if (!obj.mesh || !obj.castShadow) continue;

            PointShadowUniforms uniforms;
            memcpy(uniforms.lightViewProj, glm::value_ptr(faceMatrix), 64);
            memcpy(uniforms.model, glm::value_ptr(obj.transform), 64);
            uniforms.lightPosAndFarPlane[0] = lightPos.x;
            uniforms.lightPosAndFarPlane[1] = lightPos.y;
            uniforms.lightPosAndFarPlane[2] = lightPos.z;
            uniforms.lightPosAndFarPlane[3] = farPlane;

            size_t offset = (face * objects.size() + i) * SHADOW_UNIFORM_ALIGNMENT;
            wgpuQueueWriteBuffer(ctx.queue(), m_pointShadowUniformBuffer, offset, &uniforms, sizeof(uniforms));
        }

        if (!m_pointShadowFaceViews[face]) {
            std::cerr << "[ShadowManager] ERROR: Face " << face << " texture view is null!" << std::endl;
            continue;
        }

        // Color attachment (R32Float for linear depth)
        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = m_pointShadowFaceViews[face];
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {1.0f, 0.0f, 0.0f, 1.0f};

        // Depth attachment
        WGPURenderPassDepthStencilAttachment depthAttachment = {};
        depthAttachment.view = m_pointShadowDepthView;
        depthAttachment.depthLoadOp = WGPULoadOp_Clear;
        depthAttachment.depthStoreOp = WGPUStoreOp_Discard;
        depthAttachment.depthClearValue = 1.0f;

        WGPURenderPassDescriptor passDesc = {};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        passDesc.depthStencilAttachment = &depthAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        if (!pass) {
            std::cerr << "[ShadowManager] ERROR: Failed to begin render pass for face " << face << std::endl;
            continue;
        }
        wgpuRenderPassEncoderSetPipeline(pass, m_pointShadowPipeline);

        wgpuRenderPassEncoderSetViewport(pass, 0, 0,
                                         static_cast<float>(m_shadowMapResolution),
                                         static_cast<float>(m_shadowMapResolution),
                                         0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, m_shadowMapResolution, m_shadowMapResolution);

        // Render each object
        for (size_t i = 0; i < objects.size(); i++) {
            const auto& obj = objects[i];
            if (!obj.mesh || !obj.castShadow) continue;

            uint32_t dynamicOffset = static_cast<uint32_t>((face * objects.size() + i) * SHADOW_UNIFORM_ALIGNMENT);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, m_pointShadowPassBindGroup, 1, &dynamicOffset);

            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, obj.mesh->vertexBuffer(), 0,
                                                  obj.mesh->vertexCount() * sizeof(Vertex3D));
            if (obj.mesh->indexBuffer()) {
                wgpuRenderPassEncoderSetIndexBuffer(pass, obj.mesh->indexBuffer(),
                                                     WGPUIndexFormat_Uint32, 0,
                                                     obj.mesh->indexCount() * sizeof(uint32_t));
                wgpuRenderPassEncoderDrawIndexed(pass, obj.mesh->indexCount(), 1, 0, 0, 0);
            } else {
                wgpuRenderPassEncoderDraw(pass, obj.mesh->vertexCount(), 1, 0, 0);
            }
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    return true;
}

// -------------------------------------------------------------------------
// Bind Group Management
// -------------------------------------------------------------------------

void ShadowManager::updateShadowBindGroup(WGPUDevice device, bool hasDirShadow, bool hasPointShadow) {
    // Release old bind group
    if (m_shadowSampleBindGroup) {
        wgpuBindGroupRelease(m_shadowSampleBindGroup);
        m_shadowSampleBindGroup = nullptr;
    }

    // Create bind group entries
    WGPUBindGroupEntry entries[10] = {};

    // Binding 0: Shadow sample uniform buffer
    entries[0].binding = 0;
    entries[0].buffer = m_shadowSampleUniformBuffer;
    entries[0].size = 96;

    // Binding 1: Shadow map texture (directional/spot)
    entries[1].binding = 1;
    entries[1].textureView = hasDirShadow ? m_shadowMapView : m_dummyShadowView;

    // Binding 2: Shadow comparison sampler
    entries[2].binding = 2;
    entries[2].sampler = m_shadowSampler;

    // Bindings 3-8: Point shadow face textures
    for (int i = 0; i < 6; i++) {
        entries[3 + i].binding = 3 + i;
        entries[3 + i].textureView = hasPointShadow ? m_pointShadowFaceViews[i] : m_dummyPointShadowViews[i];
    }

    // Binding 9: Point shadow sampler
    entries[9].binding = 9;
    entries[9].sampler = m_pointShadowSampler;

    // Create bind group
    WGPUBindGroupDescriptor desc = {};
    desc.layout = m_shadowSampleBindGroupLayout;
    desc.entryCount = 10;
    desc.entries = entries;
    m_shadowSampleBindGroup = wgpuDeviceCreateBindGroup(device, &desc);

    m_shadowBindGroupDirty = false;
}

// -------------------------------------------------------------------------
// Light Matrix Computation
// -------------------------------------------------------------------------

glm::mat4 ShadowManager::computeDirectionalLightMatrix(const LightData& light, const Scene& scene) {
    glm::vec3 lightDir = glm::normalize(light.direction);

    // Find a suitable up vector
    glm::vec3 up = glm::vec3(0, 1, 0);
    if (std::abs(glm::dot(lightDir, up)) > 0.99f) {
        up = glm::vec3(0, 0, 1);
    }

    // Fixed frustum for predictable shadow coverage
    float frustumSize = 10.0f;
    glm::vec3 sceneCenter = glm::vec3(0, 0, 0);

    float lightDistance = 50.0f;
    glm::vec3 lightPos = sceneCenter - lightDir * lightDistance;

    glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, up);

    float nearPlane = 1.0f;
    float farPlane = lightDistance * 2.0f;

    glm::mat4 lightProj = glm::orthoRH_ZO(
        -frustumSize, frustumSize,
        -frustumSize, frustumSize,
        nearPlane, farPlane
    );

    return lightProj * lightView;
}

glm::mat4 ShadowManager::computeSpotLightMatrix(const LightData& light) {
    glm::vec3 lightDir = glm::normalize(light.direction);
    glm::vec3 lightPos = light.position;

    glm::vec3 up = glm::vec3(0, 1, 0);
    if (std::abs(glm::dot(lightDir, up)) > 0.99f) {
        up = glm::vec3(0, 0, 1);
    }

    glm::vec3 target = lightPos + lightDir;
    glm::mat4 lightView = glm::lookAt(lightPos, target, up);

    float fov = glm::radians(light.spotAngle * 2.0f);
    fov = glm::clamp(fov, glm::radians(10.0f), glm::radians(170.0f));

    float nearPlane = 0.5f;
    float farPlane = light.range > 0.0f ? light.range : 50.0f;

    glm::mat4 lightProj = glm::perspectiveRH_ZO(fov, 1.0f, nearPlane, farPlane);

    return lightProj * lightView;
}

glm::mat4 ShadowManager::computePointLightFaceMatrix(const glm::vec3& lightPos, int face,
                                                      float nearPlane, float farPlane) {
    // Cube face directions
    static const glm::vec3 directions[6] = {
        { 1.0f,  0.0f,  0.0f},  // +X
        {-1.0f,  0.0f,  0.0f},  // -X
        { 0.0f,  1.0f,  0.0f},  // +Y
        { 0.0f, -1.0f,  0.0f},  // -Y
        { 0.0f,  0.0f,  1.0f},  // +Z
        { 0.0f,  0.0f, -1.0f}   // -Z
    };
    // Standard cube map up vectors
    static const glm::vec3 ups[6] = {
        { 0.0f, -1.0f,  0.0f},  // +X
        { 0.0f, -1.0f,  0.0f},  // -X
        { 0.0f,  0.0f,  1.0f},  // +Y
        { 0.0f,  0.0f, -1.0f},  // -Y
        { 0.0f, -1.0f,  0.0f},  // +Z
        { 0.0f, -1.0f,  0.0f}   // -Z
    };

    glm::mat4 view = glm::lookAt(lightPos, lightPos + directions[face], ups[face]);
    glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, nearPlane, farPlane);

    return proj * view;
}

} // namespace vivid::render3d
