#pragma once

/**
 * @file sequencer.h
 * @brief Step sequencer for pattern-based triggering
 *
 * 16-step sequencer with per-step values. Runs on the audio thread
 * for sample-accurate timing.
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

namespace vivid {
class Chain;  // Forward declaration
}


namespace vivid::audio {

class MidiSender;    // Forward declaration

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
class Sequencer : public AudioOperator, public MidiReceiver {
public:
    static constexpr int MAX_STEPS = 16;

    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> steps{"steps", 16, 1, 16};   ///< Number of active steps
    Param<int> midiChannel{"midiChannel", 0, 0, 15};  ///< MIDI output channel (0-15)
    Param<float> gate{"gate", 0.5f, 0.01f, 1.0f};  ///< Note length as fraction of step (0-1)

    /// @}
    // -------------------------------------------------------------------------

    Sequencer() {
        registerParam(steps);
        registerParam(midiChannel);
        registerParam(gate);
        // Initialize velocities to 1.0 and notes to C4 (60)
        for (int i = 0; i < MAX_STEPS; ++i) {
            m_velocities[i] = 1.0f;
            m_notes[i] = 60;  // Default to middle C
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
     * @brief Set step with MIDI note and velocity
     * @param step Step index (0-15)
     * @param note MIDI note number (0-127, 60 = middle C)
     * @param velocity Note velocity (0-1)
     *
     * This automatically enables the step and sets both note and velocity.
     * Use this for melodic sequences.
     */
    void setStep(int step, uint8_t note, float velocity);

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
     * @brief Get step MIDI note
     * @param step Step index (0-15)
     * @return MIDI note number (0-127)
     */
    uint8_t getNote(int step) const;

    /**
     * @brief Clear all steps
     */
    void clearPattern();

    /**
     * @brief Set pattern from bitmask (for quick patterns)
     * @param pattern 16-bit pattern (bit 0 = step 0)
     */
    void setPattern(uint16_t pattern);

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
     * // Melodic pattern
     * seq.setStep(0, 60, 0.8f);  // C4
     * seq.setStep(1, 63, 0.7f);  // Eb4
     * seq.setStep(2, 67, 0.8f);  // G4
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
    // Pattern data (set from main thread, read from audio thread)
    std::array<bool, MAX_STEPS> m_pattern = {};
    std::array<float, MAX_STEPS> m_velocities = {};
    std::array<uint8_t, MAX_STEPS> m_notes = {};  // MIDI note per step (default 60)

    // State (atomic for cross-thread access)
    std::atomic<int> m_currentStep{-1};
    std::atomic<bool> m_triggeredFlag{false};      // For audio thread (cleared each block)
    std::atomic<bool> m_visualTriggeredFlag{false}; // For main thread (cleared on read)
    std::atomic<float> m_currentVelocity{0.0f};
    std::atomic<uint8_t> m_currentNote{60};        // Current step's MIDI note

    // Audio thread internal state
    bool m_pendingTrigger = false;
    uint64_t m_lastTriggerCount = 0;  // For tracking Clock's trigger count

    // Internal advance (called on audio thread)
    void advanceInternalNoFlag();  // Advance without setting triggered flag (for catchup)
    void advanceInternal();        // Advance and set triggered flag

    // MIDI routing
    std::string m_targetName;                       // Target MidiReceiver name
    MidiReceiver* m_cachedTarget = nullptr;         // Cached target pointer
    std::string m_midiOutName;                      // External MidiSender name
    MidiSender* m_cachedMidiOut = nullptr;          // Cached MidiSender pointer
    Chain* m_chain = nullptr;                       // Chain reference for lookups

    // Note tracking for proper note-off
    uint8_t m_lastPlayedNote = 0;                   // Last note sent (for note-off)
    bool m_noteIsPlaying = false;                   // Whether we have an active note

    // Internal helpers
    void sendNoteOn(uint8_t note, float velocity);
    void sendNoteOff(uint8_t note);
    void resolveTargets();                          // Resolve cached pointers
};

} // namespace vivid::audio
