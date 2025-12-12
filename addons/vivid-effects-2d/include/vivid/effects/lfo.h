#pragma once

/**
 * @file lfo.h
 * @brief Low-frequency oscillator operator
 *
 * Generates animated oscillating values for parameter modulation.
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>

namespace vivid::effects {

/**
 * @brief Waveform types for LFO
 */
enum class LFOWaveform {
    Sine,       ///< Smooth sinusoidal wave
    Triangle,   ///< Linear ramp up and down
    Saw,        ///< Linear ramp with sharp reset
    Square,     ///< Binary on/off oscillation
    Noise       ///< Random values (sample-and-hold)
};

/**
 * @brief Low-frequency oscillator
 *
 * Generates oscillating values over time for animating parameters.
 * Outputs both a grayscale texture and a scalar value for modulation.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | frequency | float | 0.01-20 | 1.0 | Oscillation frequency in Hz |
 * | amplitude | float | 0-2 | 1.0 | Output amplitude |
 * | offset | float | -1 to 1 | 0.0 | DC offset |
 * | phase | float | 0-1 | 0.0 | Phase offset (0-1 = 0-360°) |
 * | pulseWidth | float | 0-1 | 0.5 | Duty cycle for square wave |
 *
 * @par Example
 * @code
 * chain.add<LFO>("lfo")
 *     .waveform(LFOWaveform::Sine)
 *     .frequency(0.5f)
 *     .amplitude(0.5f)
 *     .offset(0.5f);  // Outputs 0-1 range
 * @endcode
 *
 * @par Inputs
 * None (generator)
 *
 * @par Output
 * - Grayscale texture (OutputKind::Value)
 * - Use outputValue() to get scalar result
 */
class LFO : public TextureOperator {
public:
    LFO() = default;
    ~LFO() override;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set waveform type
     * @param w Waveform (Sine, Triangle, Saw, Square, Noise)
     * @return Reference for chaining
     */
    LFO& waveform(LFOWaveform w) { if (m_waveform != w) { m_waveform = w; markDirty(); } return *this; }

    /**
     * @brief Set oscillation frequency
     * @param f Frequency in Hz (0.01-20, default 1.0)
     * @return Reference for chaining
     */
    LFO& frequency(float f) { if (m_frequency != f) { m_frequency = f; markDirty(); } return *this; }

    /**
     * @brief Set output amplitude
     * @param a Amplitude (0-2, default 1.0)
     * @return Reference for chaining
     */
    LFO& amplitude(float a) { if (m_amplitude != a) { m_amplitude = a; markDirty(); } return *this; }

    /**
     * @brief Set DC offset
     * @param o Offset (-1 to 1, default 0.0)
     * @return Reference for chaining
     */
    LFO& offset(float o) { if (m_offset != o) { m_offset = o; markDirty(); } return *this; }

    /**
     * @brief Set phase offset
     * @param p Phase (0-1 = 0-360°, default 0.0)
     * @return Reference for chaining
     */
    LFO& phase(float p) { if (m_phase != p) { m_phase = p; markDirty(); } return *this; }

    /**
     * @brief Set pulse width (square wave)
     * @param pw Pulse width (0-1, default 0.5)
     * @return Reference for chaining
     */
    LFO& pulseWidth(float pw) { if (m_pulseWidth != pw) { m_pulseWidth = pw; markDirty(); } return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Value Access
    /// @{

    /**
     * @brief Get current oscillator value
     * @return Current value (for CPU-side use)
     */
    float value() const { return m_currentValue; }

    /**
     * @brief Get output value for parameter linking
     * @return Current oscillator value
     */
    float outputValue() const override { return m_currentValue; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "LFO"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    std::vector<ParamDecl> params() override {
        return { m_frequency.decl(), m_amplitude.decl(), m_offset.decl(),
                 m_phase.decl(), m_pulseWidth.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "frequency") { out[0] = m_frequency; return true; }
        if (name == "amplitude") { out[0] = m_amplitude; return true; }
        if (name == "offset") { out[0] = m_offset; return true; }
        if (name == "phase") { out[0] = m_phase; return true; }
        if (name == "pulseWidth") { out[0] = m_pulseWidth; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "frequency") { frequency(value[0]); return true; }
        if (name == "amplitude") { amplitude(value[0]); return true; }
        if (name == "offset") { offset(value[0]); return true; }
        if (name == "phase") { phase(value[0]); return true; }
        if (name == "pulseWidth") { pulseWidth(value[0]); return true; }
        return false;
    }

    /// @}

private:
    void createPipeline(Context& ctx);

    LFOWaveform m_waveform = LFOWaveform::Sine;
    Param<float> m_frequency{"frequency", 1.0f, 0.01f, 20.0f};
    Param<float> m_amplitude{"amplitude", 1.0f, 0.0f, 2.0f};
    Param<float> m_offset{"offset", 0.0f, -1.0f, 1.0f};
    Param<float> m_phase{"phase", 0.0f, 0.0f, 1.0f};
    Param<float> m_pulseWidth{"pulseWidth", 0.5f, 0.0f, 1.0f};
    float m_currentValue = 0.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
