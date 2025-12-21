#pragma once

/**
 * @file sequencer.h
 * @brief Step sequencer for pattern-based triggering
 *
 * 16-step sequencer with per-step values.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/param_registry.h>
#include <string>
#include <vector>
#include <array>
#include <functional>

namespace vivid::audio {

/**
 * @brief Step sequencer for patterns
 *
 * 16-step sequencer that outputs triggers and values based on a pattern.
 * Each step can be on/off and have a velocity value. Advances on external
 * trigger (typically from Clock).
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | steps | int | 1-16 | 16 | Number of active steps |
 *
 * @par Example
 * @code
 * chain.add<Clock>("clock");
 * chain.get<Clock>("clock")->bpm = 120.0f;
 * chain.get<Clock>("clock")->division(ClockDiv::Sixteenth);
 * chain.add<Sequencer>("seq");
 *
 * // Set pattern: kick on 1, 5, 9, 13
 * auto* seq = chain.get<Sequencer>("seq");
 * seq->steps = 16;
 * seq->setStep(0, true);
 * seq->setStep(4, true);
 * seq->setStep(8, true);
 * seq->setStep(12, true);
 *
 * void update(Context& ctx) {
 *     if (chain.get<Clock>("clock")->triggered()) {
 *         chain.get<Sequencer>("seq")->advance();
 *         if (chain.get<Sequencer>("seq")->triggered()) {
 *             chain.get<Kick>("kick")->trigger();
 *         }
 *     }
 * }
 * @endcode
 */
class Sequencer : public Operator, public ParamRegistry {
public:
    static constexpr int MAX_STEPS = 16;

    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> steps{"steps", 16, 1, 16};   ///< Number of active steps

    /// @}
    // -------------------------------------------------------------------------

    Sequencer() {
        registerParam(steps);
    }
    ~Sequencer() override = default;
    // -------------------------------------------------------------------------
    /// @name Pattern Editing
    /// @{

    /**
     * @brief Set step on/off state
     * @param step Step index (0-15)
     * @param on Whether step is active
     * @param velocity Optional velocity (0-1, default 1)
     */
    void setStep(int step, bool on, float velocity = 1.0f);

    /**
     * @brief Get step state
     * @param step Step index (0-15)
     * @return True if step is active
     */
    bool getStep(int step) const;

    /**
     * @brief Get step velocity
     * @param step Step index (0-15)
     * @return Velocity value (0-1)
     */
    float getVelocity(int step) const;

    /**
     * @brief Clear all steps
     */
    void clearPattern();

    /**
     * @brief Set pattern from bitmask (for quick patterns)
     * @param pattern 16-bit pattern (bit 0 = step 0)
     */
    void setPattern(uint16_t pattern);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback
    /// @{

    /**
     * @brief Advance to next step
     */
    void advance();

    /**
     * @brief Check if current step triggered
     */
    bool triggered() const { return m_triggered; }

    /**
     * @brief Get current step velocity (if triggered)
     */
    float currentVelocity() const { return m_currentVelocity; }

    /**
     * @brief Get current step index
     */
    int currentStep() const { return m_currentStep; }

    /**
     * @brief Reset to step 0
     */
    void reset();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Callbacks
    /// @{

    /**
     * @brief Set callback for trigger events
     * @param callback Function called when step triggers, receives velocity
     *
     * Example:
     * @code
     * seq.onTrigger([&](float velocity) {
     *     kick.trigger(velocity);       // Audio
     *     flash.trigger(velocity);      // Visual
     *     particles.burst(50 * velocity);
     * });
     * @endcode
     */
    void onTrigger(std::function<void(float velocity)> callback) {
        m_onTrigger = std::move(callback);
    }

    /**
     * @brief Set simple callback (no velocity)
     * @param callback Function called when step triggers
     */
    void onTrigger(std::function<void()> callback) {
        m_onTrigger = [cb = std::move(callback)](float) { cb(); };
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Sequencer"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    std::vector<ParamDecl> params() override { return registeredParams(); }
    bool getParam(const std::string& name, float out[4]) override {
        return getRegisteredParam(name, out);
    }
    bool setParam(const std::string& name, const float value[4]) override {
        return setRegisteredParam(name, value);
    }

    /// @}

private:

    // Pattern data
    std::array<bool, MAX_STEPS> m_pattern = {};
    std::array<float, MAX_STEPS> m_velocities = {};

    // State
    int m_currentStep = 0;
    bool m_triggered = false;
    float m_currentVelocity = 0.0f;

    // Callback
    std::function<void(float velocity)> m_onTrigger;
};

} // namespace vivid::audio
