// Vivid Effects 2D - Displace Operator Implementation

#include <vivid/effects/displace.h>
#include <vivid/context.h>
#include <cstring>

namespace vivid::effects {

// Uniform buffer structure (16-byte aligned)
struct DisplaceUniforms {
    float strength;
    float strengthX;
    float strengthY;
    float _pad;
};

Displace::~Displace() {
    cleanup();
}

void Displace::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void Displace::createPipeline(Context& ctx) {
    // Embedded shader
    const char* shaderSource = R"(
struct Uniforms {
    strength: f32,
    strengthX: f32,
    strengthY: f32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var sourceTex: texture_2d<f32>;
@group(0) @binding(2) var mapTex: texture_2d<f32>;
@group(0) @binding(3) var texSampler: sampler;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f(3.0, -1.0),
        vec2f(-1.0, 3.0)
    );
    var output: VertexOutput;
    output.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    output.uv = (positions[vertexIndex] + 1.0) * 0.5;
    output.uv.y = 1.0 - output.uv.y;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Sample displacement map
    let displacement = textureSample(mapTex, texSampler, input.uv);

    // Use R and G channels as X and Y displacement
    // Map from [0,1] to [-1,1] range
    let offsetX = (displacement.r - 0.5) * 2.0 * uniforms.strength * uniforms.strengthX;
    let offsetY = (displacement.g - 0.5) * 2.0 * uniforms.strength * uniforms.strengthY;

    // Apply displacement to UV coordinates
    let displacedUV = input.uv + vec2f(offsetX, offsetY);

    // Sample source texture at displaced coordinates
    return textureSample(sourceTex, texSampler, displacedUV);
}
)";

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Displace Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Displace Uniforms");
    bufferDesc.size = sizeof(DisplaceUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(ctx.device(), &samplerDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[4] = {};

    // Uniforms
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = sizeof(DisplaceUniforms);

    // Source texture
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Map texture
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Sampler
    layoutEntries[3].binding = 3;
    layoutEntries[3].visibility = WGPUShaderStage_Fragment;
    layoutEntries[3].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.label = toStringView("Displace Bind Group Layout");
    layoutDesc.entryCount = 4;
    layoutDesc.entries = layoutEntries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &layoutDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

    // Create render pipeline
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = toStringView("Displace Pipeline");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);

    // Cleanup
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void Displace::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    // Match input resolution (from source input)
    matchInputResolution(0);

    // Get input textures
    WGPUTextureView sourceView = inputView(0);
    WGPUTextureView mapView = inputView(1);

    if (!sourceView || !mapView) {
        return;  // Need both inputs
    }

    if (!needsCook()) return;

    // Update uniforms
    DisplaceUniforms uniforms = {};
    uniforms.strength = strength;
    uniforms.strengthX = strengthX;
    uniforms.strengthY = strengthY;

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group with current input textures
    WGPUBindGroupEntry bindEntries[4] = {};

    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].offset = 0;
    bindEntries[0].size = sizeof(DisplaceUniforms);

    bindEntries[1].binding = 1;
    bindEntries[1].textureView = sourceView;

    bindEntries[2].binding = 2;
    bindEntries[2].textureView = mapView;

    bindEntries[3].binding = 3;
    bindEntries[3].sampler = m_sampler;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.label = toStringView("Displace Bind Group");
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 4;
    bindDesc.entries = bindEntries;

    // Release old bind group
    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
    }
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    // Begin render pass
    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);

    // Draw
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    // End render pass
    endRenderPass(pass, encoder, ctx);
    didCook();
}

void Displace::cleanup() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
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

} // namespace vivid::effects
