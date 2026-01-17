#pragma once

/**
 * @file hihat.h
 * @brief Hi-hat cymbal synthesizer
 *
 * Metallic hi-hat with open/closed modes.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/dsp/filters.h>
#include <vivid/audio/midi_receiver.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Noise color for hi-hat
 */
enum class NoiseType {
    White,  ///< Full spectrum (bright)
    Pink    ///< -3dB/octave (warmer)
};

/**
 * @brief Filter slope for hi-hat
 */
enum class FilterSlope {
    Slope12dB,  ///< Standard 12dB/oct
    Slope24dB   ///< Steeper 24dB/oct
};

/**
 * @brief Hi-hat cymbal synthesizer
 *
 * Generates hi-hat sounds using filtered noise with metallic character.
 * Supports both closed (short) and open (long decay) hi-hat sounds.
 * Uses highpass filtering and resonance for metallic shimmer.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | decay | float | 0.01-2 | 0.1 | Decay time (short = closed, long = open) |
 * | tone | float | 0-1 | 0.5 | Brightness/high frequency emphasis |
 * | ring | float | 0-1 | 0.3 | Metallic ring amount |
 * | pitch | float | 0.5-2 | 1.0 | Ring oscillator pitch multiplier |
 * | attack | float | 0-0.05 | 0 | Filter envelope attack time |
 * | noiseType | enum | White/Pink | White | Noise color |
 * | filterSlope | enum | 12dB/24dB | 12dB | Filter steepness |
 *
 * @par Example
 * @code
 * // Closed hi-hat
 * chain.add<HiHat>("hihatC").decay(0.05f).tone(0.7f);
 *
 * // Open hi-hat
 * chain.add<HiHat>("hihatO").decay(0.5f).tone(0.6f).ring(0.4f);
 *
 * chain.get<HiHat>("hihatC")->trigger();
 * @endcode
 
 * @see Kick, Snare, Clap, Sequencer, Clock, NoiseGen
 */
class HiHat : public AudioOperator, public MidiReceiver {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> decay{"decay", 0.1f, 0.01f, 2.0f};    ///< Decay time (short=closed, long=open)
    Param<float> tone{"tone", 0.5f, 0.0f, 1.0f};       ///< Brightness
    Param<float> ring{"ring", 0.3f, 0.0f, 1.0f};       ///< Metallic ring amount
    Param<float> pitch{"pitch", 1.0f, 0.5f, 2.0f};     ///< Ring oscillator pitch
    Param<float> attack{"attack", 0.0f, 0.0f, 0.05f};  ///< Filter attack time
    Param<float> volume{"volume", 0.7f, 0.0f, 1.0f};   ///< Output volume

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set noise type
     */
    void noiseType(NoiseType type) { m_noiseType = type; }
    NoiseType noiseType() const { return m_noiseType; }

    /**
     * @brief Set filter slope
     */
    void filterSlope(FilterSlope slope) { m_filterSlope = slope; }
    FilterSlope filterSlope() const { return m_filterSlope; }

    /// @}
    // -------------------------------------------------------------------------

    HiHat() {
        registerParam(decay);
        registerParam(tone);
        registerParam(ring);
        registerParam(pitch);
        registerParam(attack);
        registerParam(volume);
    }
    ~HiHat() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    // trigger() inherited from AudioOperator - queues to audio thread
    void choke();  // Instantly stop (for closed hi-hat interrupting open)
    void reset();
    bool isActive() const { return m_env > 0.0001f; }

    // Envelope access for visualization
    float envelope() const { return m_env; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name MidiReceiver Interface
    /// @{

    void midiNoteOn(uint8_t note, float velocity, uint8_t channel = 0) override;
    void midiNoteOff(uint8_t note, float velocity = 0.0f, uint8_t channel = 0) override;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "HiHat"; }

    // Pull-based audio generation (called from audio thread)
    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    // Custom visualization
    bool drawVisualization(VizDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

private:
    float generateNoise();
    float generatePinkNoise();

    // State
    float m_velocity = 1.0f;
    float m_env = 0.0f;
    float m_filterEnv = 0.0f;  // For attack
    uint32_t m_seed = 98765;
    uint32_t m_attackSample = 0;

    // Pink noise state (Voss-McCartney algorithm)
    float m_pinkRows[16] = {0};
    int m_pinkIndex = 0;
    float m_pinkRunningSum = 0.0f;

    // Configuration
    NoiseType m_noiseType = NoiseType::White;
    FilterSlope m_filterSlope = FilterSlope::Slope12dB;

    // SVF filters for tone shaping
    dsp::SVFFilter m_filter1;
    dsp::SVFFilter m_filter2;  // For 24dB mode

    // Ring oscillator phases (for metallic character)
    float m_ringPhase[6] = {0, 0, 0, 0, 0, 0};

    uint32_t m_sampleRate = 48000;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
