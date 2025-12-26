#pragma once

/**
 * @file wavetable_synth.h
 * @brief Polyphonic wavetable synthesizer with morphing timbres
 *
 * The audio equivalent of procedural textures - rich, evolving sounds from
 * mathematical wavetables. Supports built-in tables, custom loading, and
 * programmatic generation.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/envelope.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <cmath>

namespace vivid::audio {

/**
 * @brief Built-in wavetable presets
 */
enum class BuiltinTable {
    Basic,      ///< Sine -> Triangle -> Saw -> Square (classic morph)
    Analog,     ///< Warm, slightly detuned classics with subtle harmonics
    Digital,    ///< Harsh, FM-like timbres with sharp edges
    Vocal,      ///< Formant-based vowel sounds (A-E-I-O-U)
    Texture,    ///< Noise-based, granular feel with organic movement
    PWM         ///< Pulse width modulation sweep from thin to thick
};

/**
 * @brief Phase warp modes for timbral variety
 *
 * Warp modes transform the oscillator phase before wavetable lookup,
 * dramatically expanding the timbral range from a single wavetable.
 */
enum class WarpMode {
    None,       ///< No warping (default)
    Sync,       ///< Hard sync - resets phase at warp frequency
    BendPlus,   ///< Phase bend up - emphasizes attack/brightness
    BendMinus,  ///< Phase bend down - softer, rounder tone
    Mirror,     ///< Mirror at midpoint - creates symmetrical waveform
    Asym,       ///< Asymmetric - positive half stretched
    Quantize,   ///< Bit-reduce phase - lo-fi stepped effect
    FM,         ///< Self-FM - phase modulated by own output
    Flip        ///< Flip second half - octave-up harmonic content
};

/**
 * @brief Filter types for WavetableSynth per-voice filtering
 */
enum class SynthFilterType {
    LP12,       ///< 12dB/oct low-pass (2-pole)
    LP24,       ///< 24dB/oct low-pass (4-pole, classic subtractive)
    HP12,       ///< 12dB/oct high-pass
    BP,         ///< Band-pass
    Notch       ///< Notch (band-reject)
};

/**
 * @brief Polyphonic wavetable synthesizer
 *
 * Morphs through multi-frame wavetables for evolving timbres. Includes
 * built-in tables covering classic analog sounds, FM-style digital tones,
 * vocal formants, and textural noise.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | position | float | 0-1 | 0.0 | Morph position through wavetable |
 * | maxVoices | int | 1-8 | 4 | Maximum simultaneous voices |
 * | detune | float | 0-50 | 0 | Voice detune spread in cents |
 * | volume | float | 0-1 | 0.5 | Output volume |
 * | attack | float | 0.001-5 | 0.01 | Attack time in seconds |
 * | decay | float | 0.001-5 | 0.1 | Decay time in seconds |
 * | sustain | float | 0-1 | 0.7 | Sustain level |
 * | release | float | 0.001-10 | 0.3 | Release time in seconds |
 *
 * @par Example
 * @code
 * auto& wt = chain.add<WavetableSynth>("wt");
 * wt.loadBuiltin(BuiltinTable::Analog);
 * wt.maxVoices = 4;
 * wt.attack = 0.1f;
 * wt.release = 0.5f;
 *
 * // Modulate wavetable position for evolving timbre
 * wt.position = lfo.value();
 *
 * // Play a chord
 * wt.noteOn(freq::C4);
 * wt.noteOn(freq::E4);
 * wt.noteOn(freq::G4);
 * @endcode
 */
class WavetableSynth : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> position{"position", 0.0f, 0.0f, 1.0f};    ///< Wavetable morph position
    Param<int> maxVoices{"maxVoices", 4, 1, 8};             ///< Maximum voices
    Param<float> detune{"detune", 0.0f, 0.0f, 50.0f};       ///< Detune spread in cents
    Param<float> volume{"volume", 0.5f, 0.0f, 1.0f};        ///< Output volume

    // Envelope parameters (shared by all voices)
    Param<float> attack{"attack", 0.01f, 0.001f, 5.0f};     ///< Attack time
    Param<float> decay{"decay", 0.1f, 0.001f, 5.0f};        ///< Decay time
    Param<float> sustain{"sustain", 0.7f, 0.0f, 1.0f};      ///< Sustain level
    Param<float> release{"release", 0.3f, 0.001f, 10.0f};   ///< Release time

    // Unison parameters
    Param<int> unisonVoices{"unisonVoices", 1, 1, 8};       ///< Unison voice count
    Param<float> unisonSpread{"unisonSpread", 20.0f, 0.0f, 100.0f}; ///< Detune spread in cents
    Param<float> unisonStereo{"unisonStereo", 1.0f, 0.0f, 1.0f};    ///< Stereo width (0=mono, 1=full)

    // Sub oscillator
    Param<float> subLevel{"subLevel", 0.0f, 0.0f, 1.0f};    ///< Sub oscillator level
    Param<int> subOctave{"subOctave", -1, -2, -1};          ///< Sub octave (-1 or -2)

    // Portamento
    Param<float> portamento{"portamento", 0.0f, 0.0f, 2000.0f}; ///< Glide time in ms

    // Velocity sensitivity
    Param<float> velToVolume{"velToVolume", 1.0f, 0.0f, 1.0f};  ///< Velocity to volume amount
    Param<float> velToAttack{"velToAttack", 0.0f, -1.0f, 1.0f}; ///< Velocity to attack modulation

    // Warp parameters
    Param<float> warpAmount{"warpAmount", 0.0f, 0.0f, 1.0f};    ///< Warp intensity (0=off)

    // Filter parameters
    Param<float> filterCutoff{"filterCutoff", 20000.0f, 20.0f, 20000.0f};  ///< Filter cutoff Hz
    Param<float> filterResonance{"filterResonance", 0.0f, 0.0f, 1.0f};    ///< Filter resonance (0-1)
    Param<float> filterKeytrack{"filterKeytrack", 0.0f, 0.0f, 1.0f};      ///< Cutoff tracks pitch

    // Filter envelope
    Param<float> filterAttack{"filterAttack", 0.01f, 0.001f, 10.0f};      ///< Filter env attack
    Param<float> filterDecay{"filterDecay", 0.3f, 0.001f, 10.0f};         ///< Filter env decay
    Param<float> filterSustain{"filterSustain", 0.0f, 0.0f, 1.0f};        ///< Filter env sustain
    Param<float> filterRelease{"filterRelease", 0.3f, 0.001f, 10.0f};     ///< Filter env release
    Param<float> filterEnvAmount{"filterEnvAmount", 0.0f, -1.0f, 1.0f};   ///< Env to cutoff amount

    /// @}
    // -------------------------------------------------------------------------

    WavetableSynth();
    ~WavetableSynth() override = default;

    // -------------------------------------------------------------------------
    /// @name Warp Mode
    /// @{

    /**
     * @brief Set the phase warp mode
     * @param mode The warp mode to use
     *
     * Warp modes transform the phase before wavetable lookup, creating
     * dramatic timbral variations from a single wavetable.
     */
    void setWarpMode(WarpMode mode) { m_warpMode = mode; }

    /**
     * @brief Get the current warp mode
     */
    WarpMode warpMode() const { return m_warpMode; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Filter
    /// @{

    /**
     * @brief Set the filter type
     * @param type The filter type to use
     */
    void setFilterType(SynthFilterType type) { m_filterType = type; }

    /**
     * @brief Get the current filter type
     */
    SynthFilterType filterType() const { return m_filterType; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Wavetable Loading
    /// @{

    /**
     * @brief Load a built-in wavetable preset
     * @param table The preset to load
     */
    void loadBuiltin(BuiltinTable table);

    /**
     * @brief Load wavetable from audio file (single-cycle frames)
     * @param path Path to audio file (WAV format)
     * @param framesPerCycle Samples per waveform cycle (default 2048)
     * @return true if loaded successfully
     *
     * The file should contain multiple single-cycle waveforms concatenated.
     * The total sample count divided by framesPerCycle determines the number
     * of wavetable frames.
     */
    bool loadWavetable(const std::string& path, uint32_t framesPerCycle = 2048);

    /**
     * @brief Generate wavetable from harmonic amplitudes
     * @param harmonics Vector of harmonic amplitudes (1 = fundamental)
     * @param frameCount Number of wavetable frames to generate
     *
     * Creates an additive synthesis wavetable where each frame has
     * progressively more harmonics.
     */
    void generateFromHarmonics(const std::vector<float>& harmonics, uint32_t frameCount = 8);

    /**
     * @brief Generate wavetable from custom formula
     * @param fn Function taking (phase 0-1, position 0-1) returning sample -1 to 1
     * @param frameCount Number of wavetable frames to generate
     */
    void generateFromFormula(std::function<float(float phase, float position)> fn,
                            uint32_t frameCount = 8);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Play a note at the given frequency
     * @param hz Frequency in Hz
     * @param velocity Velocity 0-1 (default 1.0)
     * @return Number of voices spawned (including unison), or 0 if failed
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
     * @param velocity MIDI velocity 0-127 (default 127)
     * @return Number of voices spawned (including unison), or 0 if failed
     */
    int noteOnMidi(int midiNote, int velocity = 127);

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
     * @brief Get number of wavetable frames
     */
    uint32_t frameCount() const { return m_frameCount; }

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
    std::string name() const override { return "WavetableSynth"; }

    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    // Wavetable storage
    static constexpr uint32_t SAMPLES_PER_FRAME = 2048;
    static constexpr uint32_t MAX_FRAMES = 256;

    std::vector<float> m_wavetable;  // Frames * SAMPLES_PER_FRAME
    uint32_t m_frameCount = 0;

    // Voice state
    struct Voice {
        float frequency = 0.0f;        // Base frequency (before detune)
        float targetFrequency = 0.0f;  // For portamento
        float currentFrequency = 0.0f; // Interpolated frequency
        float phase = 0.0f;
        float subPhase = 0.0f;         // Sub oscillator phase
        EnvelopeStage envStage = EnvelopeStage::Idle;
        float envValue = 0.0f;
        float envProgress = 0.0f;
        float releaseStartValue = 0.0f;
        uint64_t noteId = 0;

        // Unison tracking
        uint64_t unisonGroup = 0;      // Groups voices for same note
        float detuneOffset = 0.0f;     // Cents offset for this voice
        float pan = 0.0f;              // -1 to 1 stereo position

        // Velocity
        float velocity = 1.0f;         // 0-1 velocity captured at noteOn

        // For FM warp mode (self-modulation feedback)
        float lastSample = 0.0f;

        // Filter envelope (separate from amplitude envelope)
        EnvelopeStage filterEnvStage = EnvelopeStage::Idle;
        float filterEnvValue = 0.0f;
        float filterEnvProgress = 0.0f;
        float filterReleaseStartValue = 0.0f;

        // Biquad filter state (2 cascaded stages for 24dB)
        // Each stage: z1, z2 for transposed direct form II
        float filterZ1[2] = {0.0f, 0.0f};
        float filterZ2[2] = {0.0f, 0.0f};

        bool isActive() const { return envStage != EnvelopeStage::Idle; }
        bool isReleasing() const { return envStage == EnvelopeStage::Release; }

        void resetFilter() {
            filterZ1[0] = filterZ1[1] = 0.0f;
            filterZ2[0] = filterZ2[1] = 0.0f;
        }
    };

    std::vector<Voice> m_voices;
    uint64_t m_noteCounter = 0;
    uint64_t m_unisonGroupCounter = 0;  // For grouping unison voices
    float m_lastFrequency = 0.0f;       // For legato portamento
    WarpMode m_warpMode = WarpMode::None;  // Current warp mode
    SynthFilterType m_filterType = SynthFilterType::LP24;  // Default to classic 24dB LP
    uint32_t m_sampleRate = 48000;

    // Voice management
    int findFreeVoice() const;
    int findVoiceToSteal() const;
    int findVoiceByFrequency(float hz) const;
    std::vector<int> findVoicesByBaseFrequency(float hz) const;  // For unison noteOff

    // Sample generation
    float sampleWavetable(float phase, float position) const;
    float linearInterpolate(float a, float b, float t) const;
    float centsToRatio(float cents) const;

    // Phase warping
    float warpPhase(float phase, float amount, float lastSample) const;

    // Amplitude envelope
    void advanceEnvelope(Voice& voice, uint32_t samples);
    float computeEnvelope(Voice& voice) const;

    // Filter envelope
    void advanceFilterEnvelope(Voice& voice, uint32_t samples);
    float computeFilterEnvelope(Voice& voice) const;

    // Per-voice filter
    float applyFilter(Voice& voice, float input, float cutoffHz, float resonance);

    // Wavetable generation helpers
    void generateBasicTable();
    void generateAnalogTable();
    void generateDigitalTable();
    void generateVocalTable();
    void generateTextureTable();
    void generatePWMTable();

    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;
    static constexpr float FREQ_TOLERANCE = 0.5f;
};

} // namespace vivid::audio
