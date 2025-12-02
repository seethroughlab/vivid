#include "pipeline3d_vertex_lit.h"
#include "mesh.h"
#include <iostream>
#include <cstring>

namespace vivid {

// ============================================================================
// Vertex-Lit Shader (Retro/PS1 Style)
// ============================================================================

namespace shaders3d {

const char* VERTEX_LIT = R"(
// ============================================================================
// Vertex-Lit Shader with Quantization (PS1/Toon Style)
// ============================================================================

// Camera uniform - group 0
struct CameraUniform {
    view: mat4x4f,
    projection: mat4x4f,
    viewProjection: mat4x4f,
    cameraPosition: vec3f,
    _pad: f32,
}

// Transform uniform - group 1
struct TransformUniform {
    model: mat4x4f,
    normalMatrix: mat4x4f,
}

// Light direction uniform - group 2
struct LightUniform {
    direction: vec3f,
    _pad1: f32,
    color: vec3f,
    _pad2: f32,
}

// Vertex-lit material - group 3
struct VertexLitMaterial {
    diffuse: vec3f,
    _pad1: f32,
    ambient: vec3f,
    ambientAmount: f32,
    emissive: vec3f,
    _pad2: f32,
    quantizeSteps: i32,
    hardSpecular: i32,
    specularPower: f32,
    specularThreshold: f32,
    hasTexture: i32,
    _pad3: f32,
    _pad4: f32,
    _pad5: f32,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;
@group(1) @binding(0) var<uniform> transform: TransformUniform;
@group(2) @binding(0) var<uniform> light: LightUniform;
@group(3) @binding(0) var<uniform> material: VertexLitMaterial;
@group(3) @binding(1) var diffuseTexture: texture_2d<f32>;
@group(3) @binding(2) var diffuseSampler: sampler;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) tangent: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) uv: vec2f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = transform.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.position = camera.viewProjection * worldPos;
    out.worldNormal = normalize((transform.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    out.uv = in.uv;

    return out;
}

// Quantize a value to discrete steps (for toon/PS1 effect)
fn quantize(value: f32, steps: i32) -> f32 {
    if (steps <= 0) {
        return value;
    }
    let s = f32(steps);
    return floor(value * s + 0.5) / s;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let normal = normalize(in.worldNormal);
    let lightDir = normalize(-light.direction);
    let viewDir = normalize(camera.cameraPosition - in.worldPos);

    // === Diffuse lighting (N·L) ===
    var NdotL = max(dot(normal, lightDir), 0.0);

    // Apply quantization for toon/PS1 look
    if (material.quantizeSteps > 0) {
        NdotL = quantize(NdotL, material.quantizeSteps);
    }

    // Get base color from texture or material
    var baseColor = material.diffuse;
    if (material.hasTexture != 0) {
        let texColor = textureSample(diffuseTexture, diffuseSampler, in.uv);
        baseColor = baseColor * texColor.rgb;
    }

    // Combine ambient and diffuse
    let ambient = material.ambient * material.ambientAmount;
    let diffuse = baseColor * NdotL * light.color;

    var color = ambient + diffuse;

    // === Hard specular highlight (optional) ===
    if (material.hardSpecular != 0) {
        let halfDir = normalize(lightDir + viewDir);
        let NdotH = max(dot(normal, halfDir), 0.0);
        let spec = pow(NdotH, material.specularPower);

        // Hard threshold for specular (sharp highlight)
        if (spec > material.specularThreshold) {
            color += light.color * 0.5;  // Add white-ish highlight
        }
    }

    // Add emissive
    color += material.emissive;

    // No HDR/gamma for authentic retro look
    return vec4f(clamp(color, vec3f(0.0), vec3f(1.0)), 1.0);
}
)";

} // namespace shaders3d

// ============================================================================
// Pipeline3DVertexLit Implementation
// ============================================================================

Pipeline3DVertexLit::~Pipeline3DVertexLit() {
    destroy();
}

void Pipeline3DVertexLit::destroy() {
    destroyDepthBuffer();

    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (cameraLayout_) { wgpuBindGroupLayoutRelease(cameraLayout_); cameraLayout_ = nullptr; }
    if (transformLayout_) { wgpuBindGroupLayoutRelease(transformLayout_); transformLayout_ = nullptr; }
    if (lightLayout_) { wgpuBindGroupLayoutRelease(lightLayout_); lightLayout_ = nullptr; }
    if (materialLayout_) { wgpuBindGroupLayoutRelease(materialLayout_); materialLayout_ = nullptr; }
    if (pipelineLayout_) { wgpuPipelineLayoutRelease(pipelineLayout_); pipelineLayout_ = nullptr; }
    if (shaderModule_) { wgpuShaderModuleRelease(shaderModule_); shaderModule_ = nullptr; }
    if (textureSampler_) { wgpuSamplerRelease(textureSampler_); textureSampler_ = nullptr; }
    if (cameraBuffer_) { wgpuBufferRelease(cameraBuffer_); cameraBuffer_ = nullptr; }
    if (transformBuffer_) { wgpuBufferRelease(transformBuffer_); transformBuffer_ = nullptr; }
    if (lightBuffer_) { wgpuBufferRelease(lightBuffer_); lightBuffer_ = nullptr; }
    if (materialBuffer_) { wgpuBufferRelease(materialBuffer_); materialBuffer_ = nullptr; }
    if (defaultTextureView_) { wgpuTextureViewRelease(defaultTextureView_); defaultTextureView_ = nullptr; }
    if (defaultTexture_) { wgpuTextureRelease(defaultTexture_); defaultTexture_ = nullptr; }

    renderer_ = nullptr;
}

void Pipeline3DVertexLit::ensureDepthBuffer(int width, int height) {
    if (depthTexture_ && depthWidth_ == width && depthHeight_ == height) {
        return;
    }

    destroyDepthBuffer();

    WGPUTextureDescriptor depthDesc = {};
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;
    depthDesc.dimension = WGPUTextureDimension_2D;
    depthDesc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    depthDesc.format = WGPUTextureFormat_Depth24PlusStencil8;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;

    depthTexture_ = wgpuDeviceCreateTexture(renderer_->device(), &depthDesc);
    depthView_ = wgpuTextureCreateView(depthTexture_, nullptr);
    depthWidth_ = width;
    depthHeight_ = height;
}

void Pipeline3DVertexLit::destroyDepthBuffer() {
    if (depthView_) { wgpuTextureViewRelease(depthView_); depthView_ = nullptr; }
    if (depthTexture_) { wgpuTextureRelease(depthTexture_); depthTexture_ = nullptr; }
    depthWidth_ = 0;
    depthHeight_ = 0;
}

bool Pipeline3DVertexLit::init(Renderer& renderer) {
    destroy();
    renderer_ = &renderer;

    return createPipeline(shaders3d::VERTEX_LIT);
}

bool Pipeline3DVertexLit::createPipeline(const std::string& shaderSource) {
    WGPUDevice device = renderer_->device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{.data = shaderSource.c_str(), .length = shaderSource.size()};

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    shaderModule_ = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    if (!shaderModule_) {
        std::cerr << "[Pipeline3DVertexLit] Failed to create shader module\n";
        return false;
    }

    // Create bind group layouts
    // Group 0: Camera
    {
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = 0;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        cameraLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    }

    // Group 1: Transform
    {
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = 0;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        transformLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    }

    // Group 2: Light
    {
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Fragment;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = 0;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        lightLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    }

    // Group 3: Material + Texture
    {
        WGPUBindGroupLayoutEntry entries[3] = {};

        // Material uniform
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = 0;

        // Diffuse texture
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].texture.sampleType = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

        // Sampler
        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].sampler.type = WGPUSamplerBindingType_Filtering;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 3;
        layoutDesc.entries = entries;
        materialLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    }

    // Create pipeline layout
    WGPUBindGroupLayout layouts[] = {cameraLayout_, transformLayout_, lightLayout_, materialLayout_};
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 4;
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Create sampler for texture filtering
    {
        WGPUSamplerDescriptor samplerDesc = {};
        samplerDesc.addressModeU = WGPUAddressMode_Repeat;
        samplerDesc.addressModeV = WGPUAddressMode_Repeat;
        samplerDesc.addressModeW = WGPUAddressMode_Repeat;
        samplerDesc.magFilter = WGPUFilterMode_Linear;
        samplerDesc.minFilter = WGPUFilterMode_Linear;
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        samplerDesc.maxAnisotropy = 1;
        textureSampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);
    }

    // Vertex layout (position, normal, uv, tangent)
    WGPUVertexAttribute attributes[4] = {};
    attributes[0].format = WGPUVertexFormat_Float32x3;
    attributes[0].offset = 0;
    attributes[0].shaderLocation = 0;  // position

    attributes[1].format = WGPUVertexFormat_Float32x3;
    attributes[1].offset = sizeof(float) * 3;
    attributes[1].shaderLocation = 1;  // normal

    attributes[2].format = WGPUVertexFormat_Float32x2;
    attributes[2].offset = sizeof(float) * 6;
    attributes[2].shaderLocation = 2;  // uv

    attributes[3].format = WGPUVertexFormat_Float32x4;
    attributes[3].offset = sizeof(float) * 8;
    attributes[3].shaderLocation = 3;  // tangent

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(Vertex3D);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 4;
    vertexLayout.attributes = attributes;

    // Blend state
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule_;
    fragmentState.entryPoint = WGPUStringView{.data = "fs_main", .length = 7};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Depth stencil state
    WGPUDepthStencilState depthState = {};
    depthState.format = WGPUTextureFormat_Depth24PlusStencil8;
    depthState.depthWriteEnabled = WGPUOptionalBool_True;
    depthState.depthCompare = WGPUCompareFunction_Less;

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout_;

    pipelineDesc.vertex.module = shaderModule_;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;

    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.depthStencil = &depthState;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_Back;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    if (!pipeline_) {
        std::cerr << "[Pipeline3DVertexLit] Failed to create render pipeline\n";
        return false;
    }

    // Create uniform buffers
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;

    bufferDesc.size = 256;  // Camera uniform
    cameraBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    bufferDesc.size = 128;  // Transform (2 mat4)
    transformBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    bufferDesc.size = 32;   // Light (direction + color)
    lightBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    bufferDesc.size = 80;   // Material (5 * 16 bytes)
    materialBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Create 1x1 white default texture for when no texture is provided
    {
        WGPUTextureDescriptor texDesc = {};
        texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.size = {1, 1, 1};
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        defaultTexture_ = wgpuDeviceCreateTexture(device, &texDesc);
        defaultTextureView_ = wgpuTextureCreateView(defaultTexture_, nullptr);

        // Upload white pixel
        uint8_t whitePixel[4] = {255, 255, 255, 255};
        WGPUTexelCopyTextureInfo dst = {};
        dst.texture = defaultTexture_;
        WGPUTexelCopyBufferLayout layout = {};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D extent = {1, 1, 1};
        wgpuQueueWriteTexture(renderer_->queue(), &dst, whitePixel, 4, &layout, &extent);
    }

    std::cout << "[Pipeline3DVertexLit] Created successfully\n";
    return true;
}

void Pipeline3DVertexLit::render(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                                  const VertexLitMaterial& material,
                                  const glm::vec3& lightDir, const glm::vec3& lightColor,
                                  Texture& output, const glm::vec4& clearColor) {
    if (!valid() || !mesh.valid() || !output.valid()) {
        return;
    }

    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();

    ensureDepthBuffer(output.width, output.height);

    // Update camera uniform
    float aspect = static_cast<float>(output.width) / output.height;
    struct CameraData {
        glm::mat4 view;
        glm::mat4 projection;
        glm::mat4 viewProjection;
        glm::vec3 cameraPosition;
        float _pad;
    } cameraData;
    cameraData.view = camera.viewMatrix();
    cameraData.projection = camera.projectionMatrix(aspect);
    cameraData.viewProjection = cameraData.projection * cameraData.view;
    cameraData.cameraPosition = camera.position;
    wgpuQueueWriteBuffer(queue, cameraBuffer_, 0, &cameraData, sizeof(cameraData));

    // Update transform uniform
    struct TransformData {
        glm::mat4 model;
        glm::mat4 normalMatrix;
    } transformData;
    transformData.model = transform;
    transformData.normalMatrix = glm::transpose(glm::inverse(transform));
    wgpuQueueWriteBuffer(queue, transformBuffer_, 0, &transformData, sizeof(transformData));

    // Update light uniform
    struct LightData {
        glm::vec3 direction;
        float _pad1;
        glm::vec3 color;
        float _pad2;
    } lightData;
    lightData.direction = glm::normalize(lightDir);
    lightData.color = lightColor;
    wgpuQueueWriteBuffer(queue, lightBuffer_, 0, &lightData, sizeof(lightData));

    // Update material uniform
    VertexLitMaterialUniform materialData = makeVertexLitMaterialUniform(material);
    wgpuQueueWriteBuffer(queue, materialBuffer_, 0, &materialData, sizeof(materialData));

    // Create bind groups
    WGPUBindGroup cameraGroup, transformGroup, lightGroup, materialGroup;

    // Camera bind group
    {
        WGPUBindGroupEntry entry = {};
        entry.binding = 0;
        entry.buffer = cameraBuffer_;
        entry.size = sizeof(CameraData);

        WGPUBindGroupDescriptor desc = {};
        desc.layout = cameraLayout_;
        desc.entryCount = 1;
        desc.entries = &entry;
        cameraGroup = wgpuDeviceCreateBindGroup(device, &desc);
    }

    // Transform bind group
    {
        WGPUBindGroupEntry entry = {};
        entry.binding = 0;
        entry.buffer = transformBuffer_;
        entry.size = sizeof(TransformData);

        WGPUBindGroupDescriptor desc = {};
        desc.layout = transformLayout_;
        desc.entryCount = 1;
        desc.entries = &entry;
        transformGroup = wgpuDeviceCreateBindGroup(device, &desc);
    }

    // Light bind group
    {
        WGPUBindGroupEntry entry = {};
        entry.binding = 0;
        entry.buffer = lightBuffer_;
        entry.size = sizeof(LightData);

        WGPUBindGroupDescriptor desc = {};
        desc.layout = lightLayout_;
        desc.entryCount = 1;
        desc.entries = &entry;
        lightGroup = wgpuDeviceCreateBindGroup(device, &desc);
    }

    // Material bind group (with texture)
    {
        WGPUBindGroupEntry entries[3] = {};

        // Material buffer
        entries[0].binding = 0;
        entries[0].buffer = materialBuffer_;
        entries[0].size = sizeof(VertexLitMaterialUniform);

        // Texture view
        WGPUTextureView texView = nullptr;
        if (material.diffuseMap && material.diffuseMap->valid()) {
            TextureData* texData = getTextureData(*material.diffuseMap);
            if (texData && texData->view) {
                texView = texData->view;
            }
        }
        if (!texView) {
            // Use default 1x1 white texture as placeholder
            texView = defaultTextureView_;
        }
        entries[1].binding = 1;
        entries[1].textureView = texView;

        // Sampler
        entries[2].binding = 2;
        entries[2].sampler = textureSampler_;

        WGPUBindGroupDescriptor desc = {};
        desc.layout = materialLayout_;
        desc.entryCount = 3;
        desc.entries = entries;
        materialGroup = wgpuDeviceCreateBindGroup(device, &desc);
    }

    // Get output texture view
    TextureData* outputData = getTextureData(output);
    WGPUTextureView outputView = outputData ? outputData->view : nullptr;
    if (!outputView) {
        std::cerr << "[Pipeline3DVertexLit] Invalid output texture\n";
        return;
    }

    // Begin render pass
    encoder_ = wgpuDeviceCreateCommandEncoder(device, nullptr);

    // Convention: negative alpha means "don't clear, keep existing content"
    bool shouldClear = clearColor.a >= 0.0f;

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = outputView;
    colorAttachment.loadOp = shouldClear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r, clearColor.g, clearColor.b, glm::max(0.0f, clearColor.a)};

    WGPURenderPassDepthStencilAttachment depthAttachment = {};
    depthAttachment.view = depthView_;
    depthAttachment.depthLoadOp = shouldClear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.stencilLoadOp = shouldClear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Store;
    depthAttachment.stencilClearValue = 0;

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.depthStencilAttachment = &depthAttachment;

    renderPass_ = wgpuCommandEncoderBeginRenderPass(encoder_, &renderPassDesc);

    // Set pipeline and bind groups
    wgpuRenderPassEncoderSetPipeline(renderPass_, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(renderPass_, 0, cameraGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(renderPass_, 1, transformGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(renderPass_, 2, lightGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(renderPass_, 3, materialGroup, 0, nullptr);

    // Set vertex/index buffers and draw
    Mesh* meshData = static_cast<Mesh*>(mesh.handle);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass_, 0, meshData->vertexBuffer(), 0, mesh.vertexCount * sizeof(Vertex3D));
    wgpuRenderPassEncoderSetIndexBuffer(renderPass_, meshData->indexBuffer(), WGPUIndexFormat_Uint32, 0, mesh.indexCount * sizeof(uint32_t));
    wgpuRenderPassEncoderDrawIndexed(renderPass_, mesh.indexCount, 1, 0, 0, 0);

    // End render pass
    wgpuRenderPassEncoderEnd(renderPass_);

    // Submit
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder_, nullptr);
    wgpuQueueSubmit(queue, 1, &commands);

    // Cleanup
    wgpuCommandBufferRelease(commands);
    wgpuRenderPassEncoderRelease(renderPass_);
    wgpuCommandEncoderRelease(encoder_);
    wgpuBindGroupRelease(cameraGroup);
    wgpuBindGroupRelease(transformGroup);
    wgpuBindGroupRelease(lightGroup);
    wgpuBindGroupRelease(materialGroup);

    renderPass_ = nullptr;
    encoder_ = nullptr;
}

} // namespace vivid
