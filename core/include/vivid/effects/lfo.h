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

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "LFO"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    /// @}

private:
    void createPipeline(Context& ctx);

    LFOWaveform m_waveform = LFOWaveform::Sine;
    float m_currentValue = 0.0f;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::effects
