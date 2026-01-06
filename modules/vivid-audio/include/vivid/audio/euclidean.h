#pragma once

/**
 * @file euclidean.h
 * @brief Euclidean rhythm generator
 *
 * Generates rhythms using the Euclidean algorithm.
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
class Euclidean : public Operator, public ParamRegistry {
public:
    static constexpr int MAX_STEPS = 16;

    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> steps{"steps", 16, 2, 16};          ///< Total number of steps
    Param<int> hits{"hits", 4, 1, 16};             ///< Number of active steps
    Param<int> rotation{"rotation", 0, 0, 15};     ///< Pattern rotation offset

    /// @}
    // -------------------------------------------------------------------------

    Euclidean() {
        registerParam(steps);
        registerParam(hits);
        registerParam(rotation);
    }
    ~Euclidean() override = default;

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
    /// @name Callbacks
    /// @{

    /**
     * @brief Set callback for trigger events
     * @param callback Function called on each hit
     *
     * Example:
     * @code
     * eucl.onTrigger([&]() {
     *     hihat.trigger();
     *     flash.trigger(0.5f);
     * });
     * @endcode
     */
    void onTrigger(std::function<void()> callback) {
        m_onTrigger = std::move(callback);
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Euclidean"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    /// @}

private:
    void regenerate();  // Regenerate pattern from parameters

    // Cached values for detecting changes
    int m_cachedSteps = 16;
    int m_cachedHits = 4;

    // Generated pattern
    std::array<bool, MAX_STEPS> m_pattern = {};

    // State
    int m_currentStep = 0;
    bool m_triggered = false;

    // Callback
    std::function<void()> m_onTrigger;
};

} // namespace vivid::audio
