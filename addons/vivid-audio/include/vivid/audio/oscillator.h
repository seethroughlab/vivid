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
    Oscillator() = default;
    ~Oscillator() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set oscillator frequency
     * @param hz Frequency in Hz (20-20000)
     */
    Oscillator& frequency(float hz) { m_frequency = hz; return *this; }

    /**
     * @brief Set waveform type
     * @param w Waveform (Sine, Triangle, Square, Saw, Pulse)
     */
    Oscillator& waveform(Waveform w) { m_waveform = w; return *this; }

    /**
     * @brief Set output volume
     * @param v Volume (0-1)
     */
    Oscillator& volume(float v) { m_volume = v; return *this; }

    /**
     * @brief Set detune in cents
     * @param cents Detune amount (-100 to +100 cents)
     */
    Oscillator& detune(float cents) { m_detune = cents; return *this; }

    /**
     * @brief Set pulse width (for Pulse waveform)
     * @param pw Pulse width (0.01-0.99, 0.5 = square wave)
     */
    Oscillator& pulseWidth(float pw) { m_pulseWidth = pw; return *this; }

    /**
     * @brief Set stereo detune for wider sound
     * @param cents Detune between L/R channels
     */
    Oscillator& stereoDetune(float cents) { m_stereoDetune = cents; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Reset oscillator phase
     */
    void reset() { m_phaseL = 0.0f; m_phaseR = 0.0f; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Oscillator"; }

    std::vector<ParamDecl> params() override {
        return { m_frequency.decl(), m_volume.decl(), m_detune.decl(),
                 m_pulseWidth.decl(), m_stereoDetune.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "frequency") { out[0] = m_frequency; return true; }
        if (name == "volume") { out[0] = m_volume; return true; }
        if (name == "detune") { out[0] = m_detune; return true; }
        if (name == "pulseWidth") { out[0] = m_pulseWidth; return true; }
        if (name == "stereoDetune") { out[0] = m_stereoDetune; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "frequency") { m_frequency = value[0]; return true; }
        if (name == "volume") { m_volume = value[0]; return true; }
        if (name == "detune") { m_detune = value[0]; return true; }
        if (name == "pulseWidth") { m_pulseWidth = value[0]; return true; }
        if (name == "stereoDetune") { m_stereoDetune = value[0]; return true; }
        return false;
    }

    /// @}

private:
    float generateSample(float phase) const;
    float centsToRatio(float cents) const;

    // Parameters
    Waveform m_waveform = Waveform::Sine;
    Param<float> m_frequency{"frequency", 440.0f, 20.0f, 20000.0f};
    Param<float> m_volume{"volume", 0.5f, 0.0f, 1.0f};
    Param<float> m_detune{"detune", 0.0f, -100.0f, 100.0f};
    Param<float> m_pulseWidth{"pulseWidth", 0.5f, 0.01f, 0.99f};
    Param<float> m_stereoDetune{"stereoDetune", 0.0f, 0.0f, 50.0f};

    // State
    float m_phaseL = 0.0f;
    float m_phaseR = 0.0f;
    uint32_t m_sampleRate = 48000;

    bool m_initialized = false;

    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;
};

} // namespace vivid::audio
