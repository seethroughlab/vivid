#pragma once

/**
 * @file scratch.h
 * @brief DJ-style scratch effect with varispeed playback
 *
 * Simulates vinyl scratching with variable speed and direction.
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
 * @brief Scratch motion types
 */
enum class ScratchMotion {
    Forward,    ///< Play forward only at variable speed
    Backward,   ///< Play backward only
    BackForth,  ///< Oscillate back and forth (classic scratch)
    Random      ///< Random direction changes
};

/**
 * @brief DJ-style scratch effect
 *
 * Captures audio and plays it back with variable speed and direction,
 * simulating vinyl scratching. Great for breakdowns and transitions.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | bpm | float | 20-300 | 120 | Tempo for sync |
 * | chance | float | 0-1 | 0.5 | Probability of triggering |
 * | speed | float | 0.125-4 | 1.0 | Base playback speed |
 * | speedRandom | float | 0-1 | 0.3 | Speed randomization amount |
 * | scratchBeats | float | 0.125-2 | 0.5 | Scratch duration in beats |
 *
 * @par Example
 * @code
 * auto& scratch = chain.add<Scratch>("scratch");
 * scratch.input("drums");
 * scratch.bpm = 128.0f;
 * scratch.triggerDiv(ClockDiv::Half);
 * scratch.motion(ScratchMotion::BackForth);
 * scratch.speed = 1.5f;
 * scratch.chance = 0.2f;
 * @endcode
 *
 * @see modules/vivid-audio/examples/glitch-effects for complete example
 */
class Scratch : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};
    Param<float> speed{"speed", 1.0f, 0.125f, 4.0f};
    Param<float> speedRandom{"speedRandom", 0.3f, 0.0f, 1.0f};
    Param<float> scratchBeats{"scratchBeats", 0.5f, 0.125f, 2.0f};
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set trigger rate
     */
    void triggerDiv(ClockDiv div) { m_triggerDiv = div; }

    /**
     * @brief Set scratch motion type
     */
    void motion(ScratchMotion m) { m_motion = m; }

    /// @}
    // -------------------------------------------------------------------------

    Scratch() : m_buffer(24.0f), m_rng(std::random_device{}()) {
        registerParam(bpm);
        registerParam(chance);
        registerParam(speed);
        registerParam(speedRandom);
        registerParam(scratchBeats);
        registerParam(this->mix);
    }

    std::string name() const override { return "Scratch"; }

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;

private:
    enum class State {
        Passthrough,
        Scratching
    };

    CircularAudioBuffer m_buffer;
    State m_state = State::Passthrough;

    // Trigger clock
    ClockDiv m_triggerDiv = ClockDiv::Half;
    double m_triggerPhase = 0.0;

    // Scratch parameters
    ScratchMotion m_motion = ScratchMotion::BackForth;
    size_t m_scratchStart = 0;
    uint32_t m_scratchLength = 0;    // Total scratch duration in samples
    double m_scratchPhase = 0.0;     // Current position in scratch (0 to scratchLength)
    double m_readPos = 0.0;          // Current read position in buffer
    float m_currentSpeed = 1.0f;     // Current playback speed (can be negative)
    int m_direction = 1;             // 1 = forward, -1 = backward

    // Crossfade for direction changes
    static constexpr uint32_t CROSSFADE_SAMPLES = 64;

    // Random number generator
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};

    float random() { return m_dist(m_rng); }
    float randomSpeed();
};

} // namespace vivid::audio
