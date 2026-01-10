#pragma once

/**
 * @file clock.h
 * @brief BPM-based trigger generator with sample-accurate timing
 *
 * Generates triggers at musical timing intervals on the audio thread.
 */

#include <vivid/audio_operator.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>

namespace vivid::audio {

/**
 * @brief Clock subdivision types
 */
enum class ClockDiv {
    Whole,          ///< Whole note (1/1)
    Half,           ///< Half note (1/2)
    Quarter,        ///< Quarter note (1/4) - default beat
    Eighth,         ///< Eighth note (1/8)
    Sixteenth,      ///< Sixteenth note (1/16)
    ThirtySecond,   ///< Thirty-second note (1/32)
    DottedQuarter,  ///< Dotted quarter (1/4 + 1/8)
    DottedEighth,   ///< Dotted eighth (1/8 + 1/16)
    TripletQuarter, ///< Quarter triplet
    TripletEighth   ///< Eighth triplet
};

/**
 * @brief BPM-based clock/trigger generator with sample-accurate timing
 *
 * Generates triggers at musical time divisions on the audio thread for
 * precise timing. Use to drive drum machines, sequencers, and synchronized
 * effects.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | bpm | float | 20-300 | 120 | Tempo in beats per minute |
 * | swing | float | 0-1 | 0 | Swing amount (delays even beats) |
 *
 * @par Example
 * @code
 * chain.add<Clock>("clock");
 * chain.get<Clock>("clock")->bpm = 120.0f;
 * chain.get<Clock>("clock")->division(ClockDiv::Sixteenth);
 *
 * void update(Context& ctx) {
 *     if (chain.get<Clock>("clock")->triggered()) {
 *         chain.get<Kick>("kick")->trigger();
 *     }
 * }
 * @endcode
 *
 * @see Sequencer, Euclidean, Song, Kick, Snare, HiHat
 */
class Clock : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> bpm{"bpm", 120.0f, 20.0f, 300.0f};   ///< Tempo in beats per minute
    Param<float> swing{"swing", 0.0f, 0.0f, 1.0f};    ///< Swing amount (delays even beats)

    /// @}
    // -------------------------------------------------------------------------

    Clock() {
        registerParam(bpm);
        registerParam(swing);
    }
    ~Clock() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    void division(ClockDiv div) { m_division = div; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Trigger State
    /// @{

    /**
     * @brief Check if clock triggered since last check (main thread)
     * @return True if trigger occurred
     *
     * This reads the atomic trigger flag set by the audio thread.
     * The flag is cleared after reading to detect the next trigger.
     */
    bool triggered() {
        return m_triggeredFlag.exchange(false, std::memory_order_acquire);
    }

    /**
     * @brief Check trigger state without clearing (for visualization)
     */
    bool triggeredPeek() const {
        return m_triggeredFlag.load(std::memory_order_relaxed);
    }

    /**
     * @brief Get number of triggers since start
     */
    uint64_t triggerCount() const { return m_triggerCount.load(std::memory_order_relaxed); }

    /**
     * @brief Get current beat position (0-based)
     */
    uint32_t beat() const { return static_cast<uint32_t>(triggerCount()) % 4; }

    /**
     * @brief Get current bar (4 beats = 1 bar)
     */
    uint32_t bar() const { return static_cast<uint32_t>(triggerCount()) / 4; }

    /**
     * @brief Check if this is the downbeat (beat 0)
     */
    bool isDownbeat() const { return triggeredPeek() && beat() == 0; }

    /**
     * @brief Reset clock to start
     */
    void reset();

    /**
     * @brief Start the clock
     */
    void start() { m_running.store(true, std::memory_order_release); }

    /**
     * @brief Stop the clock
     */
    void stop() { m_running.store(false, std::memory_order_release); }

    /**
     * @brief Check if clock is running
     */
    bool isRunning() const { return m_running.load(std::memory_order_relaxed); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Callback API
    /// @{

    /**
     * @brief Set callback for triggers (called on audio thread)
     * @param cb Callback function
     */
    void onTrigger(std::function<void()> cb) { m_callback = cb; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Clock"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    /// @brief Generate audio block with sample-accurate triggers (audio thread)
    void generateBlock(uint32_t frameCount) override;

    /// @brief Draw beat grid visualization
    bool drawVisualization(VizDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

private:
    float getDivisionMultiplier() const;

    ClockDiv m_division = ClockDiv::Quarter;

    // State (accessed from audio thread)
    double m_phase = 0.0;
    bool m_lastTickOdd = false;

    // Shared state (atomic for thread safety)
    std::atomic<uint64_t> m_triggerCount{0};
    std::atomic<bool> m_triggeredFlag{false};  // Set by audio thread, cleared by main thread
    std::atomic<bool> m_running{true};

    std::function<void()> m_callback;

    static constexpr uint32_t SAMPLE_RATE = 48000;
};

} // namespace vivid::audio
