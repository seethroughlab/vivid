#pragma once

/**
 * @file lfo.h
 * @brief Low-frequency oscillator operator
 *
 * Generates animated oscillating values for parameter modulation.
 */

#include <vivid/effects/simple_texture_effect.h>
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
 * @brief Uniform buffer structure for LFO shader
 */
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
class LFO : public SimpleGeneratorEffect<LFO, LFOUniforms> {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> frequency{"frequency", 1.0f, 0.01f, 20.0f};   ///< Oscillation frequency in Hz
    Param<float> amplitude{"amplitude", 1.0f, 0.0f, 2.0f};     ///< Output amplitude
    Param<float> offset{"offset", 0.0f, -1.0f, 1.0f};          ///< DC offset
    Param<float> phase{"phase", 0.0f, 0.0f, 1.0f};             ///< Phase offset (0-1 = 0-360°)
    Param<float> pulseWidth{"pulseWidth", 0.5f, 0.0f, 1.0f};   ///< Duty cycle for square wave

    /// @}
    // -------------------------------------------------------------------------

    LFO() {
        registerParam(frequency);
        registerParam(amplitude);
        registerParam(offset);
        registerParam(phase);
        registerParam(pulseWidth);
    }
    ~LFO() override;

    /// @brief Set waveform type (Sine, Triangle, Saw, Square, Noise)
    void waveform(LFOWaveform w) { if (m_waveform != w) { m_waveform = w; markDirty(); } }

    // -------------------------------------------------------------------------
    /// @name Value Access
    /// @{

    /// @brief Get current oscillator value (for CPU-side use)
    float value() const { return m_currentValue; }

    /// @brief Get output value for parameter linking
    float outputValue() const override { return m_currentValue; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void process(Context& ctx) override;
    std::string name() const override { return "LFO"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name CRTP Interface
    /// @{

    /// @brief Get uniform values for GPU rendering
    LFOUniforms getUniforms() const {
        LFOUniforms uniforms = {};
        // Note: time is set in process() as it requires Context
        uniforms.frequency = frequency;
        uniforms.amplitude = amplitude;
        uniforms.offset = offset;
        uniforms.phase = phase;
        uniforms.pulseWidth = pulseWidth;
        uniforms.waveform = static_cast<int>(m_waveform);
        return uniforms;
    }

    /// @brief Return the WGSL fragment shader source
    const char* fragmentShader() const override;

    /// @}

private:
    LFOWaveform m_waveform = LFOWaveform::Sine;
    float m_currentValue = 0.0f;
};

#ifdef _WIN32
extern template class SimpleGeneratorEffect<LFO, LFOUniforms>;
#endif

} // namespace vivid::effects
