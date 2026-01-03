#pragma once

/**
 * @file multi_sampler.h
 * @brief Full-featured multi-sample instrument (Kontakt-style)
 *
 * Supports key zones, velocity layers, round-robin, and keyswitches.
 * Loads sample libraries with multiple samples mapped across the keyboard.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/envelope.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <limits>

namespace vivid::audio {

/**
 * @brief Single sample region with key/velocity mapping
 *
 * Defines a sample and the conditions under which it should play
 * (note range, velocity range, etc.)
 */
struct SampleRegion {
    std::string path;                    ///< Path to WAV file
    int rootNote = 60;                   ///< Original pitch of sample (MIDI note)
    int loNote = 0;                      ///< Lowest note this region responds to
    int hiNote = 127;                    ///< Highest note this region responds to
    int loVel = 0;                       ///< Lowest velocity (0-127)
    int hiVel = 127;                     ///< Highest velocity (0-127)
    float volumeDb = 0.0f;               ///< Volume adjustment in dB
    float pan = 0.0f;                    ///< Pan (-1 = left, 0 = center, 1 = right)
    int tuneCents = 0;                   ///< Fine tuning in cents

    // Loop settings
    bool loopEnabled = false;
    uint64_t loopStart = 0;              ///< Loop start in samples
    uint64_t loopEnd = 0;                ///< Loop end in samples (0 = end of file)
    uint64_t loopCrossfade = 0;          ///< Crossfade length in samples

    // Runtime data (populated after loading)
    std::vector<float> samples;          ///< Interleaved stereo sample data
    uint32_t sampleFrames = 0;           ///< Number of frames
    uint32_t sampleRate = 48000;         ///< Sample rate of loaded file
    bool loaded = false;                 ///< Whether sample has been loaded
};

/**
 * @brief Group of samples sharing settings (e.g., an articulation)
 *
 * Multiple groups can exist for keyswitching between articulations.
 */
struct SampleGroup {
    std::string name;                    ///< Group name (e.g., "Sustain", "Staccato")
    std::vector<SampleRegion> regions;   ///< Samples in this group

    // Shared envelope (can override global)
    float attack = -1.0f;                ///< -1 = use global
    float decay = -1.0f;
    float sustain = -1.0f;
    float release = -1.0f;
    float volumeDb = 0.0f;               ///< Group volume adjustment

    int keyswitch = -1;                  ///< MIDI note to activate this group (-1 = none)
};

/**
 * @brief Full-featured multi-sample instrument
 *
 * Loads sample libraries with multiple samples mapped across the keyboard,
 * supporting velocity layers, round-robin, and articulation keyswitches.
 *
 * @par Features
 * - Key zones (different samples per note range)
 * - Velocity layers (pp/p/mf/f samples)
 * - Round-robin (cycle through alternate samples)
 * - Keyswitches (change articulations via MIDI notes)
 * - JSON preset loading (from Decent Sampler conversion)
 *
 * @par Example: Load a preset
 * @code
 * auto& piano = chain.add<MultiSampler>("piano");
 * piano.loadPreset("assets/sample_packs/Ganer Piano/preset.json");
 * piano.attack = 0.01f;
 * piano.release = 1.5f;
 *
 * // Play via MIDI
 * for (const auto& e : midi.events()) {
 *     if (e.type == MidiEventType::NoteOn) {
 *         piano.noteOn(e.note, e.velocity / 127.0f);
 *     } else if (e.type == MidiEventType::NoteOff) {
 *         piano.noteOff(e.note);
 *     }
 * }
 * @endcode
 *
 * @par Example: Manual region setup
 * @code
 * auto& drums = chain.add<MultiSampler>("drums");
 *
 * SampleRegion kick;
 * kick.path = "samples/kick.wav";
 * kick.rootNote = 36;
 * kick.loNote = kick.hiNote = 36;
 * drums.addRegion(kick);
 *
 * SampleRegion snare;
 * snare.path = "samples/snare.wav";
 * snare.rootNote = 38;
 * snare.loNote = snare.hiNote = 38;
 * drums.addRegion(snare);
 * @endcode
 */
class MultiSampler : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> volume{"volume", 0.8f, 0.0f, 2.0f};         ///< Master volume
    Param<int> maxVoices{"maxVoices", 16, 1, 64};            ///< Maximum simultaneous voices

    // Global envelope (can be overridden per group)
    Param<float> attack{"attack", 0.01f, 0.0f, 5.0f};        ///< Attack time in seconds
    Param<float> decay{"decay", 0.1f, 0.0f, 5.0f};           ///< Decay time in seconds
    Param<float> sustain{"sustain", 1.0f, 0.0f, 1.0f};       ///< Sustain level
    Param<float> release{"release", 0.3f, 0.0f, 10.0f};      ///< Release time in seconds

    // Velocity response
    Param<float> velCurve{"velCurve", 0.0f, -1.0f, 1.0f};    ///< -1=soft, 0=linear, 1=hard

    /// @}
    // -------------------------------------------------------------------------

    MultiSampler();
    ~MultiSampler() override = default;

    // -------------------------------------------------------------------------
    /// @name Loading
    /// @{

    /**
     * @brief Load a JSON preset file
     * @param jsonPath Path to preset JSON (uses AssetLoader for resolution)
     * @return true if loaded successfully
     *
     * JSON format (from dspreset_parser.py output):
     * @code
     * {
     *   "name": "Piano",
     *   "samples": [
     *     { "path": "Samples/C4.wav", "root_note": 60, "lo_note": 58, "hi_note": 62, ... }
     *   ],
     *   "envelope": { "attack": 0.01, "release": 0.3 }
     * }
     * @endcode
     */
    bool loadPreset(const std::string& jsonPath);

    /**
     * @brief Load a Decent Sampler .dspreset file directly
     * @param dspresetPath Path to .dspreset XML file
     * @return true if loaded successfully
     *
     * Parses the Decent Sampler XML format and loads all sample mappings.
     * Supports: key zones, velocity layers, loop settings, envelope settings.
     */
    bool loadDspreset(const std::string& dspresetPath);

    /**
     * @brief Add a sample region to the default group
     * @param region Region to add (sample will be loaded on first use)
     */
    void addRegion(const SampleRegion& region);

    /**
     * @brief Add a complete sample group
     * @param group Group with regions
     */
    void addGroup(const SampleGroup& group);

    /**
     * @brief Clear all samples and groups
     */
    void clear();

    /**
     * @brief Get total number of regions across all groups
     */
    int regionCount() const;

    /**
     * @brief Get number of groups
     */
    int groupCount() const { return static_cast<int>(m_groups.size()); }

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
     * @brief Set active group by keyswitch note
     * @param note MIDI note that matches a group's keyswitch
     */
    void setKeyswitch(int note);

    /**
     * @brief Set active group by index
     * @param index Group index (0-based)
     */
    void setActiveGroup(int index);

    /**
     * @brief Get current active group index
     */
    int activeGroupIndex() const { return m_activeGroup; }

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
    std::string name() const override { return "MultiSampler"; }
    void generateBlock(uint32_t frameCount) override;
    bool drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) override;

    /// @}

private:
    // Voice state
    struct Voice {
        int midiNote = -1;
        SampleRegion* region = nullptr;
        double position = 0.0;           // Fractional sample position
        float pitch = 1.0f;              // Playback rate
        float velocity = 1.0f;
        float pan = 0.0f;                // Final pan (region + group)
        float volumeScale = 1.0f;        // Volume from dB adjustments

        EnvelopeStage envStage = EnvelopeStage::Idle;
        float envValue = 0.0f;
        float envProgress = 0.0f;
        float releaseStartValue = 0.0f;
        uint64_t noteId = 0;

        // Envelope times (resolved from group/global)
        float envAttack = 0.01f;
        float envDecay = 0.1f;
        float envSustain = 1.0f;
        float envRelease = 0.3f;

        bool isActive() const { return envStage != EnvelopeStage::Idle; }
        bool isReleasing() const { return envStage == EnvelopeStage::Release; }
    };

    // Sample selection
    SampleRegion* findRegion(int note, int velocity);

    // Voice management
    int findFreeVoice() const;
    int findVoiceToSteal() const;
    int findVoiceByNote(int midiNote) const;

    // Audio processing
    void processVoice(Voice& voice, float* outputL, float* outputR, uint32_t frames);
    void advanceEnvelope(Voice& voice, uint32_t samples);
    float computeEnvelope(const Voice& voice) const;
    float sampleAt(const SampleRegion& region, double position, int channel) const;

    // Pitch calculation
    float pitchFromNote(int playedNote, int rootNote, int tuneCents = 0) const {
        float semitones = static_cast<float>(playedNote - rootNote) +
                          static_cast<float>(tuneCents) / 100.0f;
        return std::pow(2.0f, semitones / 12.0f);
    }

    // Velocity curve application
    float applyVelocityCurve(float velocity) const {
        float curve = static_cast<float>(velCurve);
        if (curve < 0.0f) {
            // Soft curve (more sensitive at low velocities)
            return std::pow(velocity, 1.0f + curve);
        } else if (curve > 0.0f) {
            // Hard curve (less sensitive at low velocities)
            return std::pow(velocity, 1.0f + curve * 2.0f);
        }
        return velocity;  // Linear
    }

    // dB to linear
    float dbToLinear(float db) const {
        return std::pow(10.0f, db / 20.0f);
    }

    // WAV loading
    bool loadWAV(const std::string& path, SampleRegion& region);
    bool ensureLoaded(SampleRegion& region);

    // Sample groups
    std::vector<SampleGroup> m_groups;
    int m_activeGroup = 0;

    // Round-robin state per note
    std::unordered_map<int, int> m_roundRobinIndex;

    // Voice pool
    std::vector<Voice> m_voices;
    uint64_t m_noteCounter = 0;

    // Pending preset path (if set before init)
    std::string m_pendingPreset;
    std::string m_basePath;  // Base path for resolving sample paths

    // Audio settings
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
