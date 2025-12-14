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
 * chain.add<Synth>("synth").waveform(Waveform::Saw);
 * auto* synth = chain.get<Synth>("synth");
 * synth->frequency = 440.0f;
 * synth->attack = 0.01f;
 * synth->decay = 0.2f;
 * synth->sustain = 0.5f;
 * synth->release = 0.5f;
 *
 * chain.add<AudioOutput>("out").input("synth");
 * chain.audioOutput("out");
 *
 * // Play a note
 * synth->noteOn();
 * // ... later
 * synth->noteOff();
 * @endcode
 */
class Synth : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    // Oscillator parameters
    Param<float> frequency{"frequency", 440.0f, 20.0f, 20000.0f};  ///< Frequency in Hz
    Param<float> volume{"volume", 0.5f, 0.0f, 1.0f};               ///< Output amplitude
    Param<float> detune{"detune", 0.0f, -100.0f, 100.0f};          ///< Detune in cents
    Param<float> pulseWidth{"pulseWidth", 0.5f, 0.01f, 0.99f};     ///< Pulse width

    // Envelope parameters
    Param<float> attack{"attack", 0.01f, 0.001f, 5.0f};    ///< Attack time in seconds
    Param<float> decay{"decay", 0.1f, 0.001f, 5.0f};       ///< Decay time in seconds
    Param<float> sustain{"sustain", 0.7f, 0.0f, 1.0f};     ///< Sustain level
    Param<float> release{"release", 0.3f, 0.001f, 10.0f};  ///< Release time in seconds

    /// @}
    // -------------------------------------------------------------------------

    Synth() {
        registerParam(frequency);
        registerParam(volume);
        registerParam(detune);
        registerParam(pulseWidth);
        registerParam(attack);
        registerParam(decay);
        registerParam(sustain);
        registerParam(release);
    }
    ~Synth() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set waveform type
     * @param w Waveform (Sine, Triangle, Square, Saw, Pulse)
     */
    Synth& waveform(Waveform w) { m_waveform = w; return *this; }

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

private:
    void noteOnInternal();   // Called from audio thread
    void noteOffInternal();  // Called from audio thread

public:
    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Synth"; }

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    /// @}

private:
    float generateSample(float phase) const;
    float centsToRatio(float cents) const;
    float computeEnvelope();
    void advanceEnvelope(uint32_t samples);

    // Waveform type (enum, not a Param)
    Waveform m_waveform = Waveform::Sine;

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
