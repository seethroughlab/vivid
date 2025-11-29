#include "pipeline3d.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

namespace vivid {

// Built-in shaders
namespace shaders3d {

const char* UNLIT_NORMAL = R"(
// Camera uniform - binding 0, group 0
struct CameraUniform {
    view: mat4x4f,
    projection: mat4x4f,
    viewProjection: mat4x4f,
    cameraPosition: vec3f,
    _pad: f32,
}

// Transform uniform - binding 0, group 1
struct TransformUniform {
    model: mat4x4f,
    normalMatrix: mat4x4f,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;
@group(1) @binding(0) var<uniform> transform: TransformUniform;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) tangent: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldNormal: vec3f,
    @location(1) uv: vec2f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = transform.model * vec4f(in.position, 1.0);
    out.position = camera.viewProjection * worldPos;

    // Transform normal to world space (using normalMatrix)
    out.worldNormal = normalize((transform.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    out.uv = in.uv;

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Display normal as color (remap from [-1,1] to [0,1])
    let normalColor = in.worldNormal * 0.5 + 0.5;
    return vec4f(normalColor, 1.0);
}
)";

const char* SOLID_COLOR = R"(
// Camera uniform - binding 0, group 0
struct CameraUniform {
    view: mat4x4f,
    projection: mat4x4f,
    viewProjection: mat4x4f,
    cameraPosition: vec3f,
    _pad: f32,
}

// Transform uniform - binding 0, group 1
struct TransformUniform {
    model: mat4x4f,
    normalMatrix: mat4x4f,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;
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
    out.position = camera.viewProjection * worldPos;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    return vec4f(1.0, 0.5, 0.2, 1.0);  // Orange color
}
)";

} // namespace shaders3d

// Pipeline3D implementation

Pipeline3D::~Pipeline3D() {
    destroy();
}

Pipeline3D::Pipeline3D(Pipeline3D&& other) noexcept
    : pipeline_(other.pipeline_)
    , cameraBindGroupLayout_(other.cameraBindGroupLayout_)
    , transformBindGroupLayout_(other.transformBindGroupLayout_)
    , pipelineLayout_(other.pipelineLayout_)
    , shaderModule_(other.shaderModule_)
    , device_(other.device_)
    , sourcePath_(std::move(other.sourcePath_))
    , lastError_(std::move(other.lastError_)) {
    other.pipeline_ = nullptr;
    other.cameraBindGroupLayout_ = nullptr;
    other.transformBindGroupLayout_ = nullptr;
    other.pipelineLayout_ = nullptr;
    other.shaderModule_ = nullptr;
    other.device_ = nullptr;
}

Pipeline3D& Pipeline3D::operator=(Pipeline3D&& other) noexcept {
    if (this != &other) {
        destroy();
        pipeline_ = other.pipeline_;
        cameraBindGroupLayout_ = other.cameraBindGroupLayout_;
        transformBindGroupLayout_ = other.transformBindGroupLayout_;
        pipelineLayout_ = other.pipelineLayout_;
        shaderModule_ = other.shaderModule_;
        device_ = other.device_;
        sourcePath_ = std::move(other.sourcePath_);
        lastError_ = std::move(other.lastError_);
        other.pipeline_ = nullptr;
        other.cameraBindGroupLayout_ = nullptr;
        other.transformBindGroupLayout_ = nullptr;
        other.pipelineLayout_ = nullptr;
        other.shaderModule_ = nullptr;
        other.device_ = nullptr;
    }
    return *this;
}

bool Pipeline3D::create(Renderer& renderer, const std::string& wgslSource) {
    destroy();
    lastError_.clear();

    device_ = renderer.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{.data = wgslSource.c_str(), .length = wgslSource.size()};

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);

    shaderModule_ = wgpuDeviceCreateShaderModule(device_, &moduleDesc);
    if (!shaderModule_) {
        lastError_ = "Failed to create shader module";
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
        lastError_ = "Failed to create camera bind group layout";
        destroy();
        return false;
    }

    // Create bind group layout for transform (group 1)
    WGPUBindGroupLayoutEntry transformEntry = {};
    transformEntry.binding = 0;
    transformEntry.visibility = WGPUShaderStage_Vertex;
    transformEntry.buffer.type = WGPUBufferBindingType_Uniform;
    transformEntry.buffer.minBindingSize = sizeof(TransformUniform);

    WGPUBindGroupLayoutDescriptor transformLayoutDesc = {};
    transformLayoutDesc.entryCount = 1;
    transformLayoutDesc.entries = &transformEntry;

    transformBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &transformLayoutDesc);
    if (!transformBindGroupLayout_) {
        lastError_ = "Failed to create transform bind group layout";
        destroy();
        return false;
    }

    // Create pipeline layout with both bind group layouts
    WGPUBindGroupLayout layouts[] = {cameraBindGroupLayout_, transformBindGroupLayout_};
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 2;
    pipelineLayoutDesc.bindGroupLayouts = layouts;

    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDesc);
    if (!pipelineLayout_) {
        lastError_ = "Failed to create pipeline layout";
        destroy();
        return false;
    }

    // Get vertex layout
    WGPUVertexBufferLayout vertexLayout = Mesh::getVertexLayout();

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout_;

    // Vertex state with Vertex3D input
    pipelineDesc.vertex.module = shaderModule_;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;

    // Primitive state
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_Back;  // Enable backface culling

    // Depth stencil state
    WGPUDepthStencilState depthStencilState = {};
    depthStencilState.format = DEPTH_FORMAT;
    depthStencilState.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencilState.depthCompare = WGPUCompareFunction_Less;
    pipelineDesc.depthStencil = &depthStencilState;

    // Fragment state - output to RGBA8 texture
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

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
        lastError_ = "Failed to create render pipeline";
        destroy();
        return false;
    }

    std::cout << "[Pipeline3D] Created successfully\n";
    return true;
}

bool Pipeline3D::createFromFile(Renderer& renderer, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        lastError_ = "Failed to open file: " + path;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    sourcePath_ = path;
    return create(renderer, buffer.str());
}

void Pipeline3D::destroy() {
    if (pipeline_) {
        wgpuRenderPipelineRelease(pipeline_);
        pipeline_ = nullptr;
    }
    if (pipelineLayout_) {
        wgpuPipelineLayoutRelease(pipelineLayout_);
        pipelineLayout_ = nullptr;
    }
    if (transformBindGroupLayout_) {
        wgpuBindGroupLayoutRelease(transformBindGroupLayout_);
        transformBindGroupLayout_ = nullptr;
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

// Renderer3D implementation

Renderer3D::~Renderer3D() {
    if (cameraBuffer_) {
        wgpuBufferRelease(cameraBuffer_);
    }
    destroyDepthBuffer();
}

void Renderer3D::init(Renderer& renderer) {
    renderer_ = &renderer;

    // Create camera uniform buffer (reused each frame)
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(CameraUniform);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;

    cameraBuffer_ = wgpuDeviceCreateBuffer(renderer_->device(), &bufferDesc);
}

void Renderer3D::ensureDepthBuffer(int width, int height) {
    if (depthTexture_ && depthWidth_ == width && depthHeight_ == height) {
        return;
    }

    destroyDepthBuffer();

    // Create depth texture
    WGPUTextureDescriptor depthDesc = {};
    depthDesc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    depthDesc.format = DEPTH_FORMAT;
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    depthDesc.dimension = WGPUTextureDimension_2D;

    depthTexture_ = wgpuDeviceCreateTexture(renderer_->device(), &depthDesc);
    if (!depthTexture_) {
        std::cerr << "[Renderer3D] Failed to create depth texture\n";
        return;
    }

    // Create depth texture view
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

void Renderer3D::destroyDepthBuffer() {
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

WGPURenderPassEncoder Renderer3D::beginRenderPass(Texture& output, const glm::vec4& clearColor) {
    if (!renderer_ || !hasValidGPU(output)) return nullptr;

    // Ensure depth buffer matches output size
    ensureDepthBuffer(output.width, output.height);
    if (!depthView_) return nullptr;

    auto* outputData = getTextureData(output);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoder_ = wgpuDeviceCreateCommandEncoder(renderer_->device(), &encoderDesc);

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

    // Render pass descriptor
    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.depthStencilAttachment = &depthAttachment;

    renderPass_ = wgpuCommandEncoderBeginRenderPass(encoder_, &renderPassDesc);
    return renderPass_;
}

void Renderer3D::endRenderPass() {
    if (renderPass_) {
        wgpuRenderPassEncoderEnd(renderPass_);
        wgpuRenderPassEncoderRelease(renderPass_);
        renderPass_ = nullptr;
    }

    if (encoder_) {
        WGPUCommandBufferDescriptor cmdBufferDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder_, &cmdBufferDesc);
        wgpuQueueSubmit(renderer_->queue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder_);
        encoder_ = nullptr;
    }
}

void Renderer3D::setCamera(const Camera3D& camera, float aspectRatio) {
    currentCamera_ = makeCameraUniform(camera, aspectRatio);

    if (cameraBuffer_ && renderer_) {
        wgpuQueueWriteBuffer(renderer_->queue(), cameraBuffer_, 0,
                             &currentCamera_, sizeof(CameraUniform));
    }
}

WGPUBindGroup Renderer3D::createCameraBindGroup(WGPUBindGroupLayout layout) {
    if (!renderer_ || !cameraBuffer_) return nullptr;

    WGPUBindGroupEntry entry = {};
    entry.binding = 0;
    entry.buffer = cameraBuffer_;
    entry.size = sizeof(CameraUniform);

    WGPUBindGroupDescriptor desc = {};
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;

    return wgpuDeviceCreateBindGroup(renderer_->device(), &desc);
}

WGPUBindGroup Renderer3D::createTransformBindGroup(WGPUBindGroupLayout layout, const glm::mat4& transform) {
    if (!renderer_) return nullptr;

    // Create a temporary buffer for this transform
    TransformUniform data;
    data.model = transform;
    data.normalMatrix = glm::transpose(glm::inverse(transform));

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(TransformUniform);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;

    WGPUBuffer buffer = wgpuDeviceCreateBuffer(renderer_->device(), &bufferDesc);
    wgpuQueueWriteBuffer(renderer_->queue(), buffer, 0, &data, sizeof(TransformUniform));

    WGPUBindGroupEntry entry = {};
    entry.binding = 0;
    entry.buffer = buffer;
    entry.size = sizeof(TransformUniform);

    WGPUBindGroupDescriptor desc = {};
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;

    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(renderer_->device(), &desc);

    // Note: The buffer will be released when the bind group is released
    // Actually, we need to store the buffer reference... for now, leak it (fix later)
    // TODO: Better resource management

    return bindGroup;
}

void Renderer3D::releaseBindGroup(WGPUBindGroup bindGroup) {
    if (bindGroup) {
        wgpuBindGroupRelease(bindGroup);
    }
}

} // namespace vivid
