// Vivid Effects 2D - LFO Operator Implementation
// Low-frequency oscillator for animation

#include <vivid/effects/lfo.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::effects {

struct LFOUniforms {
    float time;
    float frequency;
    float amplitude;
    float offset;
    float phase;
    float pulseWidth;
    int waveform;
    float _pad;
};

LFO::~LFO() {
    cleanup();
}

void LFO::init(Context& ctx) {
    if (m_initialized) return;
    createOutput(ctx);
    createPipeline(ctx);
    m_initialized = true;
}

void LFO::createPipeline(Context& ctx) {
    const char* shaderSource = R"(
struct Uniforms {
    time: f32,
    frequency: f32,
    amplitude: f32,
    offset: f32,
    phase: f32,
    pulseWidth: f32,
    waveform: i32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

const PI: f32 = 3.14159265359;
const TAU: f32 = 6.28318530718;

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

fn hash(p: f32) -> f32 {
    var p3 = fract(p * 0.1031);
    p3 += dot(vec3f(p3), vec3f(p3 + 33.33, p3 + 33.33, p3 + 33.33));
    return fract((p3 + p3) * p3);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let t = uniforms.time * uniforms.frequency + uniforms.phase;
    var value: f32;

    if (uniforms.waveform == 0) {
        // Sine
        value = sin(t * TAU) * 0.5 + 0.5;
    } else if (uniforms.waveform == 1) {
        // Triangle
        value = abs(fract(t) * 2.0 - 1.0);
    } else if (uniforms.waveform == 2) {
        // Saw (ascending)
        value = fract(t);
    } else if (uniforms.waveform == 3) {
        // Square
        value = select(0.0, 1.0, fract(t) < uniforms.pulseWidth);
    } else {
        // Noise (sample-hold style)
        value = hash(floor(t));
    }

    // Apply amplitude and offset
    value = value * uniforms.amplitude + uniforms.offset;

    // Output as grayscale texture (useful for modulation)
    return vec4f(value, value, value, 1.0);
}
)";

    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(shaderSource);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = sizeof(LFOUniforms);
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &bufferDesc);

    WGPUBindGroupLayoutEntry entries[1] = {};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;
    entries[0].buffer.minBindingSize = sizeof(LFOUniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = entries;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &layoutDesc);

    WGPUBindGroupEntry bindEntries[1] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_uniformBuffer;
    bindEntries[0].size = sizeof(LFOUniforms);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = bindEntries;
    m_bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindDesc);

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
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
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);
}

void LFO::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    // Calculate current value for CPU access
    float t = static_cast<float>(ctx.time()) * m_frequency + m_phase;
    switch (m_waveform) {
        case LFOWaveform::Sine:
            m_currentValue = std::sin(t * 6.28318530718f) * 0.5f + 0.5f;
            break;
        case LFOWaveform::Triangle:
            m_currentValue = std::abs(std::fmod(t, 1.0f) * 2.0f - 1.0f);
            break;
        case LFOWaveform::Saw:
            m_currentValue = std::fmod(t, 1.0f);
            break;
        case LFOWaveform::Square:
            m_currentValue = std::fmod(t, 1.0f) < m_pulseWidth ? 1.0f : 0.0f;
            break;
        case LFOWaveform::Noise:
            // Simple hash for CPU
            m_currentValue = std::fmod(std::floor(t) * 12.9898f, 1.0f);
            break;
    }
    m_currentValue = m_currentValue * m_amplitude + m_offset;

    LFOUniforms uniforms = {};
    uniforms.time = static_cast<float>(ctx.time());
    uniforms.frequency = m_frequency;
    uniforms.amplitude = m_amplitude;
    uniforms.offset = m_offset;
    uniforms.phase = m_phase;
    uniforms.pulseWidth = m_pulseWidth;
    uniforms.waveform = static_cast<int>(m_waveform);

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    endRenderPass(pass, encoder, ctx);
}

void LFO::cleanup() {
    if (m_pipeline) { wgpuRenderPipelineRelease(m_pipeline); m_pipeline = nullptr; }
    if (m_bindGroup) { wgpuBindGroupRelease(m_bindGroup); m_bindGroup = nullptr; }
    if (m_bindGroupLayout) { wgpuBindGroupLayoutRelease(m_bindGroupLayout); m_bindGroupLayout = nullptr; }
    if (m_uniformBuffer) { wgpuBufferRelease(m_uniformBuffer); m_uniformBuffer = nullptr; }
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
