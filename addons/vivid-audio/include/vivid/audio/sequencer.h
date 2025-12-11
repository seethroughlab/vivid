#pragma once

/**
 * @file sequencer.h
 * @brief Step sequencer for pattern-based triggering
 *
 * 16-step sequencer with per-step values.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
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
 * chain.add<Clock>("clock").bpm(120.0f).division(ClockDiv::Sixteenth);
 * chain.add<Sequencer>("seq").steps(16);
 *
 * // Set pattern: kick on 1, 5, 9, 13
 * auto* seq = chain.get<Sequencer>("seq");
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
class Sequencer : public Operator {
public:
    static constexpr int MAX_STEPS = 16;

    Sequencer() = default;
    ~Sequencer() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    Sequencer& steps(int n) { m_steps = n; return *this; }

    /// @}
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
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Sequencer"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    std::vector<ParamDecl> params() override {
        return { m_steps.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "steps") { out[0] = m_steps; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "steps") { m_steps = static_cast<int>(value[0]); return true; }
        return false;
    }

    /// @}

private:
    // Parameters
    Param<int> m_steps{"steps", 16, 1, 16};

    // Pattern data
    std::array<bool, MAX_STEPS> m_pattern = {};
    std::array<float, MAX_STEPS> m_velocities = {};

    // State
    int m_currentStep = 0;
    bool m_triggered = false;
    float m_currentVelocity = 0.0f;

    bool m_initialized = false;
};

} // namespace vivid::audio
