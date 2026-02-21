#pragma once

/**
 * @file sequencer.h
 * @brief Elektron-style step sequencer with per-step parameters
 *
 * 16-step sequencer with rich per-step data (note, velocity, gate,
 * probability, micro-timing, retrigs, conditions, slide, CC).
 * Runs on the audio thread for sample-accurate timing.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/midi_receiver.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <array>
#include <functional>
#include <atomic>
#include <cstdint>
#include <initializer_list>

namespace vivid {
class Chain;  // Forward declaration
}


namespace vivid::audio {

class MidiSender;    // Forward declaration

// -----------------------------------------------------------------------------
/// @name Step Types
/// @{

/**
 * @brief Conditional trigger mode (Elektron-style)
 *
 * Controls when a step fires based on cycle count.
 */
enum class StepCondition : uint8_t {
    Always,       ///< Always fire (default)
    OneInTwo,     ///< Every 2nd cycle
    TwoInThree,  ///< 2 of 3 cycles
    OneInThree,   ///< 1 of 3 cycles
    ThreeInFour,  ///< 3 of 4 cycles
    OneInFour,    ///< 1 of 4 cycles
    OneInFive,
    OneInSix,
    OneInSeven,
    OneInEight,
    FirstOnly,    ///< Only first cycle
    NotFirst,     ///< Every cycle except first
};

/**
 * @brief Per-step CC value
 */
struct StepCC {
    uint8_t cc = 0;
    float value = -1.0f;  ///< -1 = don't send
};

/**
 * @brief Per-step data inspired by Elektron hardware
 *
 * Use C++ designated initializers for a procedural-friendly API:
 * @code
 * seq.setStep(0, {.note = 60, .velocity = 0.9f, .gate = 0.5f});
 * seq.setStep(1, {.note = 63, .velocity = 0.7f, .probability = 0.5f, .slide = true});
 * seq.setStep(2, {.note = 67, .velocity = 0.8f, .retrigCount = 3, .retrigRate = 0.25f});
 * @endcode
 */
struct Step {
    bool active = false;
    uint8_t note = 60;
    float velocity = 1.0f;
    float gate = -1.0f;             ///< -1 = use global gate param
    float probability = 1.0f;       ///< 0-1 chance of firing
    float microTiming = 0.0f;       ///< -0.5 to +0.5 nudge within step
    int retrigCount = 0;            ///< 0-8 retrigs within step
    float retrigRate = 0.5f;        ///< fraction of step for retrig spacing
    StepCondition condition = StepCondition::Always;
    bool slide = false;             ///< legato to next step (suppress note-off)
    std::array<StepCC, 2> cc = {};  ///< per-step CC values
};

/// @}
// -----------------------------------------------------------------------------

/**
 * @brief Elektron-style step sequencer (audio-thread based)
 *
 * 16-step sequencer with rich per-step parameters. Each step carries
 * note, velocity, gate, probability, micro-timing, retrigs, conditional
 * triggers, slide, and per-step CC — inspired by Elektron Digitakt/Digitone.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | steps | int | 1-16 | 16 | Number of active steps |
 * | gate | float | 0.01-1 | 0.5 | Default note length (fraction of step) |
 *
 * @par Example
 * @code
 * auto& clock = chain.add<Clock>("clock");
 * clock.bpm = 120.0f;
 * clock.division(ClockDiv::Sixteenth);
 *
 * auto& seq = chain.add<Sequencer>("seq");
 * seq.setTriggerSource("clock");
 * seq.setSteps({0, 4, 8, 12});  // Kick on 1, 5, 9, 13
 *
 * // Per-step Elektron-style params
 * seq.setStep(0, {.note = 60, .velocity = 0.9f, .gate = 0.5f});
 * seq.setStep(4, {.velocity = 1.0f, .condition = StepCondition::OneInTwo});
 *
 * auto& kick = chain.add<Kick>("kick");
 * kick.setTriggerSource("seq");
 * @endcode
 *
 * @see Clock, Euclidean, Song, Kick, Snare, HiHat, Synth
 */
class Sequencer : public AudioOperator, public MidiReceiver {
public:
    static constexpr int MAX_STEPS = 16;

    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> steps{"steps", 16, 1, 16};   ///< Number of active steps
    Param<int> midiChannel{"midiChannel", 0, 0, 15};  ///< MIDI output channel (0-15)
    Param<float> gate{"gate", 0.5f, 0.01f, 1.0f};  ///< Default note length as fraction of step (0-1)

    /// @}
    // -------------------------------------------------------------------------

    Sequencer() {
        registerParam(steps);
        registerParam(midiChannel);
        registerParam(gate);
    }
    ~Sequencer() override = default;

    // -------------------------------------------------------------------------
    /// @name Pattern Editing
    /// @{

    /**
     * @brief Set step with full Elektron-style parameters
     * @param index Step index (0-15)
     * @param s Step data (use designated initializers)
     *
     * The step is automatically marked active.
     *
     * @code
     * seq.setStep(0, {.note = 60, .velocity = 0.9f, .gate = 0.5f});
     * seq.setStep(1, {.velocity = 0.7f, .probability = 0.5f, .slide = true});
     * @endcode
     */
    void setStep(int index, const Step& s);

    /**
     * @brief Set step on/off state (backward-compatible)
     * @param step Step index (0-15)
     * @param on Whether step is active
     * @param velocity Optional velocity (0-1, default 1)
     */
    void setStep(int step, bool on, float velocity = 1.0f);

    /**
     * @brief Set step with MIDI note and velocity (backward-compatible)
     * @param step Step index (0-15)
     * @param note MIDI note number (0-127, 60 = middle C)
     * @param velocity Note velocity (0-1)
     *
     * This automatically enables the step and sets both note and velocity.
     */
    void setStep(int step, uint8_t note, float velocity);

    /**
     * @brief Activate steps by index (replaces setPattern bitmask)
     * @param activeSteps List of step indices to activate
     *
     * Clears the pattern first, then activates the listed steps with
     * default velocity. Existing per-step data for non-listed steps is reset.
     *
     * @code
     * seq.setSteps({0, 4, 8, 12});  // Four on the floor
     * seq.setSteps({0, 3, 6, 10, 12, 14});  // Breakbeat
     * @endcode
     */
    void setSteps(std::initializer_list<int> activeSteps);

    /**
     * @brief Get full step data
     * @param index Step index (0-15)
     * @return Const reference to Step struct
     */
    const Step& step(int index) const;

    /**
     * @brief Check if step is active
     * @param index Step index (0-15)
     * @return True if step is active
     */
    bool isActive(int index) const;

    /**
     * @brief Get step state (backward-compatible)
     * @param step Step index (0-15)
     * @return True if step is active
     */
    bool getStep(int step) const;

    /**
     * @brief Get step velocity (backward-compatible)
     * @param step Step index (0-15)
     * @return Velocity value (0-1)
     */
    float getVelocity(int step) const;

    /**
     * @brief Get step MIDI note (backward-compatible)
     * @param step Step index (0-15)
     * @return MIDI note number (0-127)
     */
    uint8_t getNote(int step) const;

    /**
     * @brief Clear all steps
     */
    void clearPattern();

    /**
     * @brief Get the current note being played (if triggered)
     * @return MIDI note number of current step
     */
    uint8_t currentNote() const { return m_currentNote.load(std::memory_order_relaxed); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name MIDI Routing
    /// @{

    /**
     * @brief Route MIDI notes to a named MidiReceiver (synth/sampler)
     * @param targetName Name of the target operator (must implement MidiReceiver)
     *
     * When triggered, the sequencer will send MIDI note-on/note-off events
     * to the target synth based on the per-step note and velocity values.
     *
     * @par Example
     * @code
     * auto& seq = chain.add<Sequencer>("seq");
     * auto& synth = chain.add<PolySynth>("synth");
     * seq.setTarget("synth");
     *
     * // Melodic pattern with Elektron features
     * seq.setStep(0, {.note = 60, .velocity = 0.8f});
     * seq.setStep(1, {.note = 63, .velocity = 0.7f, .slide = true});
     * seq.setStep(2, {.note = 67, .velocity = 0.8f});
     * @endcode
     */
    void setTarget(const std::string& targetName);

    /**
     * @brief Clear the MIDI target (stop routing to synths)
     */
    void clearTarget();

    /**
     * @brief Get the current target name
     */
    [[nodiscard]] const std::string& targetName() const { return m_targetName; }

    /**
     * @brief Also send MIDI notes to an external MidiOut
     * @param midiOutName Name of the MidiOut operator
     *
     * Use this to send sequenced notes to external hardware/software
     * while also playing on internal synths.
     */
    void setMidiOutput(const std::string& midiOutName);

    /**
     * @brief Clear the external MIDI output
     */
    void clearMidiOutput();

    /**
     * @brief Get the current MidiOut target name
     */
    [[nodiscard]] const std::string& midiOutputName() const { return m_midiOutName; }

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
     * @brief Get trigger velocity for downstream operators
     */
    float triggerVelocity() const override { return currentVelocity(); }

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
    /// @name Callbacks
    /// @{

    /**
     * @brief Set callback for step triggers (called on audio thread)
     * @param callback Function called with velocity on each active step
     *
     * Example:
     * @code
     * seq.onStep([&](float velocity) {
     *     flash.trigger(velocity);
     *     particles.burst(30);
     * });
     * @endcode
     */
    void onStep(std::function<void(float velocity)> callback) {
        m_onStepVel = std::move(callback);
    }

    /**
     * @brief Set callback for step triggers (no velocity, called on audio thread)
     * @param callback Function called on each active step
     */
    void onStep(std::function<void()> callback) {
        m_onStepSimple = std::move(callback);
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
    std::string name() const override { return "Sequencer"; }
    InspectData inspect() const override;

    // Audio thread interface
    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    // Step data (set from main thread, read from audio thread)
    std::array<Step, MAX_STEPS> m_steps = {};
    static const Step s_defaultStep;  // For out-of-range access

    // Per-step condition cycle counters
    std::array<uint16_t, MAX_STEPS> m_conditionCycle = {};

    // State (atomic for cross-thread access)
    std::atomic<int> m_currentStep{-1};
    std::atomic<bool> m_triggeredFlag{false};      // For audio thread (cleared each block)
    std::atomic<bool> m_visualTriggeredFlag{false}; // For main thread (cleared on read)
    std::atomic<float> m_currentVelocity{0.0f};
    std::atomic<uint8_t> m_currentNote{60};        // Current step's MIDI note

    // Audio thread internal state
    bool m_pendingTrigger = false;
    uint64_t m_lastTriggerCount = 0;  // For tracking Clock's trigger count

    // PRNG for probability (audio-thread-safe xorshift)
    uint32_t m_rngState = 12345;
    float randomFloat();  // Returns 0.0-1.0

    // Retrig state
    int m_retrigRemaining = 0;
    int m_retrigIntervalSamples = 0;
    int m_retrigCountdown = 0;
    float m_retrigVelocity = 0.0f;
    uint8_t m_retrigNote = 60;

    // Per-step gate timing
    int m_noteOffCountdown = -1;  // -1 = no pending note-off

    // Slide / micro-timing state
    int m_previousStep = -1;
    int m_microTimingDelaySamples = 0;
    bool m_microTimingPending = false;
    Step m_pendingMicroStep;  // Step data for delayed trigger

    // Internal advance (called on audio thread)
    void advanceInternalNoFlag();  // Advance without setting triggered flag (for catchup)
    void advanceInternal();        // Advance and set triggered flag

    // Condition / probability evaluation
    bool evaluateCondition(StepCondition cond, uint16_t cycle) const;

    // MIDI routing
    std::string m_targetName;                       // Target MidiReceiver name
    MidiReceiver* m_cachedTarget = nullptr;         // Cached target pointer
    std::string m_midiOutName;                      // External MidiSender name
    MidiSender* m_cachedMidiOut = nullptr;          // Cached MidiSender pointer
    Chain* m_chain = nullptr;                       // Chain reference for lookups

    // Note tracking for proper note-off
    uint8_t m_lastPlayedNote = 0;                   // Last note sent (for note-off)
    bool m_noteIsPlaying = false;                   // Whether we have an active note
    bool m_slideActive = false;                     // Suppress note-off for slide

    // Callbacks
    std::function<void(float)> m_onStepVel;
    std::function<void()> m_onStepSimple;

    // Internal helpers
    void sendNoteOn(uint8_t note, float velocity);
    void sendNoteOff(uint8_t note);
    void sendCC(uint8_t cc, float value);
    void fireStep(const Step& s, int stepDurationSamples);
    void resolveTargets();                          // Resolve cached pointers
};

} // namespace vivid::audio
