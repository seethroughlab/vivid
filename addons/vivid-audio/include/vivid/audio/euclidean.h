#pragma once

/**
 * @file euclidean.h
 * @brief Euclidean rhythm generator
 *
 * Generates rhythms using the Euclidean algorithm.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <array>

namespace vivid::audio {

/**
 * @brief Euclidean rhythm generator
 *
 * Generates rhythms using the Euclidean algorithm, which distributes
 * K hits as evenly as possible across N steps. Many traditional rhythms
 * can be expressed this way (e.g., E(3,8) = tresillo, E(5,8) = cinquillo).
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | steps | int | 2-16 | 16 | Total number of steps |
 * | hits | int | 1-16 | 4 | Number of active steps |
 * | rotation | int | 0-15 | 0 | Pattern rotation offset |
 *
 * @par Common Rhythms
 * - E(3,8) = Tresillo (Cuban rhythm)
 * - E(5,8) = Cinquillo
 * - E(7,16) = Samba
 * - E(5,16) = Bossa nova
 *
 * @par Example
 * @code
 * chain.add<Clock>("clock").bpm(120.0f).division(ClockDiv::Sixteenth);
 * chain.add<Euclidean>("eucl")
 *     .steps(16)
 *     .hits(5)
 *     .rotation(0);
 *
 * void update(Context& ctx) {
 *     if (chain.get<Clock>("clock")->triggered()) {
 *         chain.get<Euclidean>("eucl")->advance();
 *         if (chain.get<Euclidean>("eucl")->triggered()) {
 *             chain.get<Kick>("kick")->trigger();
 *         }
 *     }
 * }
 * @endcode
 */
class Euclidean : public Operator {
public:
    static constexpr int MAX_STEPS = 16;

    Euclidean() = default;
    ~Euclidean() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    Euclidean& steps(int n) { m_steps = n; regenerate(); return *this; }
    Euclidean& hits(int k) { m_hits = k; regenerate(); return *this; }
    Euclidean& rotation(int r) { m_rotation = r; return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback
    /// @{

    /**
     * @brief Advance to next step
     */
    void advance();

    /**
     * @brief Check if current step is a hit
     */
    bool triggered() const { return m_triggered; }

    /**
     * @brief Get current step index
     */
    int currentStep() const { return m_currentStep; }

    /**
     * @brief Reset to step 0
     */
    void reset();

    /**
     * @brief Get generated pattern as bitmask
     */
    uint16_t pattern() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Euclidean"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    std::vector<ParamDecl> params() override {
        return { m_steps.decl(), m_hits.decl(), m_rotation.decl() };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "steps") { out[0] = m_steps; return true; }
        if (name == "hits") { out[0] = m_hits; return true; }
        if (name == "rotation") { out[0] = m_rotation; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "steps") { m_steps = static_cast<int>(value[0]); regenerate(); return true; }
        if (name == "hits") { m_hits = static_cast<int>(value[0]); regenerate(); return true; }
        if (name == "rotation") { m_rotation = static_cast<int>(value[0]); return true; }
        return false;
    }

    /// @}

private:
    void regenerate();  // Regenerate pattern from parameters

    // Parameters
    Param<int> m_steps{"steps", 16, 2, 16};
    Param<int> m_hits{"hits", 4, 1, 16};
    Param<int> m_rotation{"rotation", 0, 0, 15};

    // Generated pattern
    std::array<bool, MAX_STEPS> m_pattern = {};

    // State
    int m_currentStep = 0;
    bool m_triggered = false;

    bool m_initialized = false;
};

} // namespace vivid::audio
