#pragma once

/**
 * @file hihat.h
 * @brief Hi-hat cymbal synthesizer
 *
 * Metallic hi-hat with open/closed modes.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

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
 */
class HiHat : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> decay{"decay", 0.1f, 0.01f, 2.0f};    ///< Decay time (short=closed, long=open)
    Param<float> tone{"tone", 0.5f, 0.0f, 1.0f};       ///< Brightness
    Param<float> ring{"ring", 0.3f, 0.0f, 1.0f};       ///< Metallic ring amount
    Param<float> volume{"volume", 0.7f, 0.0f, 1.0f};   ///< Output volume

    /// @}
    // -------------------------------------------------------------------------

    HiHat() {
        registerParam(decay);
        registerParam(tone);
        registerParam(ring);
        registerParam(volume);
    }
    ~HiHat() override = default;

    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void trigger();
    void choke();  // Instantly stop (for closed hi-hat interrupting open)
    void reset();
    bool isActive() const { return m_env > 0.0001f; }

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

    /// @}

private:
    void triggerInternal();  // Called from audio thread
    float generateNoise();
    float bandpass(float in, int ch);
    float highpass(float in, int ch);

    // State
    float m_env = 0.0f;
    uint32_t m_seed = 98765;

    // Filter states
    float m_bpState1[2] = {0, 0};
    float m_bpState2[2] = {0, 0};
    float m_hpState[2] = {0, 0};

    // Ring oscillator phases (for metallic character)
    float m_ringPhase[6] = {0, 0, 0, 0, 0, 0};

    uint32_t m_sampleRate = 48000;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
