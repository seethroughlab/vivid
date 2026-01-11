#pragma once

/**
 * @file reverse.h
 * @brief Beat-synced audio reversal effect
 *
 * Captures audio and plays it backwards at musical intervals.
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
 * @brief Beat-synced reverse playback effect
 *
 * Randomly reverses audio slices at musical intervals, creating
 * classic tape-reverse effects synced to tempo.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | bpm | float | 20-300 | 120 | Tempo for sync |
 * | chance | float | 0-1 | 0.5 | Probability of triggering |
 *
 * @par Example
 * @code
 * auto& rev = chain.add<Reverse>("reverse");
 * rev.input("synth");
 * rev.bpm = 128.0f;
 * rev.triggerDiv(ClockDiv::Half);
 * rev.reverseDiv(ClockDiv::Quarter);
 * rev.chance = 0.3f;
 * @endcode
 *
 * @see modules/vivid-audio/examples/glitch-effects for complete example
 */
class Reverse : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};
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
     * @brief Set reverse length (how much audio to reverse)
     */
    void reverseDiv(ClockDiv div) { m_reverseDiv = div; }

    /// @}
    // -------------------------------------------------------------------------

    Reverse() : m_buffer(24.0f), m_rng(std::random_device{}()) {
        registerParam(bpm);
        registerParam(chance);
        registerParam(this->mix);
    }

    std::string name() const override { return "Reverse"; }

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;

private:
    enum class State {
        Passthrough,  // Normal audio pass-through
        Reversing     // Playing back in reverse
    };

    CircularAudioBuffer m_buffer;
    State m_state = State::Passthrough;

    // Trigger clock
    ClockDiv m_triggerDiv = ClockDiv::Half;
    double m_triggerPhase = 0.0;

    // Reverse parameters
    ClockDiv m_reverseDiv = ClockDiv::Quarter;
    size_t m_reverseStart = 0;     // Buffer position where reverse starts
    uint32_t m_reverseLength = 0;  // Reverse length in samples
    double m_reversePhase = 0.0;   // Playback phase (counts up, reads backwards)

    // Crossfade for smooth transitions
    static constexpr uint32_t CROSSFADE_SAMPLES = 128;
    uint32_t m_crossfadePos = 0;
    bool m_crossfadingIn = false;
    bool m_crossfadingOut = false;

    // Random number generator
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};

    float random() { return m_dist(m_rng); }
};

} // namespace vivid::audio
