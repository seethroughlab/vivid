#include "pipeline2d.h"
#include "renderer.h"
#include <cmath>
#include <iostream>
#include <cstring>

namespace vivid {

// 2D Circle instanced shader
static const char* CIRCLE_2D_SHADER = R"(
struct Uniforms {
    resolution: vec2f,
    aspectRatio: f32,
    _pad: f32,
}

struct VertexInput {
    @location(0) position: vec2f,  // Local vertex position
}

struct InstanceInput {
    @location(1) center: vec2f,    // Circle center (0-1)
    @location(2) radius: f32,      // Circle radius
    @location(3) _pad: f32,
    @location(4) color: vec4f,     // Circle color
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) localPos: vec2f,
    @location(1) color: vec4f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

@vertex
fn vs_main(vertex: VertexInput, instance: InstanceInput) -> VertexOutput {
    var out: VertexOutput;

    // Transform local circle vertex to world position
    // vertex.position is -1 to 1, scale by radius
    let scaledPos = vertex.position * instance.radius;

    // Convert from normalized (0-1) to clip space (-1 to 1)
    let worldPos = (instance.center + scaledPos) * 2.0 - 1.0;

    // Apply aspect ratio correction
    out.position = vec4f(worldPos.x, worldPos.y * u.aspectRatio, 0.0, 1.0);
    out.localPos = vertex.position;  // For SDF antialiasing
    out.color = instance.color;

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // SDF circle for smooth edges
    let dist = length(in.localPos);

    // Smooth antialiasing at the edge
    let alpha = 1.0 - smoothstep(0.95, 1.0, dist);

    if (alpha < 0.01) {
        discard;
    }

    return vec4f(in.color.rgb, in.color.a * alpha);
}
)";

// 2D vertex structure (just position)
struct Vertex2D {
    float x, y;
};

Pipeline2DInternal::Pipeline2DInternal() = default;

Pipeline2DInternal::~Pipeline2DInternal() {
    destroy();
}

Pipeline2DInternal::Pipeline2DInternal(Pipeline2DInternal&& other) noexcept
    : renderer_(other.renderer_)
    , initialized_(other.initialized_)
    , vertexBuffer_(other.vertexBuffer_)
    , indexBuffer_(other.indexBuffer_)
    , indexCount_(other.indexCount_)
    , instanceBuffer_(other.instanceBuffer_)
    , instanceBufferCapacity_(other.instanceBufferCapacity_)
    , shaderModule_(other.shaderModule_)
    , pipeline_(other.pipeline_)
    , bindGroupLayout_(other.bindGroupLayout_)
    , uniformBuffer_(other.uniformBuffer_)
    , sampler_(other.sampler_)
{
    other.renderer_ = nullptr;
    other.initialized_ = false;
    other.vertexBuffer_ = nullptr;
    other.indexBuffer_ = nullptr;
    other.instanceBuffer_ = nullptr;
    other.shaderModule_ = nullptr;
    other.pipeline_ = nullptr;
    other.bindGroupLayout_ = nullptr;
    other.uniformBuffer_ = nullptr;
    other.sampler_ = nullptr;
}

Pipeline2DInternal& Pipeline2DInternal::operator=(Pipeline2DInternal&& other) noexcept {
    if (this != &other) {
        destroy();
        renderer_ = other.renderer_;
        initialized_ = other.initialized_;
        vertexBuffer_ = other.vertexBuffer_;
        indexBuffer_ = other.indexBuffer_;
        indexCount_ = other.indexCount_;
        instanceBuffer_ = other.instanceBuffer_;
        instanceBufferCapacity_ = other.instanceBufferCapacity_;
        shaderModule_ = other.shaderModule_;
        pipeline_ = other.pipeline_;
        bindGroupLayout_ = other.bindGroupLayout_;
        uniformBuffer_ = other.uniformBuffer_;
        sampler_ = other.sampler_;

        other.renderer_ = nullptr;
        other.initialized_ = false;
        other.vertexBuffer_ = nullptr;
        other.indexBuffer_ = nullptr;
        other.instanceBuffer_ = nullptr;
        other.shaderModule_ = nullptr;
        other.pipeline_ = nullptr;
        other.bindGroupLayout_ = nullptr;
        other.uniformBuffer_ = nullptr;
        other.sampler_ = nullptr;
    }
    return *this;
}

bool Pipeline2DInternal::init(Renderer& renderer) {
    renderer_ = &renderer;

    createCircleMesh();
    createPipeline();

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = 16; // resolution (8) + aspectRatio (4) + pad (4)
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    uniformBuffer_ = wgpuDeviceCreateBuffer(renderer_->device(), &uniformDesc);

    initialized_ = true;
    std::cout << "[Pipeline2D] Initialized successfully\n";
    return true;
}

void Pipeline2DInternal::destroy() {
    if (vertexBuffer_) wgpuBufferRelease(vertexBuffer_);
    if (indexBuffer_) wgpuBufferRelease(indexBuffer_);
    if (instanceBuffer_) wgpuBufferRelease(instanceBuffer_);
    if (uniformBuffer_) wgpuBufferRelease(uniformBuffer_);
    if (pipeline_) wgpuRenderPipelineRelease(pipeline_);
    if (bindGroupLayout_) wgpuBindGroupLayoutRelease(bindGroupLayout_);
    if (shaderModule_) wgpuShaderModuleRelease(shaderModule_);

    vertexBuffer_ = nullptr;
    indexBuffer_ = nullptr;
    instanceBuffer_ = nullptr;
    uniformBuffer_ = nullptr;
    pipeline_ = nullptr;
    bindGroupLayout_ = nullptr;
    shaderModule_ = nullptr;
    initialized_ = false;
}

void Pipeline2DInternal::createCircleMesh() {
    // Create a circle as a triangle fan
    const int segments = 32;
    std::vector<Vertex2D> vertices;
    std::vector<uint16_t> indices;

    // Center vertex
    vertices.push_back({0.0f, 0.0f});

    // Perimeter vertices
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159265f;
        vertices.push_back({std::cos(angle), std::sin(angle)});
    }

    // Triangle fan indices
    for (int i = 0; i < segments; i++) {
        indices.push_back(0);           // Center
        indices.push_back(i + 1);       // Current perimeter
        indices.push_back(i + 2);       // Next perimeter
    }

    indexCount_ = static_cast<uint32_t>(indices.size());

    // Create vertex buffer
    WGPUBufferDescriptor vbDesc = {};
    vbDesc.size = vertices.size() * sizeof(Vertex2D);
    vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    vertexBuffer_ = wgpuDeviceCreateBuffer(renderer_->device(), &vbDesc);
    wgpuQueueWriteBuffer(renderer_->queue(), vertexBuffer_, 0,
                         vertices.data(), vbDesc.size);

    // Create index buffer
    WGPUBufferDescriptor ibDesc = {};
    ibDesc.size = indices.size() * sizeof(uint16_t);
    ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    indexBuffer_ = wgpuDeviceCreateBuffer(renderer_->device(), &ibDesc);
    wgpuQueueWriteBuffer(renderer_->queue(), indexBuffer_, 0,
                         indices.data(), ibDesc.size);
}

void Pipeline2DInternal::createPipeline() {
    // Create shader module
    WGPUShaderSourceWGSL wgslSource = {};
    wgslSource.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslSource.code = WGPUStringView{.data = CIRCLE_2D_SHADER, .length = strlen(CIRCLE_2D_SHADER)};

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslSource);
    shaderModule_ = wgpuDeviceCreateShaderModule(renderer_->device(), &shaderDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.minBindingSize = 16;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    bindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(renderer_->device(), &layoutDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bindGroupLayout_;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(
        renderer_->device(), &pipelineLayoutDesc);

    // Vertex attributes
    // Vertex buffer (per-vertex)
    WGPUVertexAttribute vertexAttribs[1] = {};
    vertexAttribs[0].format = WGPUVertexFormat_Float32x2;
    vertexAttribs[0].offset = 0;
    vertexAttribs[0].shaderLocation = 0;

    // Instance buffer (per-instance)
    WGPUVertexAttribute instanceAttribs[4] = {};
    instanceAttribs[0].format = WGPUVertexFormat_Float32x2;  // center
    instanceAttribs[0].offset = 0;
    instanceAttribs[0].shaderLocation = 1;
    instanceAttribs[1].format = WGPUVertexFormat_Float32;    // radius
    instanceAttribs[1].offset = 8;
    instanceAttribs[1].shaderLocation = 2;
    instanceAttribs[2].format = WGPUVertexFormat_Float32;    // pad
    instanceAttribs[2].offset = 12;
    instanceAttribs[2].shaderLocation = 3;
    instanceAttribs[3].format = WGPUVertexFormat_Float32x4;  // color
    instanceAttribs[3].offset = 16;
    instanceAttribs[3].shaderLocation = 4;

    WGPUVertexBufferLayout vertexLayouts[2] = {};
    // Vertex buffer layout
    vertexLayouts[0].arrayStride = sizeof(Vertex2D);
    vertexLayouts[0].stepMode = WGPUVertexStepMode_Vertex;
    vertexLayouts[0].attributeCount = 1;
    vertexLayouts[0].attributes = vertexAttribs;

    // Instance buffer layout
    vertexLayouts[1].arrayStride = sizeof(CircleInstance);
    vertexLayouts[1].stepMode = WGPUVertexStepMode_Instance;
    vertexLayouts[1].attributeCount = 4;
    vertexLayouts[1].attributes = instanceAttribs;

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;

    pipelineDesc.vertex.module = shaderModule_;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};
    pipelineDesc.vertex.bufferCount = 2;
    pipelineDesc.vertex.buffers = vertexLayouts;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;

    // Color target with alpha blending
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
    pipelineDesc.fragment = &fragmentState;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    pipeline_ = wgpuDeviceCreateRenderPipeline(renderer_->device(), &pipelineDesc);
    wgpuPipelineLayoutRelease(pipelineLayout);

    if (!pipeline_) {
        std::cerr << "[Pipeline2D] Failed to create render pipeline\n";
    }
}

void Pipeline2DInternal::drawCircles(const std::vector<CircleInstance>& circles,
                                     Texture& output, const glm::vec4& clearColor) {
    if (!initialized_ || circles.empty()) return;

    auto* outputData = getTextureData(output);
    if (!outputData || !outputData->view) return;

    // Resize instance buffer if needed
    size_t requiredSize = circles.size() * sizeof(CircleInstance);
    if (requiredSize > instanceBufferCapacity_) {
        if (instanceBuffer_) {
            wgpuBufferRelease(instanceBuffer_);
        }
        WGPUBufferDescriptor ibDesc = {};
        ibDesc.size = requiredSize;
        ibDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        instanceBuffer_ = wgpuDeviceCreateBuffer(renderer_->device(), &ibDesc);
        instanceBufferCapacity_ = requiredSize;
    }

    // Upload instance data
    wgpuQueueWriteBuffer(renderer_->queue(), instanceBuffer_, 0,
                         circles.data(), requiredSize);

    // Update uniforms
    float aspect = static_cast<float>(output.width) / static_cast<float>(output.height);
    struct {
        float resX, resY;
        float aspectRatio;
        float _pad;
    } uniforms = {
        static_cast<float>(output.width),
        static_cast<float>(output.height),
        aspect,
        0.0f
    };
    wgpuQueueWriteBuffer(renderer_->queue(), uniformBuffer_, 0,
                         &uniforms, sizeof(uniforms));

    // Create bind group
    WGPUBindGroupEntry entry = {};
    entry.binding = 0;
    entry.buffer = uniformBuffer_;
    entry.size = sizeof(uniforms);

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bindGroupLayout_;
    bgDesc.entryCount = 1;
    bgDesc.entries = &entry;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(renderer_->device(), &bgDesc);

    // Create command encoder
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(renderer_->device(), &encDesc);

    // Begin render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = outputData->view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};

    WGPURenderPassDescriptor rpDesc = {};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder renderPass = wgpuCommandEncoderBeginRenderPass(encoder, &rpDesc);

    // Draw
    wgpuRenderPassEncoderSetPipeline(renderPass, pipeline_);
    wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 0, vertexBuffer_, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(renderPass, 1, instanceBuffer_, 0, requiredSize);
    wgpuRenderPassEncoderSetIndexBuffer(renderPass, indexBuffer_, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(renderPass, indexCount_,
                                      static_cast<uint32_t>(circles.size()), 0, 0, 0);

    wgpuRenderPassEncoderEnd(renderPass);

    // Submit
    WGPUCommandBufferDescriptor cbDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cbDesc);
    wgpuQueueSubmit(renderer_->queue(), 1, &cmdBuffer);

    // Cleanup
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuRenderPassEncoderRelease(renderPass);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bindGroup);
}

} // namespace vivid
