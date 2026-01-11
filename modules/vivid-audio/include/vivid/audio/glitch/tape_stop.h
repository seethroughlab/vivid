#pragma once

/**
 * @file tape_stop.h
 * @brief Tape deck slowdown/speedup effect
 *
 * Simulates a tape machine stopping or starting, with natural pitch drop/rise.
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
 * @brief Tape stop/start mode
 */
enum class TapeMode {
    Stop,       ///< Slow down to stop only
    Start,      ///< Speed up from stop only
    StopStart   ///< Slow down, then speed back up
};

/**
 * @brief Tape deck slowdown/speedup effect
 *
 * Classic DJ effect that simulates a turntable or tape deck stopping
 * and optionally restarting. The pitch naturally drops as speed decreases.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | bpm | float | 20-300 | 120 | Tempo for sync |
 * | chance | float | 0-1 | 0.5 | Probability of triggering |
 * | stopTime | float | 50-2000 | 500 | Time to stop in ms |
 * | startTime | float | 50-1000 | 200 | Time to restart in ms |
 *
 * @par Example
 * @code
 * auto& tape = chain.add<TapeStop>("tape");
 * tape.input("drums");
 * tape.bpm = 128.0f;
 * tape.triggerDiv(ClockDiv::Whole);
 * tape.mode(TapeMode::StopStart);
 * tape.stopTime = 400.0f;
 * tape.startTime = 150.0f;
 * tape.chance = 0.15f;
 * @endcode
 *
 * @see modules/vivid-audio/examples/glitch-effects for complete example
 */
class TapeStop : public AudioEffect {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters
    /// @{

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};
    Param<float> chance{"chance", 0.5f, 0.0f, 1.0f};
    Param<float> stopTime{"stopTime", 500.0f, 50.0f, 2000.0f};   ///< ms to stop
    Param<float> startTime{"startTime", 200.0f, 50.0f, 1000.0f}; ///< ms to restart
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
     * @brief Set tape mode
     */
    void mode(TapeMode m) { m_mode = m; }

    /// @}
    // -------------------------------------------------------------------------

    TapeStop() : m_buffer(4.0f), m_rng(std::random_device{}()) {
        registerParam(bpm);
        registerParam(chance);
        registerParam(stopTime);
        registerParam(startTime);
        registerParam(this->mix);
    }

    std::string name() const override { return "TapeStop"; }

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;

private:
    enum class State {
        Passthrough,
        Stopping,
        Stopped,
        Starting
    };

    CircularAudioBuffer m_buffer;
    State m_state = State::Passthrough;

    // Trigger clock
    ClockDiv m_triggerDiv = ClockDiv::Whole;
    double m_triggerPhase = 0.0;

    // Tape parameters
    TapeMode m_mode = TapeMode::StopStart;
    double m_playbackRate = 1.0;     // Current playback rate (0 to 1)
    double m_readPos = 0.0;          // Current read position
    uint32_t m_effectPhase = 0;      // Samples into current effect phase
    uint32_t m_stopSamples = 0;      // Duration of stop phase
    uint32_t m_startSamples = 0;     // Duration of start phase
    uint32_t m_stoppedDuration = 0;  // How long to stay stopped

    // Random number generator
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};

    float random() { return m_dist(m_rng); }
};

} // namespace vivid::audio
