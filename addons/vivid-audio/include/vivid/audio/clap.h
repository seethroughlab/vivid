#pragma once

/**
 * @file clap.h
 * @brief Hand clap synthesizer
 *
 * Multiple noise bursts with slight timing variations.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid::audio {

/**
 * @brief Hand clap synthesizer
 *
 * Generates clap sounds using multiple short noise bursts with slight
 * timing offsets to simulate multiple hands clapping. Bandpass filtered
 * for characteristic "clap" frequency range.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | decay | float | 0.05-1 | 0.3 | Overall decay time |
 * | tone | float | 0-1 | 0.5 | Brightness |
 * | spread | float | 0-1 | 0.5 | Timing spread between bursts |
 *
 * @par Example
 * @code
 * chain.add<Clap>("clap")
 *     .decay(0.3f)
 *     .tone(0.6f)
 *     .spread(0.5f);
 *
 * chain.get<Clap>("clap")->trigger();
 * @endcode
 */
class Clap : public AudioOperator {
public:
    Clap() = default;
    ~Clap() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    Clap& decay(float s) { m_decay = s; return *this; }
    Clap& tone(float amt) { m_tone = amt; return *this; }
    Clap& spread(float amt) { m_spread = amt; return *this; }
    Clap& volume(float v) { m_volume = v; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    void trigger();
    void reset();
    bool isActive() const { return m_env > 0.0001f; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Clap"; }

    std::vector<ParamDecl> params() override {
        return { m_decay.decl(), m_tone.decl(), m_spread.decl(), m_volume.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}

private:
    float generateNoise();
    float bandpass(float in, int ch);

    // Parameters
    Param<float> m_decay{"decay", 0.3f, 0.05f, 1.0f};
    Param<float> m_tone{"tone", 0.5f, 0.0f, 1.0f};
    Param<float> m_spread{"spread", 0.5f, 0.0f, 1.0f};
    Param<float> m_volume{"volume", 0.8f, 0.0f, 1.0f};

    // State
    float m_env = 0.0f;
    uint32_t m_samplesSinceTrigger = 0;
    uint32_t m_seed = 11111;

    // Burst timing (4 quick bursts)
    static constexpr int NUM_BURSTS = 4;
    float m_burstEnv[NUM_BURSTS] = {0, 0, 0, 0};
    uint32_t m_burstDelay[NUM_BURSTS] = {0, 0, 0, 0};

    // Bandpass filter state
    float m_bpState1[2] = {0, 0};
    float m_bpState2[2] = {0, 0};

    uint32_t m_sampleRate = 48000;
    bool m_initialized = false;
};

} // namespace vivid::audio
