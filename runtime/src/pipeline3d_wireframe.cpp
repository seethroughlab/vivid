#include "pipeline3d_wireframe.h"
#include "mesh.h"
#include <iostream>
#include <cstring>

namespace vivid {

// ============================================================================
// Wireframe Shader (Using barycentric coordinates)
// ============================================================================

namespace shaders3d {

const char* WIREFRAME = R"(
// ============================================================================
// Wireframe Shader - Edge rendering via barycentric coordinates
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

// Wireframe material - group 2
struct WireframeMaterial {
    color: vec3f,
    opacity: f32,
    thickness: f32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;
@group(1) @binding(0) var<uniform> transform: TransformUniform;
@group(2) @binding(0) var<uniform> material: WireframeMaterial;

struct VertexInput {
    @builtin(vertex_index) vertexIndex: u32,
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) tangent: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) barycentric: vec3f,
    @location(1) worldPos: vec3f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = transform.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.position = camera.viewProjection * worldPos;

    // Assign barycentric coordinates based on vertex index within triangle
    // vertex 0 -> (1,0,0), vertex 1 -> (0,1,0), vertex 2 -> (0,0,1)
    let idx = in.vertexIndex % 3u;
    if (idx == 0u) {
        out.barycentric = vec3f(1.0, 0.0, 0.0);
    } else if (idx == 1u) {
        out.barycentric = vec3f(0.0, 1.0, 0.0);
    } else {
        out.barycentric = vec3f(0.0, 0.0, 1.0);
    }

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Compute distance to nearest edge using barycentric coordinates
    // The closer to an edge, the smaller one of the barycentric values
    let bary = in.barycentric;

    // Use screen-space derivatives for consistent line width
    let d = fwidth(bary);

    // Compute smoothed edge factor for each edge
    let thickness = material.thickness * 0.5;
    let a3 = smoothstep(d.x * thickness, d.x * thickness + d.x, bary.x);
    let b3 = smoothstep(d.y * thickness, d.y * thickness + d.y, bary.y);
    let c3 = smoothstep(d.z * thickness, d.z * thickness + d.z, bary.z);

    // Combine - closer to edge = more visible
    let edgeFactor = min(min(a3, b3), c3);

    // Invert so edges are visible (edgeFactor = 0 at edges, 1 in center)
    let wireAlpha = 1.0 - edgeFactor;

    // Discard if not on edge (for performance and clean compositing)
    if (wireAlpha < 0.01) {
        discard;
    }

    return vec4f(material.color, wireAlpha * material.opacity);
}
)";

} // namespace shaders3d

// ============================================================================
// Pipeline3DWireframe Implementation
// ============================================================================

Pipeline3DWireframe::~Pipeline3DWireframe() {
    destroy();
}

void Pipeline3DWireframe::destroy() {
    destroyDepthBuffer();

    if (pipeline_) { wgpuRenderPipelineRelease(pipeline_); pipeline_ = nullptr; }
    if (cameraLayout_) { wgpuBindGroupLayoutRelease(cameraLayout_); cameraLayout_ = nullptr; }
    if (transformLayout_) { wgpuBindGroupLayoutRelease(transformLayout_); transformLayout_ = nullptr; }
    if (materialLayout_) { wgpuBindGroupLayoutRelease(materialLayout_); materialLayout_ = nullptr; }
    if (pipelineLayout_) { wgpuPipelineLayoutRelease(pipelineLayout_); pipelineLayout_ = nullptr; }
    if (shaderModule_) { wgpuShaderModuleRelease(shaderModule_); shaderModule_ = nullptr; }
    if (cameraBuffer_) { wgpuBufferRelease(cameraBuffer_); cameraBuffer_ = nullptr; }
    if (transformBuffer_) { wgpuBufferRelease(transformBuffer_); transformBuffer_ = nullptr; }
    if (materialBuffer_) { wgpuBufferRelease(materialBuffer_); materialBuffer_ = nullptr; }

    renderer_ = nullptr;
}

void Pipeline3DWireframe::ensureDepthBuffer(int width, int height) {
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

void Pipeline3DWireframe::destroyDepthBuffer() {
    if (depthView_) { wgpuTextureViewRelease(depthView_); depthView_ = nullptr; }
    if (depthTexture_) { wgpuTextureRelease(depthTexture_); depthTexture_ = nullptr; }
    depthWidth_ = 0;
    depthHeight_ = 0;
}

bool Pipeline3DWireframe::init(Renderer& renderer) {
    destroy();
    renderer_ = &renderer;

    return createPipeline(shaders3d::WIREFRAME);
}

bool Pipeline3DWireframe::createPipeline(const std::string& shaderSource) {
    WGPUDevice device = renderer_->device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{.data = shaderSource.c_str(), .length = shaderSource.size()};

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    shaderModule_ = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    if (!shaderModule_) {
        std::cerr << "[Pipeline3DWireframe] Failed to create shader module\n";
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

    // Group 2: Material
    {
        WGPUBindGroupLayoutEntry entry = {};
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Fragment;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = 0;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &entry;
        materialLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);
    }

    // Create pipeline layout
    WGPUBindGroupLayout layouts[] = {cameraLayout_, transformLayout_, materialLayout_};
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 3;
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

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

    // Blend state (alpha blending for anti-aliased edges)
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
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;  // Show both sides for wireframe
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    if (!pipeline_) {
        std::cerr << "[Pipeline3DWireframe] Failed to create render pipeline\n";
        return false;
    }

    // Create uniform buffers
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;

    bufferDesc.size = 256;  // Camera uniform
    cameraBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    bufferDesc.size = 128;  // Transform (2 mat4)
    transformBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    bufferDesc.size = 32;   // Material
    materialBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    std::cout << "[Pipeline3DWireframe] Created successfully\n";
    return true;
}

void Pipeline3DWireframe::render(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                                  const WireframeMaterial& material,
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

    // Update material uniform
    WireframeMaterialUniform materialData = makeWireframeMaterialUniform(material);
    wgpuQueueWriteBuffer(queue, materialBuffer_, 0, &materialData, sizeof(materialData));

    // Create bind groups
    WGPUBindGroup cameraGroup, transformGroup, materialGroup;

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

    // Material bind group
    {
        WGPUBindGroupEntry entry = {};
        entry.binding = 0;
        entry.buffer = materialBuffer_;
        entry.size = sizeof(WireframeMaterialUniform);

        WGPUBindGroupDescriptor desc = {};
        desc.layout = materialLayout_;
        desc.entryCount = 1;
        desc.entries = &entry;
        materialGroup = wgpuDeviceCreateBindGroup(device, &desc);
    }

    // Get output texture view
    TextureData* outputData = getTextureData(output);
    WGPUTextureView outputView = outputData ? outputData->view : nullptr;
    if (!outputView) {
        std::cerr << "[Pipeline3DWireframe] Invalid output texture\n";
        return;
    }

    // Begin render pass
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);

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

    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Set pipeline and bind groups
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, cameraGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 1, transformGroup, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 2, materialGroup, 0, nullptr);

    // Set vertex/index buffers and draw
    Mesh* meshData = static_cast<Mesh*>(mesh.handle);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, meshData->vertexBuffer(), 0, mesh.vertexCount * sizeof(Vertex3D));
    wgpuRenderPassEncoderSetIndexBuffer(renderPass, meshData->indexBuffer(), WGPUIndexFormat_Uint32, 0, mesh.indexCount * sizeof(uint32_t));
    wgpuRenderPassEncoderDrawIndexed(renderPass, mesh.indexCount, 1, 0, 0, 0);

    // End render pass
    wgpuRenderPassEncoderEnd(renderPass);

    // Submit
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuQueueSubmit(queue, 1, &commands);

    // Cleanup
    wgpuCommandBufferRelease(commands);
    wgpuRenderPassEncoderRelease(renderPass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(cameraGroup);
    wgpuBindGroupRelease(transformGroup);
    wgpuBindGroupRelease(materialGroup);
}

} // namespace vivid
