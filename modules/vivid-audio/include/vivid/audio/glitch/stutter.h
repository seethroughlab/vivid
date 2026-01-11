#pragma once

/**
 * @file stutter.h
 * @brief Beat-synced stutter/roll effect with envelopes
 *
 * Like BeatRepeat but with configurable volume envelopes for
 * build-ups, decays, and rhythmic effects.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/clock.h>
#include <vivid/audio/glitch/circular_buffer.h>
#include <vivid/audio/glitch/rate_utils.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <random>

namespace vivid::audio {

/**
 * @brief Envelope type for stutter effect
 */
enum class StutterEnvelope {
    Flat,      ///< No volume change
    Decay,     ///< Gets quieter (classic stutter)
    Build,     ///< Gets louder (reverse buildup)
    Triangle   ///< Quiet -> loud -> quiet
};

/**
 * @brief Beat-synced stutter with volume envelopes
 *
 * Creates rhythmic stutters with configurable volume shaping.
 * Great for build-ups, breakdowns, and glitch effects.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | bpm | float | 20-300 | 120 | Tempo for sync |
 * | chance | float | 0-1 | 0.5 | Probability of triggering |
 * | stutterCount | int | 1-32 | 8 | Number of stutters |
 * | envAmount | float | 0-1 | 0.5 | Envelope intensity |
 *
 * @par Example
 * @code
 * auto& stutter = chain.add<Stutter>("stutter");
 * stutter.input("synth");
 * stutter.bpm = 128.0f;
 * stutter.triggerDiv(ClockDiv::Half);
 * stutter.stutterDiv(ClockDiv::ThirtySecond);
 * stutter.stutterCount = 16;
 * stutter.envelope(StutterEnvelope::Build);  // Build-up effect
 * @endcode
 *
 * @see modules/vivid-audio/examples/glitch-effects for complete example
 */
class Stutter : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};
    Param<int> stutterCount{"stutterCount", 8, 1, 32};
    Param<float> envAmount{"envAmount", 0.5f, 0.0f, 1.0f};
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set trigger rate (how often to potentially trigger)
     */
    void triggerDiv(ClockDiv div) { m_triggerDiv = div; }

    /**
     * @brief Set stutter size (length of each stutter)
     */
    void stutterDiv(ClockDiv div) { m_stutterDiv = div; }

    /**
     * @brief Set envelope type
     */
    void envelope(StutterEnvelope env) { m_envelope = env; }

    /// @}
    // -------------------------------------------------------------------------

    Stutter() : m_buffer(24.0f), m_rng(std::random_device{}()) {
        registerParam(bpm);
        registerParam(chance);
        registerParam(stutterCount);
        registerParam(envAmount);
        registerParam(this->mix);
    }

    std::string name() const override { return "Stutter"; }

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;

private:
    enum class State {
        Passthrough,
        Stuttering
    };

    CircularAudioBuffer m_buffer;
    State m_state = State::Passthrough;

    // Trigger clock
    ClockDiv m_triggerDiv = ClockDiv::Half;
    double m_triggerPhase = 0.0;

    // Stutter parameters
    ClockDiv m_stutterDiv = ClockDiv::Sixteenth;
    StutterEnvelope m_envelope = StutterEnvelope::Decay;
    size_t m_stutterStart = 0;
    uint32_t m_stutterLength = 0;
    double m_stutterPhase = 0.0;
    int m_currentStutter = 0;
    int m_totalStutters = 0;

    // Random number generator
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};

    float random() { return m_dist(m_rng); }

    // Calculate envelope value based on current position
    float calculateEnvelope() const;
};

} // namespace vivid::audio
