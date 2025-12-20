// Vivid Effects 2D - LFO Operator Implementation
// Low-frequency oscillator for animation

#include <vivid/effects/lfo.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::effects {

LFO::~LFO() = default;

const char* LFO::fragmentShader() const {
    return R"(
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

const PI: f32 = 3.14159265359;
const TAU: f32 = 6.28318530718;

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
}

void LFO::process(Context& ctx) {
    // Calculate current value for CPU access (LFO-specific logic)
    float t = static_cast<float>(ctx.time()) * frequency + phase;
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
            m_currentValue = std::fmod(t, 1.0f) < static_cast<float>(pulseWidth) ? 1.0f : 0.0f;
            break;
        case LFOWaveform::Noise:
            // Simple hash for CPU
            m_currentValue = std::fmod(std::floor(t) * 12.9898f, 1.0f);
            break;
    }
    m_currentValue = m_currentValue * amplitude + offset;

    // Call base class process for GPU rendering, but we need to handle time specially
    if (!isInitialized()) init(ctx);

    // Get uniforms and set time (which getUniforms() cannot do)
    LFOUniforms uniforms = getUniforms();
    uniforms.time = static_cast<float>(ctx.time());

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Execute render pass (same as base class)
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    endRenderPass(pass, encoder, ctx);

    didCook();
}

// Explicit template instantiation for Windows hot-reload
template class SimpleGeneratorEffect<LFO, LFOUniforms>;

} // namespace vivid::effects
