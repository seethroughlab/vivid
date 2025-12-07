// Vivid Effects 2D - Composite Operator Implementation

#include <vivid/effects/composite.h>
#include <vivid/context.h>

namespace vivid::effects {

struct CompositeUniforms {
    int mode;
    float opacity;
    float _padding[2];
};

Composite::~Composite() {
    cleanup();
}

void Composite::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void Composite::createPipeline(Context& ctx) {
    const char* shaderSource = R"(
struct Uniforms {
    mode: i32,
    opacity: f32,
    _padding: vec2f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var texA: texture_2d<f32>;
@group(0) @binding(3) var texB: texture_2d<f32>;

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

fn blendOver(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    let a = blend.a * opacity;
    return vec4f(mix(base.rgb, blend.rgb, a), max(base.a, a));
}

fn blendAdd(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    return vec4f(base.rgb + blend.rgb * blend.a * opacity, max(base.a, blend.a * opacity));
}

fn blendMultiply(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    let result = base.rgb * blend.rgb;
    return vec4f(mix(base.rgb, result, blend.a * opacity), base.a);
}

fn blendScreen(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    let result = 1.0 - (1.0 - base.rgb) * (1.0 - blend.rgb);
    return vec4f(mix(base.rgb, result, blend.a * opacity), max(base.a, blend.a * opacity));
}

fn blendOverlay(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    var result: vec3f;
    for (var i = 0; i < 3; i++) {
        if (base[i] < 0.5) {
            result[i] = 2.0 * base[i] * blend[i];
        } else {
            result[i] = 1.0 - 2.0 * (1.0 - base[i]) * (1.0 - blend[i]);
        }
    }
    return vec4f(mix(base.rgb, result, blend.a * opacity), max(base.a, blend.a * opacity));
}

fn blendDifference(base: vec4f, blend: vec4f, opacity: f32) -> vec4f {
    let result = abs(base.rgb - blend.rgb);
    return vec4f(mix(base.rgb, result, blend.a * opacity), max(base.a, blend.a * opacity));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let a = textureSample(texA, texSampler, input.uv);
    let b = textureSample(texB, texSampler, input.uv);

    var result: vec4f;
    switch(uniforms.mode) {
        case 0: { result = blendOver(a, b, uniforms.opacity); }       // Over
        case 1: { result = blendAdd(a, b, uniforms.opacity); }        // Add
        case 2: { result = blendMultiply(a, b, uniforms.opacity); }   // Multiply
        case 3: { result = blendScreen(a, b, uniforms.opacity); }     // Screen
        case 4: { result = blendOverlay(a, b, uniforms.opacity); }    // Overlay
        case 5: { result = blendDifference(a, b, uniforms.opacity); } // Difference
        default: { result = blendOver(a, b, uniforms.opacity); }
    }

    return result;
}
)";

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Composite Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    // Create uniform buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Composite Uniforms");
    bufferDesc.size = sizeof(CompositeUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    m_sampler = wgpuDeviceCreateSampler(ctx.device(), &samplerDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[4] = {};

    // Uniform buffer
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = sizeof(CompositeUniforms);

    // Sampler
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    // Texture A
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    // Texture B
    layoutEntries[3].binding = 3;
    layoutEntries[3].visibility = WGPUShaderStage_Fragment;
    layoutEntries[3].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[3].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.label = toStringView("Composite Bind Group Layout");
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
    pipelineDesc.label = toStringView("Composite Pipeline");
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

void Composite::updateBindGroup(Context& ctx) {
    WGPUTextureView inputA = inputView(0);
    WGPUTextureView inputB = inputView(1);

    // Check if we need to recreate the bind group
    if (m_bindGroup && inputA == m_lastInputA && inputB == m_lastInputB) {
        return;
    }

    if (m_bindGroup) {
        wgpuBindGroupRelease(m_bindGroup);
        m_bindGroup = nullptr;
    }

    if (!inputA || !inputB) {
        return;
    }

    WGPUBindGroupEntry entries[4] = {};

    // Uniform buffer
    entries[0].binding = 0;
    entries[0].buffer = m_uniformBuffer;
    entries[0].offset = 0;
    entries[0].size = sizeof(CompositeUniforms);

    // Sampler
    entries[1].binding = 1;
    entries[1].sampler = m_sampler;

    // Texture A
    entries[2].binding = 2;
    entries[2].textureView = inputA;

    // Texture B
    entries[3].binding = 3;
    entries[3].textureView = inputB;

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.label = toStringView("Composite Bind Group");
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 4;
    bindDesc.entries = entries;
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    m_lastInputA = inputA;
    m_lastInputB = inputB;
}

void Composite::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    updateBindGroup(ctx);

    if (!m_bindGroup) {
        return; // Missing inputs
    }

    // Update uniforms
    CompositeUniforms uniforms = {};
    uniforms.mode = static_cast<int>(m_mode);
    uniforms.opacity = m_opacity;
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

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
}

void Composite::cleanup() {
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
    m_lastInputA = nullptr;
    m_lastInputB = nullptr;
}

} // namespace vivid::effects
