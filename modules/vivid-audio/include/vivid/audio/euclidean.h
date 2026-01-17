#pragma once

/**
 * @file euclidean.h
 * @brief Euclidean rhythm generator
 *
 * Generates rhythms using the Euclidean algorithm.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/midi_receiver.h>
#include <vivid/operator_registry.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <atomic>

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
 * auto& clock = chain.add<Clock>("clock");
 * clock.bpm = 120.0f;
 * clock.division(ClockDiv::Sixteenth);
 *
 * auto& eucl = chain.add<Euclidean>("eucl");
 * eucl.setTriggerSource("clock");  // Advances automatically on audio thread
 * eucl.steps = 16;
 * eucl.hits = 5;
 *
 * auto& kick = chain.add<Kick>("kick");
 * kick.setTriggerSource("eucl");  // Triggers on Euclidean output
 * @endcode
 
 * @see Clock, Sequencer, Kick, Snare, HiHat
 */
class Euclidean : public AudioOperator, public MidiReceiver {
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
     * @brief Check if euclidean triggered for visualization (main thread)
     *
     * This reads a separate visualization flag that accumulates triggers until
     * the main thread reads it. Use this for visual feedback in update().
     */
    bool triggered() {
        return m_visualTriggeredFlag.exchange(false, std::memory_order_acquire);
    }

    /**
     * @brief Check if current step is a hit in this audio block (audio thread)
     *
     * This flag is set during generateBlock() and cleared at the start of the
     * next block. Used by downstream audio operators (like drums) to detect triggers.
     */
    bool triggered() const override { return m_triggeredFlag.load(std::memory_order_acquire); }

    /**
     * @brief Get current step index
     * @note Thread-safe, can be called from main thread for visualization
     */
    int currentStep() const { return m_currentStep.load(std::memory_order_relaxed); }

    /**
     * @brief Reset to step 0
     */
    void reset();

    /**
     * @brief Advance to next step (backward-compatible API)
     *
     * This method advances the euclidean pattern immediately and sets the
     * triggered flag synchronously, matching the original behavior where
     * advance() was called from the main thread.
     *
     * For new code, prefer using setTriggerSource("clock") which runs
     * on the audio thread with sample-accurate timing.
     */
    void advance();

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
    /// @name MidiReceiver Interface
    /// @{

    void midiNoteOn(uint8_t note, float velocity, uint8_t channel = 0) override;
    void midiNoteOff(uint8_t note, float velocity = 0.0f, uint8_t channel = 0) override;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Euclidean"; }

    // Audio thread interface
    void generateBlock(uint32_t frameCount) override;

    /// @brief Draw step pattern visualization
    bool drawVisualization(VizDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

private:
    void regenerate();           // Regenerate pattern from parameters
    void advanceInternalNoFlag();  // Advance without setting flag (for catchup)
    void advanceInternal();      // Internal advance (called on audio thread)

    // Cached values for detecting changes
    int m_cachedSteps = 16;
    int m_cachedHits = 4;

    // Generated pattern (set from main thread, read from audio thread)
    std::array<bool, MAX_STEPS> m_pattern = {};

    // State (atomic for cross-thread access)
    std::atomic<int> m_currentStep{-1};
    std::atomic<bool> m_triggeredFlag{false};      // For audio thread (cleared each block)
    std::atomic<bool> m_visualTriggeredFlag{false}; // For main thread (cleared on read)

    // Audio thread internal state
    bool m_pendingTrigger = false;
    uint64_t m_lastTriggerCount = 0;  // For tracking Clock's trigger count

    // Callback
    std::function<void()> m_onTrigger;
};

} // namespace vivid::audio
