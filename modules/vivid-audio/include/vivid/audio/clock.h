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
 * auto& clock = chain.add<Clock>("clock");
 * clock.bpm = 120.0f;
 * clock.division(ClockDiv::Sixteenth);
 * clock.start();
 *
 * // Audio-thread triggering (recommended)
 * auto& seq = chain.add<Sequencer>("seq");
 * seq.setTriggerSource("clock");  // Sequencer advances on clock
 * auto& kick = chain.add<Kick>("kick");
 * kick.setTriggerSource("seq");   // Drum triggers on sequencer
 *
 * // In update() - just poll for visual feedback:
 * if (seq.triggered()) kickDecay = 1.0f;
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

    /**
     * @brief Enable/disable swing
     * @param enabled True to enable swing, false to disable
     * @note Swing is disabled by default for testing
     */
    void setSwingEnabled(bool enabled) { m_swingEnabled = enabled; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name MIDI Clock Sync
    /// @{

    /**
     * @brief Enable MIDI clock sync mode (sync to external MIDI clock)
     * @param enabled True to sync tempo from incoming MIDI clock
     *
     * When enabled, the clock derives BPM from incoming MIDI clock messages
     * (24 pulses per quarter note). The bpm parameter becomes read-only
     * and reflects the detected tempo.
     *
     * @note Call midiClock() from MidiIn when clock messages are received.
     */
    void setMidiClockSync(bool enabled) { m_midiClockSync = enabled; }

    /**
     * @brief Check if MIDI clock sync is enabled
     */
    bool isMidiClockSync() const { return m_midiClockSync; }

    /**
     * @brief Receive MIDI clock tick (called from MidiIn)
     *
     * When m_midiClockSync is enabled, this updates the internal BPM
     * calculation based on 24 PPQ MIDI clock timing.
     */
    void midiClock();

    /**
     * @brief Receive MIDI Start message
     */
    void midiStart();

    /**
     * @brief Receive MIDI Stop message
     */
    void midiStop();

    /**
     * @brief Receive MIDI Continue message
     */
    void midiContinue();

    /**
     * @brief Set MidiOut operator name for sending MIDI clock
     * @param midiOutName Name of MidiOut operator to send clock to
     *
     * When set, this clock will output MIDI clock (24 PPQ), start, stop,
     * and continue messages to the specified MidiOut operator.
     */
    void setMidiClockOutput(const std::string& midiOutName) { m_midiClockOutput = midiOutName; }

    /**
     * @brief Clear the MIDI clock output
     */
    void clearMidiClockOutput() { m_midiClockOutput.clear(); }

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
     *
     * @note When called directly on a Clock object, this non-const version
     * is preferred and clears the flag. When called through the base class
     * Operator* pointer (e.g., from AudioGraph), the const override below
     * is used which does NOT clear the flag.
     */
    bool triggered() {
        return m_triggeredFlag.exchange(false, std::memory_order_acquire);
    }

    /**
     * @brief Check trigger state without clearing (audio thread polling)
     * @return True if clock triggered in current audio block
     *
     * Override of base Operator::triggered() for audio thread trigger propagation.
     * Does NOT clear the flag - safe for multiple downstream operators to poll.
     * This version is called when accessing through Operator* base pointer.
     */
    bool triggered() const override {
        return m_triggeredFlag.load(std::memory_order_acquire);
    }

    /**
     * @brief Alias for triggered() const (for visualization)
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
    InspectData inspect() const override;
    OutputKind outputKind() const override { return OutputKind::Audio; }

    /// @brief Generate audio block with sample-accurate triggers (audio thread)
    void generateBlock(uint32_t frameCount) override;

    /// @brief Draw beat grid visualization
    bool drawVisualization(VizDrawList* drawList, float minX, float minY,
                           float maxX, float maxY) override;

    /// @}

private:
    float getDivisionMultiplier() const;

    ClockDiv m_division = ClockDiv::Quarter;
    bool m_swingEnabled = false;  // Swing disabled by default for testing

    // State (accessed from audio thread)
    double m_phase = 0.0;
    bool m_lastTickOdd = false;
    float m_capturedSwingDelay = 0.0f;  // Swing delay captured at odd beat time

    // Shared state (atomic for thread safety)
    std::atomic<uint64_t> m_triggerCount{0};
    std::atomic<bool> m_triggeredFlag{false};  // Set by audio thread, cleared by main thread
    std::atomic<bool> m_running{true};

    std::function<void()> m_callback;

    // MIDI clock sync
    bool m_midiClockSync = false;          // True when syncing to external MIDI clock
    uint64_t m_midiClockCount = 0;         // Count of MIDI clock ticks received
    double m_lastMidiClockTime = 0.0;      // Time of last MIDI clock tick (seconds)
    double m_midiClockInterval = 0.0;      // Average interval between ticks
    std::string m_midiClockOutput;         // MidiOut operator name for sending clock
    double m_midiClockPhase = 0.0;         // Phase for sending MIDI clock (24 PPQ)

    static constexpr uint32_t SAMPLE_RATE = 48000;
};

} // namespace vivid::audio
