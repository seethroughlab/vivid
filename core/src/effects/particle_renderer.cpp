// Vivid Effects 2D - Particle Renderer Implementation
// GPU-instanced rendering for circles and textured sprites

#include <vivid/effects/particle_renderer.h>
#include <vivid/effects/texture_operator.h>
#include <vivid/context.h>
#include <cmath>
#include <iostream>

namespace vivid::effects {

// Circle uniforms (must match shader)
struct CircleUniforms {
    float resolutionX;
    float resolutionY;
    float aspectRatio;
    float _pad;
};

// Sprite uniforms (must match shader)
struct SpriteUniforms {
    float resolutionX;
    float resolutionY;
    float aspectRatio;
    float _pad;
};

// Circle vertex (local mesh position)
struct CircleVertex {
    float x, y;
};

// Sprite vertex (quad corners)
struct SpriteVertex {
    float x, y;    // Position
    float u, v;    // UV coordinates
};

// Circle instance data (matches Circle2D layout)
struct CircleInstance {
    float posX, posY;
    float radius;
    float _pad;
    float r, g, b, a;
};

// Sprite instance data (matches Sprite2D layout)
struct SpriteInstance {
    float posX, posY;
    float size;
    float rotation;
    float r, g, b, a;
    float uvOffsetX, uvOffsetY;
    float uvScaleX, uvScaleY;
};

// WGSL shader for circle rendering (SDF-based)
static const char* CIRCLE_SHADER = R"(
struct Uniforms {
    resolution: vec2f,
    aspectRatio: f32,
    _pad: f32,
}

struct VertexInput {
    @location(0) localPos: vec2f,
}

struct InstanceInput {
    @location(1) center: vec2f,
    @location(2) radiusPad: vec2f,  // radius in .x, _pad in .y
    @location(3) color: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) localPos: vec2f,
    @location(1) color: vec4f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(vert: VertexInput, inst: InstanceInput) -> VertexOutput {
    var output: VertexOutput;

    // Scale local position by radius (stored in radiusPad.x)
    var worldPos = vert.localPos * inst.radiusPad.x;

    // Correct for aspect ratio (make circles circular)
    worldPos.x /= uniforms.aspectRatio;

    // Translate to instance center
    worldPos = worldPos + inst.center;

    // Convert from 0-1 to clip space (-1 to 1)
    var clipPos = worldPos * 2.0 - 1.0;
    clipPos.y = -clipPos.y;  // Flip Y for WebGPU

    output.position = vec4f(clipPos, 0.0, 1.0);
    output.localPos = vert.localPos;
    output.color = inst.color;

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // SDF circle with antialiasing
    let dist = length(input.localPos);

    // Smooth edge with antialiasing
    let edge = smoothstep(1.0, 0.95, dist);

    return vec4f(input.color.rgb, input.color.a * edge);
}
)";

// WGSL shader for sprite rendering (textured quads)
static const char* SPRITE_SHADER = R"(
struct Uniforms {
    resolution: vec2f,
    aspectRatio: f32,
    _pad: f32,
}

struct VertexInput {
    @location(0) localPos: vec2f,
    @location(1) uv: vec2f,
}

struct InstanceInput {
    @location(2) center: vec2f,
    @location(3) sizeRot: vec2f,  // size in .x, rotation in .y
    @location(4) color: vec4f,
    @location(5) uvOffset: vec2f,
    @location(6) uvScale: vec2f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var spriteSampler: sampler;
@group(0) @binding(2) var spriteTexture: texture_2d<f32>;

@vertex
fn vs_main(vert: VertexInput, inst: InstanceInput) -> VertexOutput {
    var output: VertexOutput;

    // Rotate local position (rotation stored in sizeRot.y)
    let c = cos(inst.sizeRot.y);
    let s = sin(inst.sizeRot.y);
    var rotated = vec2f(
        vert.localPos.x * c - vert.localPos.y * s,
        vert.localPos.x * s + vert.localPos.y * c
    );

    // Scale by instance size (stored in sizeRot.x)
    var worldPos = rotated * inst.sizeRot.x;

    // Correct for aspect ratio
    worldPos.x /= uniforms.aspectRatio;

    // Translate to instance center
    worldPos = worldPos + inst.center;

    // Convert from 0-1 to clip space (-1 to 1)
    var clipPos = worldPos * 2.0 - 1.0;
    clipPos.y = -clipPos.y;

    output.position = vec4f(clipPos, 0.0, 1.0);
    output.uv = inst.uvOffset + vert.uv * inst.uvScale;
    output.color = inst.color;

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSample(spriteTexture, spriteSampler, input.uv);
    return texColor * input.color;
}
)";

ParticleRenderer::~ParticleRenderer() {
    cleanup();
}

void ParticleRenderer::init(WGPUDevice device, WGPUQueue queue) {
    if (m_initialized) return;

    m_device = device;
    m_queue = queue;

    createCircleMesh();
    createCirclePipeline();
    createSpriteQuad();
    createSpritePipeline();

    m_initialized = true;
}

void ParticleRenderer::createCircleMesh() {
    // Create a circle using triangle fan: center + 32 perimeter vertices
    const int segments = 32;
    const int vertexCount = segments + 1;  // Center + perimeter
    const int triangleCount = segments;
    const int indexCount = triangleCount * 3;

    std::vector<CircleVertex> vertices(vertexCount);
    std::vector<uint16_t> indices(indexCount);

    // Center vertex at origin
    vertices[0] = {0.0f, 0.0f};

    // Perimeter vertices at unit radius
    for (int i = 0; i < segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159265f;
        vertices[i + 1] = {std::cos(angle), std::sin(angle)};
    }

    // Triangle fan indices
    for (int i = 0; i < segments; i++) {
        int base = i * 3;
        indices[base + 0] = 0;                              // Center
        indices[base + 1] = i + 1;                          // Current perimeter
        indices[base + 2] = (i + 1) % segments + 1;         // Next perimeter (wrap)
    }

    m_circleIndexCount = indexCount;

    // Create vertex buffer
    WGPUBufferDescriptor vertexDesc = {};
    vertexDesc.size = vertices.size() * sizeof(CircleVertex);
    vertexDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_circleVertexBuffer = wgpuDeviceCreateBuffer(m_device, &vertexDesc);
    wgpuQueueWriteBuffer(m_queue, m_circleVertexBuffer, 0, vertices.data(), vertexDesc.size);

    // Create index buffer
    WGPUBufferDescriptor indexDesc = {};
    indexDesc.size = indices.size() * sizeof(uint16_t);
    indexDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    m_circleIndexBuffer = wgpuDeviceCreateBuffer(m_device, &indexDesc);
    wgpuQueueWriteBuffer(m_queue, m_circleIndexBuffer, 0, indices.data(), indexDesc.size);

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(CircleUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_circleUniformBuffer = wgpuDeviceCreateBuffer(m_device, &uniformDesc);
}

void ParticleRenderer::createCirclePipeline() {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(CIRCLE_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);

    // Bind group layout (just uniforms)
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.minBindingSize = sizeof(CircleUniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    m_circleBindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &layoutDesc);

    // Bind group
    WGPUBindGroupEntry bindEntry = {};
    bindEntry.binding = 0;
    bindEntry.buffer = m_circleUniformBuffer;
    bindEntry.size = sizeof(CircleUniforms);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_circleBindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = &bindEntry;
    m_circleBindGroup = wgpuDeviceCreateBindGroup(m_device, &bindDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_circleBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

    // Vertex buffer layouts
    // Buffer 0: Per-vertex (localPos)
    WGPUVertexAttribute vertexAttribs[1] = {};
    vertexAttribs[0].format = WGPUVertexFormat_Float32x2;
    vertexAttribs[0].offset = 0;
    vertexAttribs[0].shaderLocation = 0;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(CircleVertex);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 1;
    vertexLayout.attributes = vertexAttribs;

    // Buffer 1: Per-instance (center, radius, pad, color)
    WGPUVertexAttribute instanceAttribs[3] = {};
    instanceAttribs[0].format = WGPUVertexFormat_Float32x2;  // center
    instanceAttribs[0].offset = 0;
    instanceAttribs[0].shaderLocation = 1;
    instanceAttribs[1].format = WGPUVertexFormat_Float32x2;  // radius + pad
    instanceAttribs[1].offset = 8;
    instanceAttribs[1].shaderLocation = 2;
    instanceAttribs[2].format = WGPUVertexFormat_Float32x4;  // color
    instanceAttribs[2].offset = 16;
    instanceAttribs[2].shaderLocation = 3;

    WGPUVertexBufferLayout instanceLayout = {};
    instanceLayout.arrayStride = sizeof(CircleInstance);
    instanceLayout.stepMode = WGPUVertexStepMode_Instance;
    instanceLayout.attributeCount = 3;
    instanceLayout.attributes = instanceAttribs;

    WGPUVertexBufferLayout bufferLayouts[2] = {vertexLayout, instanceLayout};

    // Alpha blending
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 2;
    pipelineDesc.vertex.buffers = bufferLayouts;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_circlePipeline = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void ParticleRenderer::createSpriteQuad() {
    // Simple quad: two triangles
    SpriteVertex vertices[4] = {
        {-0.5f, -0.5f, 0.0f, 1.0f},  // Bottom-left
        { 0.5f, -0.5f, 1.0f, 1.0f},  // Bottom-right
        { 0.5f,  0.5f, 1.0f, 0.0f},  // Top-right
        {-0.5f,  0.5f, 0.0f, 0.0f},  // Top-left
    };

    WGPUBufferDescriptor vertexDesc = {};
    vertexDesc.size = sizeof(vertices);
    vertexDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_spriteVertexBuffer = wgpuDeviceCreateBuffer(m_device, &vertexDesc);
    wgpuQueueWriteBuffer(m_queue, m_spriteVertexBuffer, 0, vertices, sizeof(vertices));

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(SpriteUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_spriteUniformBuffer = wgpuDeviceCreateBuffer(m_device, &uniformDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;  // Must be at least 1
    m_spriteSampler = wgpuDeviceCreateSampler(m_device, &samplerDesc);
}

void ParticleRenderer::createSpritePipeline() {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(SPRITE_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(m_device, &shaderDesc);

    // Bind group layout (uniforms + sampler + texture)
    WGPUBindGroupLayoutEntry layoutEntries[3] = {};
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Vertex;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = sizeof(SpriteUniforms);

    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 3;
    layoutDesc.entries = layoutEntries;
    m_spriteBindGroupLayout = wgpuDeviceCreateBindGroupLayout(m_device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_spriteBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(m_device, &pipelineLayoutDesc);

    // Vertex buffer layouts
    // Buffer 0: Per-vertex (localPos + uv)
    WGPUVertexAttribute vertexAttribs[2] = {};
    vertexAttribs[0].format = WGPUVertexFormat_Float32x2;
    vertexAttribs[0].offset = 0;
    vertexAttribs[0].shaderLocation = 0;
    vertexAttribs[1].format = WGPUVertexFormat_Float32x2;
    vertexAttribs[1].offset = 8;
    vertexAttribs[1].shaderLocation = 1;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(SpriteVertex);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 2;
    vertexLayout.attributes = vertexAttribs;

    // Buffer 1: Per-instance
    WGPUVertexAttribute instanceAttribs[5] = {};
    instanceAttribs[0].format = WGPUVertexFormat_Float32x2;  // center
    instanceAttribs[0].offset = 0;
    instanceAttribs[0].shaderLocation = 2;
    instanceAttribs[1].format = WGPUVertexFormat_Float32x2;  // size + rotation
    instanceAttribs[1].offset = 8;
    instanceAttribs[1].shaderLocation = 3;
    instanceAttribs[2].format = WGPUVertexFormat_Float32x4;  // color
    instanceAttribs[2].offset = 16;
    instanceAttribs[2].shaderLocation = 4;
    instanceAttribs[3].format = WGPUVertexFormat_Float32x2;  // uvOffset
    instanceAttribs[3].offset = 32;
    instanceAttribs[3].shaderLocation = 5;
    instanceAttribs[4].format = WGPUVertexFormat_Float32x2;  // uvScale
    instanceAttribs[4].offset = 40;
    instanceAttribs[4].shaderLocation = 6;

    WGPUVertexBufferLayout instanceLayout = {};
    instanceLayout.arrayStride = sizeof(SpriteInstance);
    instanceLayout.stepMode = WGPUVertexStepMode_Instance;
    instanceLayout.attributeCount = 5;
    instanceLayout.attributes = instanceAttribs;

    WGPUVertexBufferLayout bufferLayouts[2] = {vertexLayout, instanceLayout};

    // Alpha blending
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 2;
    pipelineDesc.vertex.buffers = bufferLayouts;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleStrip;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_spritePipeline = wgpuDeviceCreateRenderPipeline(m_device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void ParticleRenderer::ensureInstanceCapacity(size_t count, bool forSprites) {
    if (forSprites) {
        if (count <= m_spriteInstanceCapacity) return;

        // Release old buffer
        if (m_spriteInstanceBuffer) {
            wgpuBufferRelease(m_spriteInstanceBuffer);
        }

        // Allocate with some headroom
        m_spriteInstanceCapacity = count + count / 4;

        WGPUBufferDescriptor bufferDesc = {};
        bufferDesc.size = m_spriteInstanceCapacity * sizeof(SpriteInstance);
        bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_spriteInstanceBuffer = wgpuDeviceCreateBuffer(m_device, &bufferDesc);
    } else {
        if (count <= m_circleInstanceCapacity) return;

        // Release old buffer
        if (m_circleInstanceBuffer) {
            wgpuBufferRelease(m_circleInstanceBuffer);
        }

        // Allocate with some headroom
        m_circleInstanceCapacity = count + count / 4;

        WGPUBufferDescriptor bufferDesc = {};
        bufferDesc.size = m_circleInstanceCapacity * sizeof(CircleInstance);
        bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_circleInstanceBuffer = wgpuDeviceCreateBuffer(m_device, &bufferDesc);
    }
}

void ParticleRenderer::renderCircles(Context& ctx,
                                     const std::vector<Circle2D>& circles,
                                     WGPUTextureView output,
                                     int outputWidth,
                                     int outputHeight,
                                     const glm::vec4& clearColor) {
    if (circles.empty()) return;
    if (!m_initialized) return;

    // Ensure instance buffer is large enough
    ensureInstanceCapacity(circles.size(), false);

    // Upload circle data (Circle2D and CircleInstance have same layout)
    wgpuQueueWriteBuffer(m_queue, m_circleInstanceBuffer, 0,
                         circles.data(), circles.size() * sizeof(Circle2D));

    // Update uniforms
    CircleUniforms uniforms = {};
    uniforms.resolutionX = static_cast<float>(outputWidth);
    uniforms.resolutionY = static_cast<float>(outputHeight);
    uniforms.aspectRatio = uniforms.resolutionX / uniforms.resolutionY;
    wgpuQueueWriteBuffer(m_queue, m_circleUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encoderDesc);

    // Begin render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = output;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Draw circles
    wgpuRenderPassEncoderSetPipeline(pass, m_circlePipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_circleBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_circleVertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_circleInstanceBuffer, 0,
                                         circles.size() * sizeof(CircleInstance));
    wgpuRenderPassEncoderSetIndexBuffer(pass, m_circleIndexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, m_circleIndexCount, circles.size(), 0, 0, 0);

    // End render pass
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(m_queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
}

void ParticleRenderer::renderSprites(Context& ctx,
                                     const std::vector<Sprite2D>& sprites,
                                     WGPUTextureView spriteTexture,
                                     WGPUTextureView output,
                                     int outputWidth,
                                     int outputHeight,
                                     const glm::vec4& clearColor) {
    if (sprites.empty()) return;
    if (!m_initialized) return;
    if (!spriteTexture) return;

    // Ensure instance buffer is large enough
    ensureInstanceCapacity(sprites.size(), true);

    // Upload sprite data (Sprite2D and SpriteInstance have same layout)
    wgpuQueueWriteBuffer(m_queue, m_spriteInstanceBuffer, 0,
                         sprites.data(), sprites.size() * sizeof(Sprite2D));

    // Update uniforms
    SpriteUniforms uniforms = {};
    uniforms.resolutionX = static_cast<float>(outputWidth);
    uniforms.resolutionY = static_cast<float>(outputHeight);
    uniforms.aspectRatio = uniforms.resolutionX / uniforms.resolutionY;
    wgpuQueueWriteBuffer(m_queue, m_spriteUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group with the provided texture
    WGPUBindGroupEntry bindEntries[3] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_spriteUniformBuffer;
    bindEntries[0].size = sizeof(SpriteUniforms);
    bindEntries[1].binding = 1;
    bindEntries[1].sampler = m_spriteSampler;
    bindEntries[2].binding = 2;
    bindEntries[2].textureView = spriteTexture;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_spriteBindGroupLayout;
    bindDesc.entryCount = 3;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(m_device, &bindDesc);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encoderDesc);

    // Begin render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = output;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r, clearColor.g, clearColor.b, clearColor.a};

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Draw sprites
    wgpuRenderPassEncoderSetPipeline(pass, m_spritePipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_spriteVertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_spriteInstanceBuffer, 0,
                                         sprites.size() * sizeof(SpriteInstance));
    wgpuRenderPassEncoderDraw(pass, 4, sprites.size(), 0, 0);  // 4 vertices per quad

    // End render pass
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(m_queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    wgpuBindGroupRelease(bindGroup);
}

void ParticleRenderer::cleanup() {
    if (m_circlePipeline) { wgpuRenderPipelineRelease(m_circlePipeline); m_circlePipeline = nullptr; }
    if (m_circleVertexBuffer) { wgpuBufferRelease(m_circleVertexBuffer); m_circleVertexBuffer = nullptr; }
    if (m_circleIndexBuffer) { wgpuBufferRelease(m_circleIndexBuffer); m_circleIndexBuffer = nullptr; }
    if (m_circleInstanceBuffer) { wgpuBufferRelease(m_circleInstanceBuffer); m_circleInstanceBuffer = nullptr; }
    if (m_circleUniformBuffer) { wgpuBufferRelease(m_circleUniformBuffer); m_circleUniformBuffer = nullptr; }
    if (m_circleBindGroupLayout) { wgpuBindGroupLayoutRelease(m_circleBindGroupLayout); m_circleBindGroupLayout = nullptr; }
    if (m_circleBindGroup) { wgpuBindGroupRelease(m_circleBindGroup); m_circleBindGroup = nullptr; }

    if (m_spritePipeline) { wgpuRenderPipelineRelease(m_spritePipeline); m_spritePipeline = nullptr; }
    if (m_spriteVertexBuffer) { wgpuBufferRelease(m_spriteVertexBuffer); m_spriteVertexBuffer = nullptr; }
    if (m_spriteInstanceBuffer) { wgpuBufferRelease(m_spriteInstanceBuffer); m_spriteInstanceBuffer = nullptr; }
    if (m_spriteUniformBuffer) { wgpuBufferRelease(m_spriteUniformBuffer); m_spriteUniformBuffer = nullptr; }
    if (m_spriteBindGroupLayout) { wgpuBindGroupLayoutRelease(m_spriteBindGroupLayout); m_spriteBindGroupLayout = nullptr; }
    if (m_spriteSampler) { wgpuSamplerRelease(m_spriteSampler); m_spriteSampler = nullptr; }

    m_initialized = false;
    m_circleInstanceCapacity = 0;
    m_spriteInstanceCapacity = 0;
}

} // namespace vivid::effects
