#pragma once

// Vivid Effects 2D - LFO Operator
// Low-frequency oscillator for animation (generates grayscale texture)

#include <vivid/effects/texture_operator.h>

namespace vivid::effects {

enum class LFOWaveform {
    Sine,
    Triangle,
    Saw,
    Square,
    Noise
};

class LFO : public TextureOperator {
public:
    LFO() = default;
    ~LFO() override;

    // Fluent API
    LFO& waveform(LFOWaveform w) { m_waveform = w; return *this; }
    LFO& frequency(float f) { m_frequency = f; return *this; }
    LFO& amplitude(float a) { m_amplitude = a; return *this; }
    LFO& offset(float o) { m_offset = o; return *this; }
    LFO& phase(float p) { m_phase = p; return *this; }
    LFO& pulseWidth(float pw) { m_pulseWidth = pw; return *this; }  // For square wave

    // Get current value (for CPU-side use)
    float value() const { return m_currentValue; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "LFO"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

private:
    void createPipeline(Context& ctx);

    LFOWaveform m_waveform = LFOWaveform::Sine;
    float m_frequency = 1.0f;   // Hz
    float m_amplitude = 1.0f;
    float m_offset = 0.0f;
    float m_phase = 0.0f;
    float m_pulseWidth = 0.5f;
    float m_currentValue = 0.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
