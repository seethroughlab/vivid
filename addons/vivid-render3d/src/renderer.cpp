#include <vivid/render3d/renderer.h>
#include <vivid/render3d/scene_composer.h>
#include <vivid/context.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace vivid::render3d {

using namespace vivid::effects;

namespace {
constexpr WGPUTextureFormat DEPTH_FORMAT = WGPUTextureFormat_Depth24Plus;
// toStringView is inherited from vivid::effects (texture_operator.h)

// Shader source embedded in code
const char* SHADER_SOURCE = R"(
struct Uniforms {
    mvp: mat4x4f,
    model: mat4x4f,
    lightDir: vec3f,
    _pad1: f32,
    lightColor: vec3f,
    ambient: f32,
    baseColor: vec4f,
    shadingMode: u32,
    _pad2: vec3f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) color: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldNormal: vec3f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f,
    @location(3) lighting: f32,
};

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.position = uniforms.mvp * vec4f(in.position, 1.0);
    out.worldNormal = normalize((uniforms.model * vec4f(in.normal, 0.0)).xyz);
    out.uv = in.uv;
    out.color = in.color;

    if (uniforms.shadingMode == 2u) {
        let NdotL = max(dot(out.worldNormal, uniforms.lightDir), 0.0);
        out.lighting = uniforms.ambient + NdotL;
    } else {
        out.lighting = 1.0;
    }
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    var finalColor = uniforms.baseColor * in.color;

    if (uniforms.shadingMode == 0u) {
        return finalColor;
    } else if (uniforms.shadingMode == 1u) {
        let NdotL = max(dot(normalize(in.worldNormal), uniforms.lightDir), 0.0);
        let lighting = uniforms.ambient + NdotL * uniforms.lightColor;
        return vec4f(finalColor.rgb * lighting, finalColor.a);
    } else {
        return vec4f(finalColor.rgb * in.lighting * uniforms.lightColor, finalColor.a);
    }
}
)";

// Uniform buffer structure - must match WGSL shader layout exactly
// WGSL uses std140-like alignment rules
struct Uniforms {
    float mvp[16];         // mat4x4f: 64 bytes, offset 0
    float model[16];       // mat4x4f: 64 bytes, offset 64
    float lightDir[3];     // vec3f: 12 bytes, offset 128
    float _pad1;           // f32: 4 bytes, offset 140
    float lightColor[3];   // vec3f: 12 bytes, offset 144
    float ambient;         // f32: 4 bytes, offset 156
    float baseColor[4];    // vec4f: 16 bytes, offset 160
    uint32_t shadingMode;  // u32: 4 bytes, offset 176
    float _pad2[3];        // vec3f: 12 bytes padding, offset 180
    // wgpu adds extra padding to 208 bytes
    float _pad3[4];        // 16 bytes extra, offset 192
};                         // Total: 208 bytes

static_assert(sizeof(Uniforms) == 208, "Uniforms struct must be 208 bytes");

} // anonymous namespace

Render3D::Render3D() {
    m_camera.lookAt(glm::vec3(3, 2, 3), glm::vec3(0, 0, 0));
}

Render3D::~Render3D() {
    cleanup();
}

Render3D& Render3D::scene(Scene& s) {
    m_scene = &s;
    m_composer = nullptr;  // Clear composer when scene is set directly
    return *this;
}

Render3D& Render3D::input(SceneComposer* composer) {
    m_composer = composer;
    if (composer) {
        setInput(0, composer);  // Register for dependency tracking
    }
    return *this;
}

Render3D& Render3D::camera(const Camera3D& cam) {
    m_camera = cam;
    return *this;
}

Render3D& Render3D::shadingMode(ShadingMode mode) {
    m_shadingMode = mode;
    return *this;
}

Render3D& Render3D::color(float r, float g, float b, float a) {
    m_defaultColor = glm::vec4(r, g, b, a);
    return *this;
}

Render3D& Render3D::color(const glm::vec4& c) {
    m_defaultColor = c;
    return *this;
}

Render3D& Render3D::lightDirection(glm::vec3 dir) {
    m_lightDirection = glm::normalize(dir);
    return *this;
}

Render3D& Render3D::lightColor(glm::vec3 color) {
    m_lightColor = color;
    return *this;
}

Render3D& Render3D::ambient(float a) {
    m_ambient = a;
    return *this;
}

Render3D& Render3D::clearColor(float r, float g, float b, float a) {
    m_clearColor = glm::vec4(r, g, b, a);
    return *this;
}

Render3D& Render3D::wireframe(bool enabled) {
    m_wireframe = enabled;
    return *this;
}

void Render3D::init(Context& ctx) {
    if (m_initialized) return;

    // Use inherited createOutput() from TextureOperator
    createOutput(ctx);

    createDepthBuffer(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void Render3D::createDepthBuffer(Context& ctx) {
    WGPUDevice device = ctx.device();

    WGPUTextureDescriptor depthDesc = {};
    depthDesc.label = toStringView("Render3D Depth");
    depthDesc.size.width = m_width;
    depthDesc.size.height = m_height;
    depthDesc.size.depthOrArrayLayers = 1;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    depthDesc.dimension = WGPUTextureDimension_2D;
    depthDesc.format = DEPTH_FORMAT;
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;

    m_depthTexture = wgpuDeviceCreateTexture(device, &depthDesc);

    WGPUTextureViewDescriptor depthViewDesc = {};
    depthViewDesc.format = DEPTH_FORMAT;
    depthViewDesc.dimension = WGPUTextureViewDimension_2D;
    depthViewDesc.mipLevelCount = 1;
    depthViewDesc.arrayLayerCount = 1;
    m_depthView = wgpuTextureCreateView(m_depthTexture, &depthViewDesc);
}

void Render3D::createPipeline(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(SHADER_SOURCE);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Render3D Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Query device limits for uniform buffer alignment
    WGPULimits limits = {};
    wgpuDeviceGetLimits(device, &limits);
    m_uniformAlignment = limits.minUniformBufferOffsetAlignment;
    if (m_uniformAlignment < sizeof(Uniforms)) {
        m_uniformAlignment = ((sizeof(Uniforms) + 255) / 256) * 256;  // Round up to 256
    }

    // Create uniform buffer large enough for MAX_OBJECTS
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Render3D Uniforms");
    bufferDesc.size = m_uniformAlignment * MAX_OBJECTS;
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Create bind group layout with dynamic offset
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.hasDynamicOffset = true;
    layoutEntry.buffer.minBindingSize = sizeof(Uniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.label = toStringView("Render3D Bind Group Layout");
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Vertex attributes for Vertex3D
    WGPUVertexAttribute vertexAttrs[4] = {};

    // position: vec3f at offset 0
    vertexAttrs[0].format = WGPUVertexFormat_Float32x3;
    vertexAttrs[0].offset = 0;
    vertexAttrs[0].shaderLocation = 0;

    // normal: vec3f at offset 12
    vertexAttrs[1].format = WGPUVertexFormat_Float32x3;
    vertexAttrs[1].offset = 12;
    vertexAttrs[1].shaderLocation = 1;

    // uv: vec2f at offset 24
    vertexAttrs[2].format = WGPUVertexFormat_Float32x2;
    vertexAttrs[2].offset = 24;
    vertexAttrs[2].shaderLocation = 2;

    // color: vec4f at offset 32
    vertexAttrs[3].format = WGPUVertexFormat_Float32x4;
    vertexAttrs[3].offset = 32;
    vertexAttrs[3].shaderLocation = 3;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(Vertex3D);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 4;
    vertexLayout.attributes = vertexAttrs;

    // Color target
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Depth stencil state
    WGPUDepthStencilState depthStencil = {};
    depthStencil.format = DEPTH_FORMAT;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencil.depthCompare = WGPUCompareFunction_Less;

    // Main pipeline (filled triangles)
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = toStringView("Render3D Pipeline");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_Back;
    pipelineDesc.depthStencil = &depthStencil;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // Wireframe pipeline
    pipelineDesc.label = toStringView("Render3D Wireframe Pipeline");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_LineList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    m_wireframePipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // Cleanup
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void Render3D::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // If using a composer, get the scene from it
    Scene* sceneToRender = m_scene;
    if (m_composer) {
        sceneToRender = &m_composer->outputScene();
    }

    if (!sceneToRender || sceneToRender->empty()) {
        return;
    }

    WGPUDevice device = ctx.device();

    // Update camera aspect ratio
    m_camera.aspect(static_cast<float>(m_width) / m_height);
    glm::mat4 viewProj = m_camera.viewProjectionMatrix();

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    // Begin render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a};

    WGPURenderPassDepthStencilAttachment depthAttachment = {};
    depthAttachment.view = m_depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &depthAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    // Set pipeline
    wgpuRenderPassEncoderSetPipeline(pass, m_wireframe ? m_wireframePipeline : m_pipeline);

    // First pass: write all uniform data to buffer at different offsets
    size_t numObjects = sceneToRender->objects().size();
    for (size_t i = 0; i < numObjects && i < MAX_OBJECTS; i++) {
        const auto& obj = sceneToRender->objects()[i];
        if (!obj.mesh || !obj.mesh->valid()) {
            continue;
        }

        // Compute MVP matrix for this object
        glm::mat4 mvp = viewProj * obj.transform;
        glm::vec4 objColor = obj.color * m_defaultColor;

        // Update uniforms
        Uniforms uniforms = {};
        memcpy(uniforms.mvp, glm::value_ptr(mvp), sizeof(uniforms.mvp));
        memcpy(uniforms.model, glm::value_ptr(obj.transform), sizeof(uniforms.model));
        uniforms.lightDir[0] = m_lightDirection.x;
        uniforms.lightDir[1] = m_lightDirection.y;
        uniforms.lightDir[2] = m_lightDirection.z;
        uniforms.lightColor[0] = m_lightColor.x;
        uniforms.lightColor[1] = m_lightColor.y;
        uniforms.lightColor[2] = m_lightColor.z;
        uniforms.ambient = m_ambient;
        uniforms.baseColor[0] = objColor.r;
        uniforms.baseColor[1] = objColor.g;
        uniforms.baseColor[2] = objColor.b;
        uniforms.baseColor[3] = objColor.a;
        uniforms.shadingMode = static_cast<uint32_t>(m_shadingMode);

        // Write to buffer at offset for this object
        size_t offset = i * m_uniformAlignment;
        wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, offset, &uniforms, sizeof(uniforms));
    }

    // Create a single bind group for dynamic offset usage
    WGPUBindGroupEntry bindEntry = {};
    bindEntry.binding = 0;
    bindEntry.buffer = m_uniformBuffer;
    bindEntry.offset = 0;
    bindEntry.size = sizeof(Uniforms);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = &bindEntry;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Second pass: render each object with its dynamic offset
    for (size_t i = 0; i < numObjects && i < MAX_OBJECTS; i++) {
        const auto& obj = sceneToRender->objects()[i];
        if (!obj.mesh || !obj.mesh->valid()) {
            continue;
        }

        uint32_t dynamicOffset = static_cast<uint32_t>(i * m_uniformAlignment);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 1, &dynamicOffset);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, obj.mesh->vertexBuffer(), 0,
                                              obj.mesh->vertexCount() * sizeof(Vertex3D));
        wgpuRenderPassEncoderSetIndexBuffer(pass, obj.mesh->indexBuffer(),
                                             WGPUIndexFormat_Uint32, 0,
                                             obj.mesh->indexCount() * sizeof(uint32_t));
        wgpuRenderPassEncoderDrawIndexed(pass, obj.mesh->indexCount(), 1, 0, 0, 0);
    }

    wgpuBindGroupRelease(bindGroup);

    // End render pass
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit commands
    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
}

void Render3D::cleanup() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_wireframePipeline) {
        wgpuRenderPipelineRelease(m_wireframePipeline);
        m_wireframePipeline = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_uniformBuffer) {
        wgpuBufferRelease(m_uniformBuffer);
        m_uniformBuffer = nullptr;
    }
    if (m_depthView) {
        wgpuTextureViewRelease(m_depthView);
        m_depthView = nullptr;
    }
    if (m_depthTexture) {
        wgpuTextureDestroy(m_depthTexture);
        wgpuTextureRelease(m_depthTexture);
        m_depthTexture = nullptr;
    }

    // Use inherited releaseOutput() from TextureOperator
    releaseOutput();

    m_initialized = false;
}

} // namespace vivid::render3d
