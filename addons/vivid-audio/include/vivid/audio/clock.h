#pragma once

/**
 * @file clock.h
 * @brief BPM-based trigger generator
 *
 * Generates triggers at musical timing intervals.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <functional>

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
 * @brief BPM-based clock/trigger generator
 *
 * Generates triggers at musical time divisions. Use to drive drum machines,
 * sequencers, and synchronized effects. Supports multiple subdivisions and
 * swing timing.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | bpm | float | 20-300 | 120 | Tempo in beats per minute |
 * | swing | float | 0-1 | 0 | Swing amount (delays even beats) |
 *
 * @par Example
 * @code
 * chain.add<Clock>("clock").bpm(120.0f).division(ClockDiv::Sixteenth);
 *
 * void update(Context& ctx) {
 *     if (chain.get<Clock>("clock")->triggered()) {
 *         chain.get<Kick>("kick")->trigger();
 *     }
 * }
 * @endcode
 */
class Clock : public Operator {
public:
    Clock() = default;
    ~Clock() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    Clock& bpm(float tempo) { m_bpm = tempo; return *this; }
    Clock& division(ClockDiv div) { m_division = div; return *this; }
    Clock& swing(float amt) { m_swing = amt; return *this; }

    /// @brief Get current BPM
    float getBpm() const { return m_bpm; }

    /// @brief Get current swing amount
    float getSwing() const { return m_swing; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Trigger State
    /// @{

    /**
     * @brief Check if clock triggered this frame
     * @return True if trigger occurred
     */
    bool triggered() const { return m_triggered; }

    /**
     * @brief Get number of triggers since start
     */
    uint64_t triggerCount() const { return m_triggerCount; }

    /**
     * @brief Get current beat position (0-based)
     */
    uint32_t beat() const { return static_cast<uint32_t>(m_triggerCount) % 4; }

    /**
     * @brief Get current bar (4 beats = 1 bar)
     */
    uint32_t bar() const { return static_cast<uint32_t>(m_triggerCount) / 4; }

    /**
     * @brief Check if this is the downbeat (beat 0)
     */
    bool isDownbeat() const { return triggered() && beat() == 0; }

    /**
     * @brief Reset clock to start
     */
    void reset();

    /**
     * @brief Start the clock
     */
    void start() { m_running = true; }

    /**
     * @brief Stop the clock
     */
    void stop() { m_running = false; }

    /**
     * @brief Check if clock is running
     */
    bool isRunning() const { return m_running; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Callback API
    /// @{

    /**
     * @brief Set callback for triggers
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

    std::vector<ParamDecl> params() override {
        return { m_bpm.decl(), m_swing.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "bpm") { out[0] = m_bpm; return true; }
        if (name == "swing") { out[0] = m_swing; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "bpm") { m_bpm = value[0]; return true; }
        if (name == "swing") { m_swing = value[0]; return true; }
        return false;
    }

    /// @}

private:
    float getDivisionMultiplier() const;

    // Parameters
    Param<float> m_bpm{"bpm", 120.0f, 20.0f, 300.0f};
    Param<float> m_swing{"swing", 0.0f, 0.0f, 1.0f};
    ClockDiv m_division = ClockDiv::Quarter;

    // State
    double m_phase = 0.0;
    uint64_t m_triggerCount = 0;
    bool m_triggered = false;
    bool m_running = true;
    bool m_lastTickOdd = false;

    std::function<void()> m_callback;

    uint32_t m_sampleRate = 48000;
    bool m_initialized = false;
};

} // namespace vivid::audio
