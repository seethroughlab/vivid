#pragma once

/**
 * @file oscillator.h
 * @brief Audio oscillator for sound synthesis
 *
 * Generates basic waveforms at audio frequencies for synthesis.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <cmath>

namespace vivid::audio {

/**
 * @brief Oscillator waveform types
 */
enum class Waveform {
    Sine,       ///< Pure sine wave - smooth, fundamental tone
    Triangle,   ///< Triangle wave - softer than square, odd harmonics
    Square,     ///< Square wave - hollow, reedy sound, odd harmonics
    Saw,        ///< Sawtooth wave - bright, buzzy, all harmonics
    Pulse       ///< Variable pulse width - adjustable timbre
};

/**
 * @brief Audio oscillator for synthesis
 *
 * Generates periodic waveforms at audio frequencies. Unlike the LFO
 * (which operates at low frequencies for modulation), this oscillator
 * is designed for audio-rate synthesis.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | frequency | float | 20-20000 | 440.0 | Oscillator frequency in Hz |
 * | volume | float | 0-1 | 0.5 | Output amplitude |
 * | detune | float | -100-100 | 0.0 | Detune in cents |
 * | pulseWidth | float | 0.01-0.99 | 0.5 | Pulse width (Pulse waveform only) |
 *
 * @par Example
 * @code
 * chain.add<Oscillator>("osc")
 *     .frequency(440.0f)
 *     .waveform(Waveform::Saw)
 *     .volume(0.5f);
 *
 * chain.add<AudioOutput>("out").input("osc");
 * chain.audioOutput("out");
 * @endcode
 */
class Oscillator : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> frequency{"frequency", 440.0f, 20.0f, 20000.0f};     ///< Frequency in Hz
    Param<float> volume{"volume", 0.5f, 0.0f, 1.0f};                  ///< Output volume
    Param<float> detune{"detune", 0.0f, -100.0f, 100.0f};             ///< Detune in cents
    Param<float> pulseWidth{"pulseWidth", 0.5f, 0.01f, 0.99f};        ///< Pulse width (Pulse waveform only)
    Param<float> stereoDetune{"stereoDetune", 0.0f, 0.0f, 50.0f};     ///< Stereo detune in cents

    /// @}
    // -------------------------------------------------------------------------

    Oscillator() {
        registerParam(frequency);
        registerParam(volume);
        registerParam(detune);
        registerParam(pulseWidth);
        registerParam(stereoDetune);
    }
    ~Oscillator() override = default;

    /// @brief Set waveform type
    void waveform(Waveform w) { m_waveform = w; }

    /// @brief Reset oscillator phase
    void reset() { m_phaseL = 0.0f; m_phaseR = 0.0f; }

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Oscillator"; }

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    float generateSample(float phase) const;
    float centsToRatio(float cents) const;

    // Waveform (enum, not a Param)
    Waveform m_waveform = Waveform::Sine;

    // State
    float m_phaseL = 0.0f;
    float m_phaseR = 0.0f;
    uint32_t m_sampleRate = 48000;

    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;
};

} // namespace vivid::audio
