#pragma once

/**
 * @file beat_repeat.h
 * @brief Beat-synced audio slice repeater
 *
 * Captures audio and loops slices rhythmically, inspired by Ableton's
 * Beat Repeat and Ned Rush's Lucky 16.
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
 * @brief Beat-synced slice repeater
 *
 * Randomly captures and loops audio slices at musical intervals.
 * Creates classic stutter/glitch effects.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | bpm | float | 20-300 | 120 | Tempo for sync |
 * | chance | float | 0-1 | 0.5 | Probability of triggering |
 * | repeatCount | int | 1-16 | 4 | Number of slice repeats |
 * | decay | float | 0-1 | 0.0 | Volume decay per repeat |
 *
 * @par Example
 * @code
 * auto& repeat = chain.add<BeatRepeat>("repeat");
 * repeat.input("synth");
 * repeat.bpm = 128.0f;
 * repeat.triggerDiv = ClockDiv::Eighth;
 * repeat.sliceDiv = ClockDiv::Sixteenth;
 * repeat.chance = 0.4f;
 * repeat.repeatCount = 4;
 * @endcode
 *
 * @see modules/vivid-audio/examples/glitch-effects for complete example
 */
class BeatRepeat : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};
    Param<int> repeatCount{"repeatCount", 4, 1, 16};
    Param<float> decay{"decay", 0.0f, 0.0f, 1.0f};
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
     * @brief Set slice size (length of captured audio)
     */
    void sliceDiv(ClockDiv div) { m_sliceDiv = div; }

    /// @}
    // -------------------------------------------------------------------------

    BeatRepeat() : m_buffer(24.0f), m_rng(std::random_device{}()) {
        registerParam(bpm);
        registerParam(chance);
        registerParam(repeatCount);
        registerParam(decay);
        registerParam(this->mix);  // Note: shadows base class m_mix
    }

    std::string name() const override { return "BeatRepeat"; }

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;

private:
    enum class State {
        Passthrough,  // Normal audio pass-through, recording to buffer
        Repeating     // Playing back captured slice
    };

    CircularAudioBuffer m_buffer;
    State m_state = State::Passthrough;

    // Trigger clock
    ClockDiv m_triggerDiv = ClockDiv::Eighth;
    double m_triggerPhase = 0.0;

    // Slice parameters
    ClockDiv m_sliceDiv = ClockDiv::Sixteenth;
    size_t m_sliceStart = 0;      // Buffer position where slice starts
    uint32_t m_sliceLength = 0;   // Slice length in samples
    double m_slicePhase = 0.0;    // Playback phase within slice (0 to sliceLength)
    int m_currentRepeat = 0;      // Current repeat number
    float m_currentGain = 1.0f;   // Current repeat gain (for decay)

    // Random number generator
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};

    float random() { return m_dist(m_rng); }
};

} // namespace vivid::audio
