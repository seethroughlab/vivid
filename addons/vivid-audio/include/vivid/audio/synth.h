#pragma once

/**
 * @file synth.h
 * @brief Simple synthesizer combining oscillator and envelope
 *
 * A complete voice combining waveform generation with ADSR envelope.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/oscillator.h>
#include <vivid/audio/envelope.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Simple synthesizer voice
 *
 * Combines an oscillator and ADSR envelope into a single operator for
 * convenient sound synthesis. Supports multiple waveforms and full
 * envelope control.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | frequency | float | 20-20000 | 440.0 | Oscillator frequency in Hz |
 * | volume | float | 0-1 | 0.5 | Output amplitude |
 * | attack | float | 0.001-5 | 0.01 | Attack time in seconds |
 * | decay | float | 0.001-5 | 0.1 | Decay time in seconds |
 * | sustain | float | 0-1 | 0.7 | Sustain level |
 * | release | float | 0.001-10 | 0.3 | Release time in seconds |
 *
 * @par Example
 * @code
 * chain.add<Synth>("synth")
 *     .frequency(440.0f)
 *     .waveform(Waveform::Saw)
 *     .attack(0.01f)
 *     .decay(0.2f)
 *     .sustain(0.5f)
 *     .release(0.5f);
 *
 * chain.add<AudioOutput>("out").input("synth");
 * chain.audioOutput("out");
 *
 * // Play a note
 * chain.get<Synth>("synth")->noteOn();
 * // ... later
 * chain.get<Synth>("synth")->noteOff();
 * @endcode
 */
class Synth : public AudioOperator {
public:
    Synth() = default;
    ~Synth() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API - Oscillator
    /// @{

    /**
     * @brief Set oscillator frequency
     * @param hz Frequency in Hz (20-20000)
     */
    Synth& frequency(float hz) { m_frequency = hz; return *this; }

    /**
     * @brief Set waveform type
     * @param w Waveform (Sine, Triangle, Square, Saw, Pulse)
     */
    Synth& waveform(Waveform w) { m_waveform = w; return *this; }

    /**
     * @brief Set output volume
     * @param v Volume (0-1)
     */
    Synth& volume(float v) { m_volume = v; return *this; }

    /**
     * @brief Set detune in cents
     * @param cents Detune amount (-100 to +100 cents)
     */
    Synth& detune(float cents) { m_detune = cents; return *this; }

    /**
     * @brief Set pulse width (for Pulse waveform)
     * @param pw Pulse width (0.01-0.99)
     */
    Synth& pulseWidth(float pw) { m_pulseWidth = pw; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Fluent API - Envelope
    /// @{

    /**
     * @brief Set attack time
     * @param seconds Attack time (0.001-5 seconds)
     */
    Synth& attack(float seconds) { m_attack = seconds; return *this; }

    /**
     * @brief Set decay time
     * @param seconds Decay time (0.001-5 seconds)
     */
    Synth& decay(float seconds) { m_decay = seconds; return *this; }

    /**
     * @brief Set sustain level
     * @param level Sustain level (0-1)
     */
    Synth& sustain(float level) { m_sustain = level; return *this; }

    /**
     * @brief Set release time
     * @param seconds Release time (0.001-10 seconds)
     */
    Synth& release(float seconds) { m_release = seconds; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Trigger note on (start envelope attack)
     */
    void noteOn();

    /**
     * @brief Trigger note off (start envelope release)
     */
    void noteOff();

    /**
     * @brief Play a note with specific frequency
     * @param hz Frequency in Hz
     */
    void noteOn(float hz);

    /**
     * @brief Check if note is currently playing
     */
    bool isPlaying() const { return m_envStage != EnvelopeStage::Idle; }

    /**
     * @brief Reset synth to initial state
     */
    void reset();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Synth"; }

    std::vector<ParamDecl> params() override {
        return {
            m_frequency.decl(), m_volume.decl(), m_detune.decl(), m_pulseWidth.decl(),
            m_attack.decl(), m_decay.decl(), m_sustain.decl(), m_release.decl()
        };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "frequency") { out[0] = m_frequency; return true; }
        if (name == "volume") { out[0] = m_volume; return true; }
        if (name == "detune") { out[0] = m_detune; return true; }
        if (name == "pulseWidth") { out[0] = m_pulseWidth; return true; }
        if (name == "attack") { out[0] = m_attack; return true; }
        if (name == "decay") { out[0] = m_decay; return true; }
        if (name == "sustain") { out[0] = m_sustain; return true; }
        if (name == "release") { out[0] = m_release; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "frequency") { m_frequency = value[0]; return true; }
        if (name == "volume") { m_volume = value[0]; return true; }
        if (name == "detune") { m_detune = value[0]; return true; }
        if (name == "pulseWidth") { m_pulseWidth = value[0]; return true; }
        if (name == "attack") { m_attack = value[0]; return true; }
        if (name == "decay") { m_decay = value[0]; return true; }
        if (name == "sustain") { m_sustain = value[0]; return true; }
        if (name == "release") { m_release = value[0]; return true; }
        return false;
    }

    /// @}

private:
    float generateSample(float phase) const;
    float centsToRatio(float cents) const;
    float computeEnvelope();
    void advanceEnvelope(uint32_t samples);

    // Oscillator parameters
    Waveform m_waveform = Waveform::Sine;
    Param<float> m_frequency{"frequency", 440.0f, 20.0f, 20000.0f};
    Param<float> m_volume{"volume", 0.5f, 0.0f, 1.0f};
    Param<float> m_detune{"detune", 0.0f, -100.0f, 100.0f};
    Param<float> m_pulseWidth{"pulseWidth", 0.5f, 0.01f, 0.99f};

    // Envelope parameters
    Param<float> m_attack{"attack", 0.01f, 0.001f, 5.0f};
    Param<float> m_decay{"decay", 0.1f, 0.001f, 5.0f};
    Param<float> m_sustain{"sustain", 0.7f, 0.0f, 1.0f};
    Param<float> m_release{"release", 0.3f, 0.001f, 10.0f};

    // Oscillator state
    float m_phase = 0.0f;
    uint32_t m_sampleRate = 48000;

    // Envelope state
    EnvelopeStage m_envStage = EnvelopeStage::Idle;
    float m_envValue = 0.0f;
    float m_envProgress = 0.0f;
    float m_releaseStartValue = 0.0f;

    bool m_initialized = false;

    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;
};

} // namespace vivid::audio
