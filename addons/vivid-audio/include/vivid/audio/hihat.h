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
    HiHat() = default;
    ~HiHat() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    HiHat& decay(float s) { m_decay = s; return *this; }
    HiHat& tone(float amt) { m_tone = amt; return *this; }
    HiHat& ring(float amt) { m_ring = amt; return *this; }
    HiHat& volume(float v) { m_volume = v; return *this; }

    /// @}
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

    std::vector<ParamDecl> params() override {
        return { m_decay.decl(), m_tone.decl(), m_ring.decl(), m_volume.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    float generateNoise();
    float bandpass(float in, int ch);
    float highpass(float in, int ch);

    // Parameters
    Param<float> m_decay{"decay", 0.1f, 0.01f, 2.0f};
    Param<float> m_tone{"tone", 0.5f, 0.0f, 1.0f};
    Param<float> m_ring{"ring", 0.3f, 0.0f, 1.0f};
    Param<float> m_volume{"volume", 0.7f, 0.0f, 1.0f};

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
    bool m_initialized = false;

    static constexpr float TWO_PI = 6.28318530718f;
};

} // namespace vivid::audio
