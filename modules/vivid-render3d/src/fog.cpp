// Fog Post-Processing Effect Implementation

#include <vivid/render3d/fog.h>
#include <vivid/render3d/renderer.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <cstring>

namespace vivid::render3d {

REGISTER_OPERATOR_FULL(Fog, "3D Post-Processing", "Depth-based atmospheric fog effect", true)
    .related({"Render3D", "DepthOfField", "CameraOperator"})
    .limitations({"Requires Render3D depth output enabled"})
    .examples({"modules/vivid-render3d/examples/fog-test"})
    .api({".input(Render3D*)", ".fogColor[3]", ".fogMode = FogMode::Linear|Exponential|ExponentialSquared"});

using namespace vivid::effects;

namespace {

const char* FOG_SHADER_SOURCE = R"(
struct Uniforms {
    fogColor: vec3f,
    fogStart: f32,
    fogEnd: f32,
    fogDensity: f32,
    fogMode: i32,
    nearPlane: f32,
    farPlane: f32,
    _pad: f32,
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

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(colorTexture, texSampler, input.uv);
    let normalizedDepth = textureSample(depthTexture, texSampler, input.uv).r;

    // Convert normalized depth (0-1) to world distance
    let dist = mix(uniforms.nearPlane, uniforms.farPlane, normalizedDepth);

    // Calculate fog factor based on mode
    var factor: f32;
    if (uniforms.fogMode == 0) {
        // Linear fog: smooth transition from start to end
        factor = saturate((uniforms.fogEnd - dist) / (uniforms.fogEnd - uniforms.fogStart));
    } else if (uniforms.fogMode == 1) {
        // Exponential fog: natural atmospheric scattering
        factor = exp(-uniforms.fogDensity * dist);
    } else {
        // Exponential squared fog: denser fog effect
        let d = uniforms.fogDensity * dist;
        factor = exp(-d * d);
    }

    // Mix scene color with fog color based on factor
    // factor = 1.0 means full scene color (no fog)
    // factor = 0.0 means full fog color
    let finalColor = mix(uniforms.fogColor, color.rgb, factor);

    return vec4f(finalColor, color.a);
}
)";

// Uniform struct matching WGSL layout (48 bytes, aligned to 16)
struct FogUniforms {
    float fogColorR;      // offset 0
    float fogColorG;      // offset 4
    float fogColorB;      // offset 8
    float fogStart;       // offset 12
    float fogEnd;         // offset 16
    float fogDensity;     // offset 20
    int fogMode;          // offset 24
    float nearPlane;      // offset 28
    float farPlane;       // offset 32
    float _pad[3];        // offset 36-47
};

} // namespace

Fog::~Fog() {
    cleanup();
}

void Fog::input(Render3D* render) {
    m_render3d = render;
    setInput(0, render);
}

void Fog::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void Fog::createPipeline(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(FOG_SHADER_SOURCE);

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
    bufferDesc.size = sizeof(FogUniforms);
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

void Fog::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    // Match input resolution
    matchInputResolution(0);

    if (!m_render3d || !m_render3d->hasDepthOutput()) {
        // No depth output available - can't apply fog
        return;
    }

    if (!needsCook()) return;

    WGPUDevice device = ctx.device();

    // Get input textures
    WGPUTextureView colorView = m_render3d->outputView();
    WGPUTextureView depthView = m_render3d->depthOutputView();

    if (!colorView || !depthView) return;

    // Get camera near/far planes from the renderer
    m_nearPlane = m_render3d->getNearPlane();
    m_farPlane = m_render3d->getFarPlane();

    // Update uniforms
    FogUniforms uniforms;
    uniforms.fogColorR = fogColor[0];
    uniforms.fogColorG = fogColor[1];
    uniforms.fogColorB = fogColor[2];
    uniforms.fogStart = static_cast<float>(fogStart);
    uniforms.fogEnd = static_cast<float>(fogEnd);
    uniforms.fogDensity = static_cast<float>(fogDensity);
    uniforms.fogMode = static_cast<int>(fogMode);
    uniforms.nearPlane = m_nearPlane;
    uniforms.farPlane = m_farPlane;
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group
    WGPUBindGroupEntry bindEntries[4] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(FogUniforms);

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

    // Render fog
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

void Fog::cleanup() {
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

} // namespace vivid::render3d
