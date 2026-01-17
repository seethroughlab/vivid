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
#include <vivid/audio/midi_receiver.h>
#include <vivid/operator_registry.h>
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
 
 * @see Synth, FMSynth, WavetableSynth, Oscillator, Envelope, MidiIn
 */
class PolySynth : public AudioOperator, public MidiReceiver {
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
     * @param velocity Note velocity (0.0-1.0, default 1.0)
     * @return Voice index used, or -1 if no voice available
     */
    int noteOn(float hz, float velocity = 1.0f);

    /**
     * @brief Release a note at the given frequency
     * @param hz Frequency in Hz (must match noteOn frequency)
     */
    void noteOff(float hz);

    /**
     * @brief Play a MIDI note
     * @param midiNote MIDI note number (60 = middle C)
     * @param velocity Note velocity (0.0-1.0, default 1.0)
     * @return Voice index used, or -1 if no voice available
     */
    int noteOnMidi(int midiNote, float velocity = 1.0f);

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

    /**
     * @brief Set pitch bend range in semitones
     * @param semitones Bend range (default 2.0 = standard ±2 semitones)
     */
    void setPitchBendRange(float semitones) { m_pitchBendRange = semitones; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name MidiReceiver Interface
    /// @{

    void midiNoteOn(uint8_t note, float velocity, uint8_t channel = 0) override;
    void midiNoteOff(uint8_t note, float velocity = 0.0f, uint8_t channel = 0) override;
    void midiPitchBend(float value, uint8_t channel = 0) override;
    void midiAllNotesOff() override { allNotesOff(); }
    void midiPanic() override { panic(); }

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

    // Visualization access
    Waveform getWaveform() const { return m_waveform; }
    float maxEnvelopeValue() const;  // Returns max envelope across all voices

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

    // Custom visualization
    bool drawVisualization(VizDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

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
        uint64_t noteId = 0;      // For voice stealing (oldest first)
        float velocity = 1.0f;    // Note velocity (0.0-1.0)

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
    float computeEnvelope(const Voice& voice) const;

    std::vector<Voice> m_voices;
    Waveform m_waveform = Waveform::Saw;
    VoiceStealMode m_stealMode = VoiceStealMode::Oldest;
    uint64_t m_noteCounter = 0;
    uint32_t m_sampleRate = 48000;

    // Pitch bend
    float m_pitchBend = 0.0f;       // Current pitch bend value (-1 to +1)
    float m_pitchBendRange = 2.0f;  // Pitch bend range in semitones

    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;
    static constexpr float FREQ_TOLERANCE = 0.5f;  // Hz tolerance for note matching
};

} // namespace vivid::audio
