// Vivid Effects 2D - LFO Operator Implementation
// Low-frequency oscillator for animation

#include <vivid/effects/lfo.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>
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

    // Use shared command encoder for batched submission
    WGPUCommandEncoder encoder = ctx.gpuEncoder();

    WGPURenderPassEncoder pass;
    beginRenderPass(pass, encoder);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, m_bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    endRenderPass(pass, encoder, ctx);

    didCook();
}

bool LFO::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    viz.drawBackground(bounds);

    // Sample waveform at 32 points
    constexpr int NUM_SAMPLES = 32;
    float w = bounds.w - 8;
    float h = bounds.h - 16;  // Leave room for label
    float xStart = bounds.x + 4;
    float yMid = bounds.y + 4 + h * 0.5f;

    // Get current phase to show position marker
    float freq = static_cast<float>(frequency);
    float amp = static_cast<float>(amplitude);
    float off = static_cast<float>(offset);
    float ph = static_cast<float>(phase);
    float pw = static_cast<float>(pulseWidth);

    // Draw waveform line
    uint32_t waveColor = VIZ_COL32(100, 200, 255, 255);  // Cyan
    float lastX = 0, lastY = 0;

    for (int i = 0; i <= NUM_SAMPLES; ++i) {
        float t = static_cast<float>(i) / NUM_SAMPLES;  // 0 to 1
        float value = 0.0f;

        // Sample the waveform at this phase
        switch (m_waveform) {
            case LFOWaveform::Sine:
                value = std::sin(t * 6.28318530718f) * 0.5f + 0.5f;
                break;
            case LFOWaveform::Triangle:
                value = std::abs(std::fmod(t, 1.0f) * 2.0f - 1.0f);
                break;
            case LFOWaveform::Saw:
                value = t;
                break;
            case LFOWaveform::Square:
                value = t < pw ? 1.0f : 0.0f;
                break;
            case LFOWaveform::Noise:
                // Just show a zigzag for noise
                value = (i % 4 < 2) ? 0.3f : 0.7f;
                break;
        }

        // Apply amplitude and offset for display
        value = value * amp + off;
        value = std::clamp(value, -0.5f, 1.5f);  // Clamp for display

        float x = xStart + t * w;
        float y = yMid - (value - 0.5f) * h * 0.8f;  // Invert Y, center at 0.5

        if (i > 0) {
            dl->AddLine({lastX, lastY}, {x, y}, waveColor, 1.5f);
        }
        lastX = x;
        lastY = y;
    }

    // Draw position marker (vertical line at current phase)
    float currentPhase = std::fmod(ph, 1.0f);
    float markerX = xStart + currentPhase * w;
    uint32_t markerColor = VIZ_COL32(255, 200, 100, 200);  // Gold
    dl->AddLine({markerX, bounds.y + 4}, {markerX, bounds.y + 4 + h}, markerColor, 1.0f);

    // Draw current value as horizontal line
    float valueY = yMid - (m_currentValue - 0.5f) * h * 0.8f;
    dl->AddLine({xStart, valueY}, {xStart + w, valueY}, VIZ_COL32(255, 100, 100, 100), 1.0f);

    // Draw small dot at current value position
    dl->AddCircleFilled({markerX, valueY}, 3.0f, VIZ_COL32(255, 200, 100, 255));

    // Draw waveform name and frequency at bottom
    const char* waveNames[] = {"Sine", "Tri", "Saw", "Sqr", "Noise"};
    int waveIdx = static_cast<int>(m_waveform);
    if (waveIdx < 0 || waveIdx > 4) waveIdx = 0;

    char label[32];
    snprintf(label, sizeof(label), "%s %.1fHz", waveNames[waveIdx], freq);
    VizBounds labelBounds = bounds.splitBottom(0.2f);
    viz.drawLabel(labelBounds, label, VizColors::TextSecondary);

    return true;
}

// Explicit template instantiation for Windows hot-reload
template class SimpleGeneratorEffect<LFO, LFOUniforms>;

} // namespace vivid::effects
