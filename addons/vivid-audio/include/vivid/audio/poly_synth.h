#pragma once

/**
 * @file poly_synth.h
 * @brief Polyphonic synthesizer with multiple voices
 *
 * A polyphonic synthesizer supporting 4-16 simultaneous voices with
 * automatic voice allocation and stealing.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/oscillator.h>
#include <vivid/audio/envelope.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

/**
 * @brief Voice stealing mode
 */
enum class VoiceStealMode {
    Oldest,     ///< Steal the oldest playing voice
    Quietest,   ///< Steal the quietest (lowest envelope) voice
    None        ///< Don't steal - ignore new notes when full
};

/**
 * @brief Polyphonic synthesizer
 *
 * Supports 4-16 simultaneous voices with shared waveform, detune, and
 * ADSR envelope parameters. Includes automatic voice allocation and
 * configurable voice stealing.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | maxVoices | int | 4-16 | 8 | Maximum simultaneous voices |
 * | volume | float | 0-1 | 0.5 | Master output volume |
 * | attack | float | 0.001-5 | 0.01 | Attack time in seconds |
 * | decay | float | 0.001-5 | 0.1 | Decay time in seconds |
 * | sustain | float | 0-1 | 0.7 | Sustain level |
 * | release | float | 0.001-10 | 0.3 | Release time in seconds |
 * | detune | float | -100-100 | 0 | Detune in cents |
 * | unisonDetune | float | 0-50 | 0 | Unison spread in cents |
 *
 * @par Example
 * @code
 * auto& synth = chain.add<PolySynth>("synth");
 * synth.waveform(Waveform::Saw);
 * synth.maxVoices = 8;
 * synth.attack = 0.02f;
 * synth.release = 0.5f;
 *
 * // Play a chord
 * synth.noteOn(freq::C4);
 * synth.noteOn(freq::E4);
 * synth.noteOn(freq::G4);
 *
 * // Release all
 * synth.allNotesOff();
 * @endcode
 */
class PolySynth : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> maxVoices{"maxVoices", 8, 4, 16};                    ///< Maximum voices
    Param<float> volume{"volume", 0.5f, 0.0f, 1.0f};                ///< Master volume
    Param<float> detune{"detune", 0.0f, -100.0f, 100.0f};           ///< Global detune (cents)
    Param<float> unisonDetune{"unisonDetune", 0.0f, 0.0f, 50.0f};   ///< Stereo spread (cents)
    Param<float> pulseWidth{"pulseWidth", 0.5f, 0.01f, 0.99f};      ///< Pulse width

    // Envelope parameters (shared by all voices)
    Param<float> attack{"attack", 0.01f, 0.001f, 5.0f};    ///< Attack time
    Param<float> decay{"decay", 0.1f, 0.001f, 5.0f};       ///< Decay time
    Param<float> sustain{"sustain", 0.7f, 0.0f, 1.0f};     ///< Sustain level
    Param<float> release{"release", 0.3f, 0.001f, 10.0f};  ///< Release time

    /// @}
    // -------------------------------------------------------------------------

    PolySynth();
    ~PolySynth() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set waveform type for all voices
     */
    void waveform(Waveform w) { m_waveform = w; }

    /**
     * @brief Set voice stealing mode
     */
    void stealMode(VoiceStealMode mode) { m_stealMode = mode; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Play a note at the given frequency
     * @param hz Frequency in Hz
     * @return Voice index used, or -1 if no voice available
     */
    int noteOn(float hz);

    /**
     * @brief Release a note at the given frequency
     * @param hz Frequency in Hz (must match noteOn frequency)
     */
    void noteOff(float hz);

    /**
     * @brief Play a MIDI note
     * @param midiNote MIDI note number (60 = middle C)
     * @return Voice index used, or -1 if no voice available
     */
    int noteOnMidi(int midiNote);

    /**
     * @brief Release a MIDI note
     * @param midiNote MIDI note number
     */
    void noteOffMidi(int midiNote);

    /**
     * @brief Release all playing notes
     */
    void allNotesOff();

    /**
     * @brief Immediately silence all voices
     */
    void panic();

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    /**
     * @brief Get number of currently active voices
     */
    int activeVoiceCount() const;

    /**
     * @brief Check if any voices are playing
     */
    bool isPlaying() const { return activeVoiceCount() > 0; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "PolySynth"; }

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    // Voice state
    struct Voice {
        float frequency = 0.0f;
        float phaseL = 0.0f;
        float phaseR = 0.0f;
        EnvelopeStage envStage = EnvelopeStage::Idle;
        float envValue = 0.0f;
        float envProgress = 0.0f;
        float releaseStartValue = 0.0f;
        uint64_t noteId = 0;  // For voice stealing (oldest first)

        bool isActive() const { return envStage != EnvelopeStage::Idle; }
        bool isReleasing() const { return envStage == EnvelopeStage::Release; }
    };

    int findFreeVoice() const;
    int findVoiceToSteal() const;
    int findVoiceByFrequency(float hz) const;
    float generateSample(float phase) const;
    float centsToRatio(float cents) const;
    void processVoice(Voice& voice, float* outputL, float* outputR, uint32_t frames);
    void advanceEnvelope(Voice& voice, uint32_t samples);
    float computeEnvelope(Voice& voice);

    std::vector<Voice> m_voices;
    Waveform m_waveform = Waveform::Saw;
    VoiceStealMode m_stealMode = VoiceStealMode::Oldest;
    uint64_t m_noteCounter = 0;
    uint32_t m_sampleRate = 48000;

    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;
    static constexpr float FREQ_TOLERANCE = 0.5f;  // Hz tolerance for note matching
};

} // namespace vivid::audio
