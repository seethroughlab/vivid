#include "shadow_map.h"
#include <vivid/graphics3d.h>
#include <iostream>
#include <cstring>
#include <functional>

namespace vivid {

// ============================================================================
// Shadow Map Depth-Only Shader
// ============================================================================

static const char* SHADOW_MAP_SHADER = R"(
// Shadow map depth-only shader
// Renders geometry from light's perspective, outputting only depth

struct LightMatrixUniform {
    lightViewProj: mat4x4f,
}

struct TransformUniform {
    model: mat4x4f,
}

@group(0) @binding(0) var<uniform> light: LightMatrixUniform;
@group(1) @binding(0) var<uniform> transform: TransformUniform;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) tangent: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos = transform.model * vec4f(in.position, 1.0);
    out.position = light.lightViewProj * worldPos;
    return out;
}

// Empty fragment shader - depth is written automatically
@fragment
fn fs_main() {
    // Depth-only pass, no color output needed
}
)";

// ============================================================================
// ShadowMap Implementation
// ============================================================================

ShadowMap::~ShadowMap() {
    destroy();
}

ShadowMap::ShadowMap(ShadowMap&& other) noexcept
    : renderer_(other.renderer_)
    , resolution_(other.resolution_)
    , depthTexture_(other.depthTexture_)
    , depthView_(other.depthView_)
{
    other.renderer_ = nullptr;
    other.depthTexture_ = nullptr;
    other.depthView_ = nullptr;
}

ShadowMap& ShadowMap::operator=(ShadowMap&& other) noexcept {
    if (this != &other) {
        destroy();
        renderer_ = other.renderer_;
        resolution_ = other.resolution_;
        depthTexture_ = other.depthTexture_;
        depthView_ = other.depthView_;
        other.renderer_ = nullptr;
        other.depthTexture_ = nullptr;
        other.depthView_ = nullptr;
    }
    return *this;
}

bool ShadowMap::init(Renderer& renderer, int resolution) {
    destroy();
    renderer_ = &renderer;
    resolution_ = resolution;

    // Create depth texture for shadow map
    WGPUTextureDescriptor depthDesc = {};
    depthDesc.size = {static_cast<uint32_t>(resolution), static_cast<uint32_t>(resolution), 1};
    depthDesc.format = WGPUTextureFormat_Depth32Float;
    depthDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    depthDesc.dimension = WGPUTextureDimension_2D;

    depthTexture_ = wgpuDeviceCreateTexture(renderer_->device(), &depthDesc);
    if (!depthTexture_) {
        std::cerr << "[ShadowMap] Failed to create depth texture\n";
        return false;
    }

    // Create depth texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_Depth32Float;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_DepthOnly;

    depthView_ = wgpuTextureCreateView(depthTexture_, &viewDesc);
    if (!depthView_) {
        std::cerr << "[ShadowMap] Failed to create depth view\n";
        destroy();
        return false;
    }

    return true;
}

void ShadowMap::destroy() {
    if (depthView_) {
        wgpuTextureViewRelease(depthView_);
        depthView_ = nullptr;
    }
    if (depthTexture_) {
        wgpuTextureDestroy(depthTexture_);
        wgpuTextureRelease(depthTexture_);
        depthTexture_ = nullptr;
    }
    renderer_ = nullptr;
    resolution_ = 0;
}

glm::mat4 ShadowMap::calcDirectionalLightMatrix(
    const glm::vec3& lightDir,
    const glm::vec3& sceneCenter,
    float sceneRadius
) {
    // Position light far enough back to see entire scene
    glm::vec3 lightPos = sceneCenter - glm::normalize(lightDir) * sceneRadius * 2.0f;

    // Choose an up vector that's not parallel to light direction
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(glm::normalize(lightDir), up)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    // View matrix: look from light position toward scene center
    glm::mat4 view = glm::lookAt(lightPos, sceneCenter, up);

    // Orthographic projection to cover the scene
    float size = sceneRadius * 1.5f;
    glm::mat4 proj = glm::ortho(-size, size, -size, size, 0.1f, sceneRadius * 4.0f);

    return proj * view;
}

glm::mat4 ShadowMap::calcSpotLightMatrix(
    const glm::vec3& position,
    const glm::vec3& direction,
    float outerAngle,
    float radius
) {
    // Choose an up vector that's not parallel to light direction
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(glm::normalize(direction), up)) > 0.99f) {
        up = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    // View matrix: look from light position along direction
    glm::mat4 view = glm::lookAt(position, position + direction, up);

    // Perspective projection with spotlight cone angle
    float fov = outerAngle * 2.0f;  // Full cone angle
    glm::mat4 proj = glm::perspective(fov, 1.0f, 0.1f, radius);

    return proj * view;
}

// ============================================================================
// ShadowMapPipeline Implementation
// ============================================================================

ShadowMapPipeline::~ShadowMapPipeline() {
    destroy();
}

bool ShadowMapPipeline::init(Renderer& renderer) {
    destroy();
    renderer_ = &renderer;

    if (!createPipeline()) {
        destroy();
        return false;
    }

    // Create light matrix buffer
    WGPUBufferDescriptor bufDesc = {};
    bufDesc.size = sizeof(glm::mat4);
    bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    lightMatrixBuffer_ = wgpuDeviceCreateBuffer(renderer_->device(), &bufDesc);

    // Create transform buffer
    bufDesc.size = sizeof(glm::mat4);
    transformBuffer_ = wgpuDeviceCreateBuffer(renderer_->device(), &bufDesc);

    // Create light matrix bind group
    WGPUBindGroupEntry entry = {};
    entry.binding = 0;
    entry.buffer = lightMatrixBuffer_;
    entry.size = sizeof(glm::mat4);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = lightMatrixLayout_;
    bgDesc.entryCount = 1;
    bgDesc.entries = &entry;
    lightMatrixBindGroup_ = wgpuDeviceCreateBindGroup(renderer_->device(), &bgDesc);

    std::cout << "[ShadowMapPipeline] Created successfully\n";
    return true;
}

void ShadowMapPipeline::destroy() {
    if (renderPass_) {
        wgpuRenderPassEncoderEnd(renderPass_);
        wgpuRenderPassEncoderRelease(renderPass_);
        renderPass_ = nullptr;
    }
    if (encoder_) {
        wgpuCommandEncoderRelease(encoder_);
        encoder_ = nullptr;
    }
    if (lightMatrixBindGroup_) {
        wgpuBindGroupRelease(lightMatrixBindGroup_);
        lightMatrixBindGroup_ = nullptr;
    }
    if (transformBuffer_) {
        wgpuBufferDestroy(transformBuffer_);
        wgpuBufferRelease(transformBuffer_);
        transformBuffer_ = nullptr;
    }
    if (lightMatrixBuffer_) {
        wgpuBufferDestroy(lightMatrixBuffer_);
        wgpuBufferRelease(lightMatrixBuffer_);
        lightMatrixBuffer_ = nullptr;
    }
    if (pipeline_) {
        wgpuRenderPipelineRelease(pipeline_);
        pipeline_ = nullptr;
    }
    if (pipelineLayout_) {
        wgpuPipelineLayoutRelease(pipelineLayout_);
        pipelineLayout_ = nullptr;
    }
    if (lightMatrixLayout_) {
        wgpuBindGroupLayoutRelease(lightMatrixLayout_);
        lightMatrixLayout_ = nullptr;
    }
    if (transformLayout_) {
        wgpuBindGroupLayoutRelease(transformLayout_);
        transformLayout_ = nullptr;
    }
    if (shaderModule_) {
        wgpuShaderModuleRelease(shaderModule_);
        shaderModule_ = nullptr;
    }
    renderer_ = nullptr;
}

bool ShadowMapPipeline::createPipeline() {
    // Create shader module
    WGPUShaderSourceWGSL wgslSource = {};
    wgslSource.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslSource.code = WGPUStringView{.data = SHADOW_MAP_SHADER, .length = strlen(SHADOW_MAP_SHADER)};

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslSource);

    shaderModule_ = wgpuDeviceCreateShaderModule(renderer_->device(), &shaderDesc);
    if (!shaderModule_) {
        std::cerr << "[ShadowMapPipeline] Failed to create shader module\n";
        return false;
    }

    // Create bind group layouts
    // Group 0: Light matrix
    {
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex;
        entry.buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        lightMatrixLayout_ = wgpuDeviceCreateBindGroupLayout(renderer_->device(), &layoutDesc);
    }

    // Group 1: Transform
    {
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex;
        entry.buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        transformLayout_ = wgpuDeviceCreateBindGroupLayout(renderer_->device(), &layoutDesc);
    }

    // Create pipeline layout
    WGPUBindGroupLayout layouts[] = {lightMatrixLayout_, transformLayout_};
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 2;
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(renderer_->device(), &pipelineLayoutDesc);

    // Vertex buffer layout (same as Vertex3D)
    WGPUVertexAttribute attributes[4] = {};
    // Position
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;
    // Normal
    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = sizeof(float) * 3;
    attributes[1].shaderLocation = 1;
    // UV
    attributes[2].format = WGPUVertexFormat_Float32x2;
    attributes[2].offset = sizeof(float) * 6;
    attributes[2].shaderLocation = 2;
    // Tangent
    attributes[3].format = WGPUVertexFormat_Float32x4;
    attributes[3].offset = sizeof(float) * 8;
    attributes[3].shaderLocation = 3;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(float) * 12;  // Vertex3D size
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 4;
    vertexLayout.attributes = attributes;

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout_;

    // Vertex state
    pipelineDesc.vertex.module = shaderModule_;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;

    // Fragment state (minimal - depth only)
    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule_;
    fragmentState.entryPoint = WGPUStringView{.data = "fs_main", .length = 7};
    fragmentState.targetCount = 0;  // No color targets
    pipelineDesc.fragment = &fragmentState;

    // Primitive state
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_Back;

    // Depth state
    WGPUDepthStencilState depthState = {};
    depthState.format = WGPUTextureFormat_Depth32Float;
    depthState.depthWriteEnabled = WGPUOptionalBool_True;
    depthState.depthCompare = WGPUCompareFunction_Less;
    pipelineDesc.depthStencil = &depthState;

    // Multisample state
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    pipeline_ = wgpuDeviceCreateRenderPipeline(renderer_->device(), &pipelineDesc);
    if (!pipeline_) {
        std::cerr << "[ShadowMapPipeline] Failed to create render pipeline\n";
        return false;
    }

    return true;
}

WGPURenderPassEncoder ShadowMapPipeline::beginShadowPass(ShadowMap& shadowMap, const glm::mat4& lightViewProj) {
    if (!pipeline_ || !shadowMap.valid()) {
        return nullptr;
    }

    currentShadowMap_ = &shadowMap;

    // Update light matrix buffer
    wgpuQueueWriteBuffer(renderer_->queue(), lightMatrixBuffer_, 0, &lightViewProj, sizeof(glm::mat4));

    // Create command encoder
    encoder_ = wgpuDeviceCreateCommandEncoder(renderer_->device(), nullptr);

    // Create render pass
    WGPURenderPassDepthStencilAttachment depthAttachment = {};
    depthAttachment.view = shadowMap.depthView();
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.depthReadOnly = false;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthAttachment.stencilReadOnly = true;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 0;
    passDesc.colorAttachments = nullptr;
    passDesc.depthStencilAttachment = &depthAttachment;

    renderPass_ = wgpuCommandEncoderBeginRenderPass(encoder_, &passDesc);
    if (!renderPass_) {
        wgpuCommandEncoderRelease(encoder_);
        encoder_ = nullptr;
        return nullptr;
    }

    // Set pipeline and light matrix bind group
    wgpuRenderPassEncoderSetPipeline(renderPass_, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(renderPass_, 0, lightMatrixBindGroup_, 0, nullptr);

    return renderPass_;
}

void ShadowMapPipeline::endShadowPass() {
    if (renderPass_) {
        wgpuRenderPassEncoderEnd(renderPass_);
        wgpuRenderPassEncoderRelease(renderPass_);
        renderPass_ = nullptr;
    }

    if (encoder_) {
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder_, nullptr);
        wgpuQueueSubmit(renderer_->queue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder_);
        encoder_ = nullptr;
    }

    currentShadowMap_ = nullptr;
}

void ShadowMapPipeline::renderMesh(WGPUBuffer vertexBuffer, WGPUBuffer indexBuffer,
                                    uint32_t indexCount, const glm::mat4& modelMatrix) {
    if (!renderPass_) return;

    // Update transform buffer
    wgpuQueueWriteBuffer(renderer_->queue(), transformBuffer_, 0, &modelMatrix, sizeof(glm::mat4));

    // Create transform bind group (TODO: cache these)
    WGPUBindGroupEntry entry = {};
    entry.binding = 0;
    entry.buffer = transformBuffer_;
    entry.size = sizeof(glm::mat4);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = transformLayout_;
    bgDesc.entryCount = 1;
    bgDesc.entries = &entry;
    WGPUBindGroup transformBindGroup = wgpuDeviceCreateBindGroup(renderer_->device(), &bgDesc);

    // Set bind group and draw
    wgpuRenderPassEncoderSetBindGroup(renderPass_, 1, transformBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass_, 0, vertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(renderPass_, indexBuffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(renderPass_, indexCount, 1, 0, 0, 0);

    wgpuBindGroupRelease(transformBindGroup);
}

// ============================================================================
// ShadowManager Implementation
// ============================================================================

bool ShadowManager::init(Renderer& renderer, const ShadowSettings& settings) {
    renderer_ = &renderer;
    settings_ = settings;

    if (!pipeline_.init(renderer)) {
        return false;
    }

    std::cout << "[ShadowManager] Initialized with resolution " << settings_.resolution << "\n";
    return true;
}

void ShadowManager::destroy() {
    directionalShadowMaps_.clear();
    lightMatrices_.clear();
    pipeline_.destroy();
    renderer_ = nullptr;
}

void ShadowManager::setSettings(const ShadowSettings& settings) {
    // If resolution changed, recreate shadow maps
    if (settings.resolution != settings_.resolution) {
        directionalShadowMaps_.clear();
    }
    settings_ = settings;
}

ShadowMap* ShadowManager::getDirectionalShadowMap(int lightIndex) {
    if (!enabled_ || !renderer_) return nullptr;

    // Ensure we have enough shadow maps
    while (static_cast<int>(directionalShadowMaps_.size()) <= lightIndex) {
        auto shadowMap = std::make_unique<ShadowMap>();
        if (!shadowMap->init(*renderer_, settings_.resolution)) {
            return nullptr;
        }
        directionalShadowMaps_.push_back(std::move(shadowMap));
        lightMatrices_.push_back(glm::mat4(1.0f));
    }

    return directionalShadowMaps_[lightIndex].get();
}

void ShadowManager::renderShadowMaps(
    const SceneLighting& lighting,
    const glm::vec3& sceneCenter,
    float sceneRadius,
    std::function<void(ShadowMapPipeline&, const glm::mat4&)> renderCallback
) {
    if (!enabled_ || !renderer_) return;

    int shadowIndex = 0;
    for (size_t i = 0; i < lighting.lights.size() && shadowIndex < 8; ++i) {
        const Light& light = lighting.lights[i];

        // Only directional lights for now
        if (light.type != LightType::Directional) continue;
        if (!light.castShadows) continue;

        ShadowMap* shadowMap = getDirectionalShadowMap(shadowIndex);
        if (!shadowMap) continue;

        // Calculate light matrix
        glm::mat4 lightMatrix = ShadowMap::calcDirectionalLightMatrix(
            light.direction, sceneCenter, sceneRadius
        );
        lightMatrices_[shadowIndex] = lightMatrix;

        // Render shadow map
        if (pipeline_.beginShadowPass(*shadowMap, lightMatrix)) {
            renderCallback(pipeline_, lightMatrix);
            pipeline_.endShadowPass();
        }

        shadowIndex++;
    }
}

ShadowUniform ShadowManager::getShadowUniform(int lightIndex) const {
    ShadowUniform uniform = {};

    if (lightIndex >= 0 && lightIndex < static_cast<int>(lightMatrices_.size())) {
        uniform.lightViewProj = lightMatrices_[lightIndex];
    } else {
        uniform.lightViewProj = glm::mat4(1.0f);
    }

    uniform.bias = settings_.bias;
    uniform.normalBias = settings_.normalBias;
    uniform.pcfRadius = settings_.pcfRadius;
    uniform.strength = settings_.strength;
    uniform.texelSize = 1.0f / static_cast<float>(settings_.resolution);
    uniform.pcfEnabled = settings_.pcfEnabled ? 1 : 0;

    return uniform;
}

// ============================================================================
// DepthVisualizer Implementation
// ============================================================================

static const char* DEPTH_VIS_SHADER = R"(
// Depth visualization shader - renders depth buffer to color output

@group(0) @binding(0) var depthTexture: texture_depth_2d;
@group(0) @binding(1) var texSampler: sampler;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    // Full-screen triangle (more efficient than quad)
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -3.0),
        vec2f(-1.0, 1.0),
        vec2f(3.0, 1.0)
    );
    var uvs = array<vec2f, 3>(
        vec2f(0.0, 2.0),
        vec2f(0.0, 0.0),
        vec2f(2.0, 0.0)
    );

    var out: VertexOutput;
    out.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    out.uv = uvs[vertexIndex];
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let depth = textureSample(depthTexture, texSampler, in.uv);
    // Apply gamma for better visualization (near = white, far = black)
    let visualDepth = pow(depth, 0.4);
    return vec4f(vec3f(visualDepth), 1.0);
}
)";

DepthVisualizer::~DepthVisualizer() {
    destroy();
}

bool DepthVisualizer::init(Renderer& renderer) {
    destroy();
    renderer_ = &renderer;

    // Create shader module
    WGPUShaderSourceWGSL wgslSource = {};
    wgslSource.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslSource.code = WGPUStringView{.data = DEPTH_VIS_SHADER, .length = strlen(DEPTH_VIS_SHADER)};

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslSource);

    shaderModule_ = wgpuDeviceCreateShaderModule(renderer_->device(), &shaderDesc);
    if (!shaderModule_) {
        std::cerr << "[DepthVisualizer] Failed to create shader module\n";
        return false;
    }

    // Create bind group layout
    WGPUBindGroupLayoutEntry entries[2] = {};
    // Depth texture
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].texture.sampleType = WGPUTextureSampleType_Depth;
    entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    // Sampler
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 2;
    layoutDesc.entries = entries;
    bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(renderer_->device(), &layoutDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout_;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(renderer_->device(), &pipelineLayoutDesc);

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;

    // Vertex state
    pipelineDesc.vertex.module = shaderModule_;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};

    // Fragment state - use RGBA8Unorm to match texture format
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule_;
    fragmentState.entryPoint = WGPUStringView{.data = "fs_main", .length = 7};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    // Primitive state
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;

    // Multisample state
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    pipeline_ = wgpuDeviceCreateRenderPipeline(renderer_->device(), &pipelineDesc);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!pipeline_) {
        std::cerr << "[DepthVisualizer] Failed to create pipeline\n";
        return false;
    }

    // Create sampler for depth texture sampling
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
    samplerDesc.lodMinClamp = 0.0f;
    samplerDesc.lodMaxClamp = 1.0f;
    samplerDesc.maxAnisotropy = 1;
    sampler_ = wgpuDeviceCreateSampler(renderer_->device(), &samplerDesc);

    if (!sampler_) {
        std::cerr << "[DepthVisualizer] Failed to create sampler\n";
        return false;
    }

    std::cout << "[DepthVisualizer] Initialized successfully\n";
    return true;
}

void DepthVisualizer::destroy() {
    if (sampler_) {
        wgpuSamplerRelease(sampler_);
        sampler_ = nullptr;
    }
    if (pipeline_) {
        wgpuRenderPipelineRelease(pipeline_);
        pipeline_ = nullptr;
    }
    if (bindGroupLayout_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout_);
        bindGroupLayout_ = nullptr;
    }
    if (shaderModule_) {
        wgpuShaderModuleRelease(shaderModule_);
        shaderModule_ = nullptr;
    }
    renderer_ = nullptr;
}

void DepthVisualizer::visualize(WGPUTextureView depthView, WGPUTextureView outputView, int width, int height) {
    if (!pipeline_ || !depthView || !outputView) return;

    // Create bind group
    WGPUBindGroupEntry entries[2] = {};
    entries[0].binding = 0;
    entries[0].textureView = depthView;
    entries[1].binding = 1;
    entries[1].sampler = sampler_;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bindGroupLayout_;
    bgDesc.entryCount = 2;
    bgDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(renderer_->device(), &bgDesc);

    // Create command encoder
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(renderer_->device(), nullptr);

    // Create render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = outputView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);  // Full-screen triangle
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(renderer_->queue(), 1, &cmdBuffer);

    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
}

} // namespace vivid
