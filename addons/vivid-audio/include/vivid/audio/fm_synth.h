#pragma once

/**
 * @file fm_synth.h
 * @brief FM synthesizer inspired by the Yamaha DX7
 *
 * Classic frequency modulation synthesis with 4 operators,
 * multiple algorithms, and per-operator envelopes.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/envelope.h>
#include <vivid/param.h>
#include <string>
#include <array>
#include <cmath>

namespace vivid::audio {

/**
 * @brief FM synthesis algorithms (operator routing)
 *
 * Numbers indicate operators 1-4. Arrows show modulation path.
 * Operators without arrows are carriers (audible output).
 */
enum class FMAlgorithm {
    // Serial algorithms
    Stack4,     ///< 1→2→3→4 (all modulate, 4 is carrier) - Classic FM bass
    Stack3_1,   ///< 1→2→3, 4 (3 and 4 are carriers) - Fat bass

    // Parallel algorithms
    Parallel,   ///< 1,2,3,4 all carriers (additive synthesis)
    Pairs,      ///< 1→2, 3→4 (two independent FM pairs)

    // Branching algorithms
    Branch2,    ///< 1→2,3 (1 modulates both 2 and 3) + 4
    Branch3,    ///< 1→2,3,4 (1 modulates all others)

    // Complex algorithms
    Y,          ///< 1→2, 1→3, 2+3→4 (Y-shaped)
    Diamond     ///< 1→2, 1→3, 2→4, 3→4 (diamond shape)
};

/**
 * @brief FM synthesis presets
 */
enum class FMPreset {
    EPiano,     ///< Classic DX7 electric piano
    Bass,       ///< Punchy FM bass
    Bell,       ///< Tubular bell
    Brass,      ///< Bright brass stab
    Organ,      ///< Percussive organ
    Pad,        ///< Soft evolving pad
    Pluck,      ///< Short plucked sound
    Lead        ///< Bright lead synth
};

/**
 * @brief 4-operator FM synthesizer
 *
 * A polyphonic FM synthesizer with 4 sine-wave operators, 8 algorithms,
 * and per-operator envelopes. Based on the classic Yamaha DX7 architecture
 * but simplified for ease of use.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | ratio1-4 | float | 0.5-16 | 1 | Frequency ratio for each operator |
 * | level1-4 | float | 0-1 | 1 | Output level for each operator |
 * | feedback | float | 0-1 | 0 | Operator 4 self-modulation |
 * | volume | float | 0-1 | 0.5 | Master output volume |
 *
 * @par Example
 * @code
 * auto& fm = chain.add<FMSynth>("fm");
 * fm.loadPreset(FMPreset::EPiano);
 *
 * // Or manual configuration:
 * fm.setAlgorithm(FMAlgorithm::Stack4);
 * fm.ratio1 = 1.0f;   // Fundamental
 * fm.ratio2 = 2.0f;   // 2nd harmonic modulator
 * fm.level2 = 0.8f;   // Modulation depth
 * fm.feedback = 0.3f;
 *
 * fm.noteOn(440.0f);  // Play A4
 * @endcode
 */
class FMSynth : public AudioOperator {
public:
    static constexpr int MAX_VOICES = 8;
    static constexpr int NUM_OPS = 4;

    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    // Operator frequency ratios (relative to note frequency)
    Param<float> ratio1{"ratio1", 1.0f, 0.5f, 16.0f};
    Param<float> ratio2{"ratio2", 1.0f, 0.5f, 16.0f};
    Param<float> ratio3{"ratio3", 1.0f, 0.5f, 16.0f};
    Param<float> ratio4{"ratio4", 1.0f, 0.5f, 16.0f};

    // Operator output levels (affects modulation depth or output volume)
    Param<float> level1{"level1", 1.0f, 0.0f, 1.0f};
    Param<float> level2{"level2", 1.0f, 0.0f, 1.0f};
    Param<float> level3{"level3", 1.0f, 0.0f, 1.0f};
    Param<float> level4{"level4", 1.0f, 0.0f, 1.0f};

    // Operator 4 feedback (self-modulation for richer harmonics)
    Param<float> feedback{"feedback", 0.0f, 0.0f, 1.0f};

    // Master volume
    Param<float> volume{"volume", 0.5f, 0.0f, 1.0f};

    /// @}
    // -------------------------------------------------------------------------

    FMSynth();
    ~FMSynth() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set FM algorithm
     */
    void setAlgorithm(FMAlgorithm algo) { m_algorithm = algo; }

    /**
     * @brief Get current algorithm
     */
    FMAlgorithm algorithm() const { return m_algorithm; }

    /**
     * @brief Load a preset configuration
     */
    void loadPreset(FMPreset preset);

    /**
     * @brief Set envelope for an operator
     * @param op Operator index (0-3)
     * @param a Attack time in seconds
     * @param d Decay time in seconds
     * @param s Sustain level (0-1)
     * @param r Release time in seconds
     */
    void setEnvelope(int op, float a, float d, float s, float r);

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
     * @param hz Frequency in Hz
     */
    void noteOff(float hz);

    /**
     * @brief Play a MIDI note
     */
    int noteOnMidi(int midiNote);

    /**
     * @brief Release a MIDI note
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
     * @brief Get number of active voices
     */
    int activeVoiceCount() const;

    // Visualization access
    /**
     * @brief Get maximum envelope value across all voices for an operator
     */
    float operatorEnvelope(int op) const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "FMSynth"; }

    void generateBlock(uint32_t frameCount) override;

    // Custom visualization
    bool drawVisualization(ImDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

private:
    // Operator state
    struct Operator {
        float phase = 0.0f;
        float output = 0.0f;      // Last output (for feedback)
        float prevOutput = 0.0f;  // Previous output (for feedback averaging)

        // ADSR envelope
        float attack = 0.01f;
        float decay = 0.1f;
        float sustain = 0.7f;
        float release = 0.3f;
    };

    // Voice state
    struct Voice {
        float frequency = 0.0f;
        std::array<Operator, NUM_OPS> ops;
        std::array<EnvelopeStage, NUM_OPS> envStage;
        std::array<float, NUM_OPS> envValue;
        std::array<float, NUM_OPS> envProgress;
        std::array<float, NUM_OPS> releaseStartValue;
        bool active = false;
        uint64_t noteId = 0;

        bool isActive() const { return active; }
    };

    // Voices
    std::array<Voice, MAX_VOICES> m_voices;
    uint64_t m_noteCounter = 0;

    // Global settings
    FMAlgorithm m_algorithm = FMAlgorithm::Stack4;
    std::array<Operator, NUM_OPS> m_opSettings;  // Template for new voices

    uint32_t m_sampleRate = 48000;

    // Helpers
    int findFreeVoice() const;
    int findVoiceToSteal() const;
    int findVoiceByFrequency(float hz) const;
    float processVoice(Voice& voice, float ratios[4], float levels[4], float fb);
    void advanceEnvelope(Voice& voice, int op, uint32_t samples);

    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float TWO_PI = 2.0f * PI;
    static constexpr float FREQ_TOLERANCE = 0.5f;
};

} // namespace vivid::audio
