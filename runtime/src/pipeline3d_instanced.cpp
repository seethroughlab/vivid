#include "pipeline3d_instanced.h"
#include "pipeline3d.h"
#include "renderer.h"  // For DEPTH_FORMAT
#include <iostream>
#include <cstring>

namespace vivid {

namespace shaders3d {

const char* INSTANCED_COLOR = R"(
// Camera uniform - binding 0, group 0
struct CameraUniform {
    view: mat4x4f,
    projection: mat4x4f,
    viewProjection: mat4x4f,
    cameraPosition: vec3f,
    _pad: f32,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;

struct VertexInput {
    // Mesh vertex data
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) tangent: vec4f,
    // Instance data (mat4 as 4 vec4s)
    @location(4) inst_model_0: vec4f,
    @location(5) inst_model_1: vec4f,
    @location(6) inst_model_2: vec4f,
    @location(7) inst_model_3: vec4f,
    @location(8) inst_color: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldNormal: vec3f,
    @location(1) color: vec4f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    // Reconstruct model matrix from instance data
    let model = mat4x4f(
        in.inst_model_0,
        in.inst_model_1,
        in.inst_model_2,
        in.inst_model_3
    );

    let worldPos = model * vec4f(in.position, 1.0);
    out.position = camera.viewProjection * worldPos;

    // Transform normal (simplified - assumes uniform scale)
    let normalMatrix = mat3x3f(
        model[0].xyz,
        model[1].xyz,
        model[2].xyz
    );
    out.worldNormal = normalize(normalMatrix * in.normal);

    out.color = in.inst_color;

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Simple hemisphere lighting
    let lightDir = normalize(vec3f(0.5, 1.0, 0.3));
    let ambient = 0.3;
    let diffuse = max(dot(in.worldNormal, lightDir), 0.0);
    let lighting = ambient + diffuse * 0.7;

    return vec4f(in.color.rgb * lighting, in.color.a);
}
)";

} // namespace shaders3d

// Instance attribute layout (static storage)
static WGPUVertexAttribute instanceAttributes[5] = {};
static WGPUVertexBufferLayout instanceLayout = {};
static bool instanceLayoutInitialized = false;

std::pair<WGPUVertexBufferLayout, WGPUVertexBufferLayout> Pipeline3DInstanced::getVertexLayouts() {
    if (!instanceLayoutInitialized) {
        // Instance model matrix row 0 @ location(4)
        instanceAttributes[0].format = WGPUVertexFormat_Float32x4;
        instanceAttributes[0].offset = 0;
        instanceAttributes[0].shaderLocation = 4;

        // Instance model matrix row 1 @ location(5)
        instanceAttributes[1].format = WGPUVertexFormat_Float32x4;
        instanceAttributes[1].offset = 16;
        instanceAttributes[1].shaderLocation = 5;

        // Instance model matrix row 2 @ location(6)
        instanceAttributes[2].format = WGPUVertexFormat_Float32x4;
        instanceAttributes[2].offset = 32;
        instanceAttributes[2].shaderLocation = 6;

        // Instance model matrix row 3 @ location(7)
        instanceAttributes[3].format = WGPUVertexFormat_Float32x4;
        instanceAttributes[3].offset = 48;
        instanceAttributes[3].shaderLocation = 7;

        // Instance color @ location(8)
        instanceAttributes[4].format = WGPUVertexFormat_Float32x4;
        instanceAttributes[4].offset = 64;
        instanceAttributes[4].shaderLocation = 8;

        instanceLayout.arrayStride = sizeof(Instance3D);
        instanceLayout.stepMode = WGPUVertexStepMode_Instance;
        instanceLayout.attributeCount = 5;
        instanceLayout.attributes = instanceAttributes;

        instanceLayoutInitialized = true;
    }

    return {Mesh::getVertexLayout(), instanceLayout};
}

// Pipeline3DInstanced implementation

Pipeline3DInstanced::~Pipeline3DInstanced() {
    destroy();
}

Pipeline3DInstanced::Pipeline3DInstanced(Pipeline3DInstanced&& other) noexcept
    : pipeline_(other.pipeline_)
    , cameraBindGroupLayout_(other.cameraBindGroupLayout_)
    , pipelineLayout_(other.pipelineLayout_)
    , shaderModule_(other.shaderModule_)
    , device_(other.device_) {
    other.pipeline_ = nullptr;
    other.cameraBindGroupLayout_ = nullptr;
    other.pipelineLayout_ = nullptr;
    other.shaderModule_ = nullptr;
    other.device_ = nullptr;
}

Pipeline3DInstanced& Pipeline3DInstanced::operator=(Pipeline3DInstanced&& other) noexcept {
    if (this != &other) {
        destroy();
        pipeline_ = other.pipeline_;
        cameraBindGroupLayout_ = other.cameraBindGroupLayout_;
        pipelineLayout_ = other.pipelineLayout_;
        shaderModule_ = other.shaderModule_;
        device_ = other.device_;
        other.pipeline_ = nullptr;
        other.cameraBindGroupLayout_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.shaderModule_ = nullptr;
        other.device_ = nullptr;
    }
    return *this;
}

bool Pipeline3DInstanced::create(Renderer& renderer) {
    destroy();
    device_ = renderer.device();

    const char* shaderSource = shaders3d::INSTANCED_COLOR;

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{.data = shaderSource, .length = strlen(shaderSource)};

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);

    shaderModule_ = wgpuDeviceCreateShaderModule(device_, &moduleDesc);
    if (!shaderModule_) {
        std::cerr << "[Pipeline3DInstanced] Failed to create shader module\n";
        return false;
    }

    // Create bind group layout for camera (group 0)
    WGPUBindGroupLayoutEntry cameraEntry = {};
    cameraEntry.binding = 0;
    cameraEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    cameraEntry.buffer.type = WGPUBufferBindingType_Uniform;
    cameraEntry.buffer.minBindingSize = sizeof(CameraUniform);

    WGPUBindGroupLayoutDescriptor cameraLayoutDesc = {};
    cameraLayoutDesc.entryCount = 1;
    cameraLayoutDesc.entries = &cameraEntry;

    cameraBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &cameraLayoutDesc);
    if (!cameraBindGroupLayout_) {
        std::cerr << "[Pipeline3DInstanced] Failed to create camera bind group layout\n";
        destroy();
        return false;
    }

    // Create pipeline layout (only camera bind group, no transform - it's per-instance)
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &cameraBindGroupLayout_;

    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDesc);
    if (!pipelineLayout_) {
        std::cerr << "[Pipeline3DInstanced] Failed to create pipeline layout\n";
        destroy();
        return false;
    }

    // Get vertex layouts (mesh + instance)
    auto [vertexLayout, instLayout] = getVertexLayouts();
    WGPUVertexBufferLayout bufferLayouts[] = {vertexLayout, instLayout};

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout_;

    // Vertex state with both vertex and instance buffers
    pipelineDesc.vertex.module = shaderModule_;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};
    pipelineDesc.vertex.bufferCount = 2;
    pipelineDesc.vertex.buffers = bufferLayouts;

    // Primitive state
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_Back;

    // Depth stencil state
    WGPUDepthStencilState depthStencilState = {};
    depthStencilState.format = DEPTH_FORMAT;
    depthStencilState.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencilState.depthCompare = WGPUCompareFunction_Less;
    depthStencilState.stencilFront.compare = WGPUCompareFunction_Always;
    depthStencilState.stencilFront.failOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilFront.passOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilBack.compare = WGPUCompareFunction_Always;
    depthStencilState.stencilBack.failOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilBack.depthFailOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilBack.passOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilReadMask = 0xFFFFFFFF;
    depthStencilState.stencilWriteMask = 0xFFFFFFFF;
    pipelineDesc.depthStencil = &depthStencilState;

    // Fragment state - output to RGBA8 texture
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    // Enable alpha blending
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;
    colorTarget.blend = &blendState;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule_;
    fragmentState.entryPoint = WGPUStringView{.data = "fs_main", .length = 7};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    // Multisample state
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device_, &pipelineDesc);
    if (!pipeline_) {
        std::cerr << "[Pipeline3DInstanced] Failed to create render pipeline\n";
        destroy();
        return false;
    }

    std::cout << "[Pipeline3DInstanced] Created successfully\n";
    return true;
}

void Pipeline3DInstanced::destroy() {
    if (pipeline_) {
        wgpuRenderPipelineRelease(pipeline_);
        pipeline_ = nullptr;
    }
    if (pipelineLayout_) {
        wgpuPipelineLayoutRelease(pipelineLayout_);
        pipelineLayout_ = nullptr;
    }
    if (cameraBindGroupLayout_) {
        wgpuBindGroupLayoutRelease(cameraBindGroupLayout_);
        cameraBindGroupLayout_ = nullptr;
    }
    if (shaderModule_) {
        wgpuShaderModuleRelease(shaderModule_);
        shaderModule_ = nullptr;
    }
    device_ = nullptr;
}

// Renderer3DInstanced implementation

Renderer3DInstanced::~Renderer3DInstanced() {
    destroyDepthBuffer();
    if (instanceBuffer_) {
        wgpuBufferRelease(instanceBuffer_);
    }
    if (cameraBindGroup_) {
        wgpuBindGroupRelease(cameraBindGroup_);
    }
    if (cameraBuffer_) {
        wgpuBufferRelease(cameraBuffer_);
    }
}

void Renderer3DInstanced::init(Renderer& renderer) {
    renderer_ = &renderer;

    // Create instanced pipeline
    pipeline_.create(renderer);

    // Create camera uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(CameraUniform);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;

    cameraBuffer_ = wgpuDeviceCreateBuffer(renderer_->device(), &bufferDesc);

    // Create camera bind group
    if (pipeline_.valid()) {
        WGPUBindGroupEntry entry = {};
        entry.binding = 0;
        entry.buffer = cameraBuffer_;
        entry.size = sizeof(CameraUniform);

        WGPUBindGroupDescriptor desc = {};
        desc.layout = pipeline_.cameraBindGroupLayout();
        desc.entryCount = 1;
        desc.entries = &entry;

        cameraBindGroup_ = wgpuDeviceCreateBindGroup(renderer_->device(), &desc);
    }
}

void Renderer3DInstanced::ensureDepthBuffer(int width, int height) {
    if (depthTexture_ && depthWidth_ == width && depthHeight_ == height) {
        return;
    }

    destroyDepthBuffer();

    WGPUTextureDescriptor depthDesc = {};
    depthDesc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    depthDesc.format = DEPTH_FORMAT;
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    depthDesc.dimension = WGPUTextureDimension_2D;

    depthTexture_ = wgpuDeviceCreateTexture(renderer_->device(), &depthDesc);
    if (!depthTexture_) {
        std::cerr << "[Renderer3DInstanced] Failed to create depth texture\n";
        return;
    }

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = DEPTH_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_DepthOnly;

    depthView_ = wgpuTextureCreateView(depthTexture_, &viewDesc);

    depthWidth_ = width;
    depthHeight_ = height;
}

void Renderer3DInstanced::destroyDepthBuffer() {
    if (depthView_) {
        wgpuTextureViewRelease(depthView_);
        depthView_ = nullptr;
    }
    if (depthTexture_) {
        wgpuTextureRelease(depthTexture_);
        depthTexture_ = nullptr;
    }
    depthWidth_ = 0;
    depthHeight_ = 0;
}

void Renderer3DInstanced::ensureInstanceBuffer(size_t count) {
    size_t requiredSize = count * sizeof(Instance3D);

    if (instanceBuffer_ && instanceBufferCapacity_ >= requiredSize) {
        return;
    }

    if (instanceBuffer_) {
        wgpuBufferRelease(instanceBuffer_);
    }

    // Allocate with some headroom to avoid frequent reallocations
    instanceBufferCapacity_ = requiredSize * 2;

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = instanceBufferCapacity_;
    bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;

    instanceBuffer_ = wgpuDeviceCreateBuffer(renderer_->device(), &bufferDesc);
}

void Renderer3DInstanced::drawInstanced(const Mesh& mesh,
                                         const std::vector<Instance3D>& instances,
                                         const Camera3D& camera,
                                         Texture& output,
                                         const glm::vec4& clearColor) {
    if (!renderer_ || !pipeline_.valid() || !mesh.valid() || instances.empty()) {
        return;
    }

    if (!hasValidGPU(output)) {
        return;
    }

    // Update camera uniform
    float aspectRatio = static_cast<float>(output.width) / output.height;
    CameraUniform cameraData = makeCameraUniform(camera, aspectRatio);
    wgpuQueueWriteBuffer(renderer_->queue(), cameraBuffer_, 0,
                         &cameraData, sizeof(CameraUniform));

    // Update instance buffer
    ensureInstanceBuffer(instances.size());
    wgpuQueueWriteBuffer(renderer_->queue(), instanceBuffer_, 0,
                         instances.data(), instances.size() * sizeof(Instance3D));

    // Ensure depth buffer
    ensureDepthBuffer(output.width, output.height);
    if (!depthView_) return;

    auto* outputData = getTextureData(output);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(renderer_->device(), &encoderDesc);

    // Color attachment
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = outputData->view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};

    // Depth attachment
    WGPURenderPassDepthStencilAttachment depthAttachment = {};
    depthAttachment.view = depthView_;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.depthReadOnly = false;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthAttachment.stencilReadOnly = true;

    // Begin render pass
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.depthStencilAttachment = &depthAttachment;

    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Set pipeline and bind groups
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline_.pipeline());
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, cameraBindGroup_, 0, nullptr);

    // Set vertex buffer (mesh)
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, mesh.vertexBuffer(), 0,
                                          mesh.vertexCount() * sizeof(Vertex3D));

    // Set instance buffer
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 1, instanceBuffer_, 0,
                                          instances.size() * sizeof(Instance3D));

    // Set index buffer and draw
    wgpuRenderPassEncoderSetIndexBuffer(renderPass, mesh.indexBuffer(),
                                         WGPUIndexFormat_Uint32, 0,
                                         mesh.indexCount() * sizeof(uint32_t));

    wgpuRenderPassEncoderDrawIndexed(renderPass, mesh.indexCount(),
                                      static_cast<uint32_t>(instances.size()),
                                      0, 0, 0);

    // End render pass
    wgpuRenderPassEncoderEnd(renderPass);
    wgpuRenderPassEncoderRelease(renderPass);

    // Submit
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(renderer_->queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
}

} // namespace vivid
