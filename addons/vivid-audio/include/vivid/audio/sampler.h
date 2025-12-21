#pragma once

/**
 * @file sampler.h
 * @brief Polyphonic sampler instrument (Simpler-style)
 *
 * Loads a single sample and plays it chromatically across the keyboard
 * with pitch shifting and per-voice ADSR envelopes.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/envelope.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <cmath>

namespace vivid::audio {

/**
 * @brief Voice stealing mode
 */
enum class SamplerVoiceStealMode {
    Oldest,     ///< Steal the oldest playing voice
    Quietest,   ///< Steal the quietest (lowest envelope) voice
    None        ///< Don't steal - ignore new notes when full
};

/**
 * @brief Polyphonic sampler instrument
 *
 * Loads a single WAV sample and plays it at different pitches based on
 * MIDI note input. Similar to Ableton's Simpler instrument.
 *
 * @par Features
 * - Polyphonic playback (up to 32 voices)
 * - Pitch shifting based on root note
 * - Per-voice ADSR envelope
 * - Optional loop points
 * - Voice stealing (oldest/quietest)
 *
 * @par Example
 * @code
 * auto& sampler = chain.add<Sampler>("piano");
 * sampler.loadSample("assets/piano_c4.wav");
 * sampler.rootNote = 60;  // Sample is C4
 * sampler.attack = 0.01f;
 * sampler.release = 1.0f;
 *
 * // Play via MIDI
 * for (const auto& e : midi.events()) {
 *     if (e.type == MidiEventType::NoteOn) {
 *         sampler.noteOn(e.note, e.velocity / 127.0f);
 *     } else if (e.type == MidiEventType::NoteOff) {
 *         sampler.noteOff(e.note);
 *     }
 * }
 * @endcode
 */
class Sampler : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> volume{"volume", 0.8f, 0.0f, 2.0f};         ///< Master volume
    Param<int> rootNote{"rootNote", 60, 0, 127};             ///< MIDI note of original sample pitch
    Param<int> maxVoices{"maxVoices", 8, 1, 32};             ///< Maximum simultaneous voices

    // Envelope parameters (shared by all voices)
    Param<float> attack{"attack", 0.01f, 0.0f, 5.0f};        ///< Attack time in seconds
    Param<float> decay{"decay", 0.1f, 0.0f, 5.0f};           ///< Decay time in seconds
    Param<float> sustain{"sustain", 1.0f, 0.0f, 1.0f};       ///< Sustain level
    Param<float> release{"release", 0.3f, 0.0f, 10.0f};      ///< Release time in seconds

    /// @}
    // -------------------------------------------------------------------------

    Sampler();
    ~Sampler() override = default;

    // -------------------------------------------------------------------------
    /// @name Sample Loading
    /// @{

    /**
     * @brief Load a sample from file
     * @param path Path to WAV file (uses AssetLoader for resolution)
     * @return true if loaded successfully
     */
    bool loadSample(const std::string& path);

    /**
     * @brief Check if a sample is loaded
     */
    bool hasSample() const { return !m_samples.empty(); }

    /**
     * @brief Get sample duration in seconds
     */
    float sampleDuration() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Loop Control
    /// @{

    /**
     * @brief Enable/disable looping
     */
    void setLoop(bool enabled) { m_loopEnabled = enabled; }

    /**
     * @brief Set loop points in seconds
     * @param startSec Loop start time
     * @param endSec Loop end time (0 = end of sample)
     */
    void setLoopPoints(float startSec, float endSec);

    bool isLooping() const { return m_loopEnabled; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Play a note
     * @param midiNote MIDI note number (60 = middle C)
     * @param velocity Velocity (0-1)
     * @return Voice index used, or -1 if no voice available
     */
    int noteOn(int midiNote, float velocity = 1.0f);

    /**
     * @brief Release a note
     * @param midiNote MIDI note number
     */
    void noteOff(int midiNote);

    /**
     * @brief Release all playing notes
     */
    void allNotesOff();

    /**
     * @brief Immediately silence all voices
     */
    void panic();

    /**
     * @brief Set voice stealing mode
     */
    void setVoiceStealMode(SamplerVoiceStealMode mode) { m_stealMode = mode; }

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
    std::string name() const override { return "Sampler"; }
    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    // Voice state
    struct Voice {
        int midiNote = -1;
        double position = 0.0;           // Fractional sample position
        float pitch = 1.0f;              // Playback rate (calculated from note)
        float velocity = 1.0f;

        EnvelopeStage envStage = EnvelopeStage::Idle;
        float envValue = 0.0f;
        float envProgress = 0.0f;
        float releaseStartValue = 0.0f;
        uint64_t noteId = 0;

        bool isActive() const { return envStage != EnvelopeStage::Idle; }
        bool isReleasing() const { return envStage == EnvelopeStage::Release; }
    };

    // Voice management
    int findFreeVoice() const;
    int findVoiceToSteal() const;
    int findVoiceByNote(int midiNote) const;

    // Audio processing
    void processVoice(Voice& voice, float* outputL, float* outputR, uint32_t frames);
    void advanceEnvelope(Voice& voice, uint32_t samples);
    float computeEnvelope(const Voice& voice) const;
    float sampleAt(double position, int channel) const;

    // Pitch calculation
    float pitchFromNote(int midiNote) const {
        int semitones = midiNote - static_cast<int>(rootNote);
        return std::pow(2.0f, semitones / 12.0f);
    }

    // WAV loading
    bool loadWAV(const std::string& path);

    // Sample data
    std::vector<float> m_samples;        // Interleaved stereo samples
    uint32_t m_sampleFrames = 0;
    uint32_t m_sampleRate = 48000;
    uint32_t m_channels = 2;
    std::string m_pendingPath;

    // Loop settings
    bool m_loopEnabled = false;
    uint64_t m_loopStart = 0;            // In samples
    uint64_t m_loopEnd = 0;              // In samples (0 = end)

    // Voice pool
    std::vector<Voice> m_voices;
    SamplerVoiceStealMode m_stealMode = SamplerVoiceStealMode::Oldest;
    uint64_t m_noteCounter = 0;
};

} // namespace vivid::audio
