#pragma once

/**
 * @file sequencer.h
 * @brief Step sequencer for pattern-based triggering
 *
 * 16-step sequencer with per-step values. Runs on the audio thread
 * for sample-accurate timing.
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <atomic>

namespace vivid::audio {

/**
 * @brief Step sequencer for patterns (audio-thread based)
 *
 * 16-step sequencer that outputs triggers and values based on a pattern.
 * Each step can be on/off and have a velocity value. Advances automatically
 * when triggered by its trigger source (typically a Clock).
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | steps | int | 1-16 | 16 | Number of active steps |
 *
 * @par Example
 * @code
 * auto& clock = chain.add<Clock>("clock");
 * clock.bpm = 120.0f;
 * clock.division(ClockDiv::Sixteenth);
 *
 * auto& seq = chain.add<Sequencer>("seq");
 * seq.setTriggerSource("clock");  // Advance on clock trigger
 * seq.setPattern(0x1111);  // Kick on 1, 5, 9, 13
 *
 * auto& kick = chain.add<Kick>("kick");
 * kick.setTriggerSource("seq");  // Trigger on sequencer output
 * @endcode
 *
 * @see Clock, Euclidean, Song, Kick, Snare, HiHat, Synth
 */
class Sequencer : public AudioOperator {
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
        // Initialize velocities to 1.0
        for (int i = 0; i < MAX_STEPS; ++i) {
            m_velocities[i] = 1.0f;
        }
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
    /// @name Playback State
    /// @{

    /**
     * @brief Check if sequencer triggered for visualization (main thread)
     *
     * This reads a separate visualization flag that accumulates triggers until
     * the main thread reads it. Use this for visual feedback in update().
     */
    bool triggered() {
        return m_visualTriggeredFlag.exchange(false, std::memory_order_acquire);
    }

    /**
     * @brief Check if current step triggered in this audio block (audio thread)
     *
     * This flag is set during generateBlock() and cleared at the start of the
     * next block. Used by downstream audio operators (like drums) to detect triggers.
     */
    bool triggered() const override { return m_triggeredFlag.load(std::memory_order_acquire); }

    /**
     * @brief Get current step velocity (if triggered)
     */
    float currentVelocity() const { return m_currentVelocity.load(std::memory_order_relaxed); }

    /**
     * @brief Get current step index
     */
    int currentStep() const { return m_currentStep.load(std::memory_order_relaxed); }

    /**
     * @brief Reset to before step 0
     */
    void reset();

    /**
     * @brief Advance to next step (backward-compatible API)
     *
     * This method advances the sequencer immediately and sets the triggered
     * flag synchronously, matching the original behavior where advance()
     * was called from the main thread.
     *
     * For new code, prefer using setTriggerSource("clock") which runs
     * on the audio thread with sample-accurate timing.
     */
    void advance();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Sequencer"; }

    // Audio thread interface
    void generateBlock(uint32_t frameCount) override;

    /// @}

protected:
    void onTrigger() override;

private:
    // Pattern data (set from main thread, read from audio thread)
    std::array<bool, MAX_STEPS> m_pattern = {};
    std::array<float, MAX_STEPS> m_velocities = {};

    // State (atomic for cross-thread access)
    std::atomic<int> m_currentStep{-1};
    std::atomic<bool> m_triggeredFlag{false};      // For audio thread (cleared each block)
    std::atomic<bool> m_visualTriggeredFlag{false}; // For main thread (cleared on read)
    std::atomic<float> m_currentVelocity{0.0f};

    // Audio thread internal state
    bool m_pendingTrigger = false;
    uint64_t m_lastTriggerCount = 0;  // For tracking Clock's trigger count

    // Internal advance (called on audio thread)
    void advanceInternalNoFlag();  // Advance without setting triggered flag (for catchup)
    void advanceInternal();        // Advance and set triggered flag
};

} // namespace vivid::audio
