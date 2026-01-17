#pragma once

/**
 * @file arpeggiator.h
 * @brief MIDI arpeggiator for creating patterns from held notes
 *
 * Receives MIDI notes and outputs them as arpeggiated patterns,
 * routable to internal synths or external MIDI devices.
 */

#include <vivid/audio_operator.h>
#include <vivid/audio/midi_receiver.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <cstdint>

namespace vivid {
class Chain;  // Forward declaration
}


namespace vivid::audio {

class Clock;       // Forward declaration
class MidiSender;  // Forward declaration

/**
 * @brief Arpeggiator pattern mode
 */
enum class ArpMode {
    Up,       ///< Play notes from lowest to highest
    Down,     ///< Play notes from highest to lowest
    UpDown,   ///< Play up then down (ping-pong)
    Random,   ///< Play notes in random order
    Order     ///< Play notes in the order they were pressed
};

/**
 * @brief MIDI arpeggiator operator
 *
 * Receives MIDI notes (via MidiReceiver interface) and outputs them as
 * arpeggiated patterns. Can route output to internal synths (MidiReceiver)
 * and/or external MIDI devices (MidiOut).
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | octaves | int | 1-4 | 1 | Number of octaves to arpeggiate |
 * | gate | float | 0.01-1 | 0.5 | Note length as fraction of step |
 * | midiChannel | int | 0-15 | 0 | MIDI output channel |
 *
 * @par Example
 * @code
 * auto& midiIn = chain.add<MidiIn>("midi");
 * auto& clock = chain.add<Clock>("clock");
 * clock.bpm = 120.0f;
 * clock.division(ClockDiv::Sixteenth);
 *
 * auto& arp = chain.add<Arpeggiator>("arp");
 * arp.setTriggerSource("clock");
 * arp.mode(ArpMode::Up);
 * arp.octaves = 2;
 *
 * auto& synth = chain.add<PolySynth>("synth");
 *
 * midiIn.setTarget("arp");     // Controller notes -> Arpeggiator
 * arp.setTarget("synth");      // Arpeggiated notes -> Synth
 * @endcode
 *
 * @see Sequencer, MidiIn, MidiOut, PolySynth, Clock
 */
class Arpeggiator : public AudioOperator, public MidiReceiver {
public:
    static constexpr int MAX_HELD_NOTES = 16;

    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> octaves{"octaves", 1, 1, 4};       ///< Octave range for arpeggio
    Param<float> gate{"gate", 0.5f, 0.01f, 1.0f}; ///< Note length (fraction of step)
    Param<int> midiChannel{"midiChannel", 0, 0, 15}; ///< MIDI output channel

    /// @}
    // -------------------------------------------------------------------------

    Arpeggiator();
    ~Arpeggiator() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set the arpeggiator pattern mode
     * @param m The arpeggio mode (Up, Down, UpDown, Random, Order)
     */
    void mode(ArpMode m) { m_mode = m; }

    /**
     * @brief Get the current mode
     */
    ArpMode mode() const { return m_mode; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name MIDI Routing (Output)
    /// @{

    /**
     * @brief Route arpeggiated notes to a MidiReceiver (synth/sampler)
     * @param targetName Name of the target operator
     *
     * @par Example
     * @code
     * arp.setTarget("synth");  // Notes go to PolySynth
     * @endcode
     */
    void setTarget(const std::string& targetName);

    /**
     * @brief Clear the internal MIDI target
     */
    void clearTarget();

    /**
     * @brief Get the current target name
     */
    [[nodiscard]] const std::string& targetName() const { return m_targetName; }

    /**
     * @brief Also send notes to an external MidiOut
     * @param midiOutName Name of the MidiOut operator
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
    /// @name MidiReceiver Interface (Input)
    /// @{

    /**
     * @brief Receive a note-on event (adds note to held notes)
     */
    void midiNoteOn(uint8_t note, float velocity, uint8_t channel = 0) override;

    /**
     * @brief Receive a note-off event (removes note from held notes)
     */
    void midiNoteOff(uint8_t note, float velocity = 0.0f, uint8_t channel = 0) override;

    /**
     * @brief Release all held notes
     */
    void midiAllNotesOff() override;

    /**
     * @brief Immediately stop all notes and clear state
     */
    void midiPanic() override;

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    /**
     * @brief Get the number of currently held notes
     */
    int heldNoteCount() const { return m_heldNoteCount.load(std::memory_order_relaxed); }

    /**
     * @brief Check if the arpeggiator triggered this block
     */
    bool triggered() const override { return m_triggeredFlag.load(std::memory_order_acquire); }

    /**
     * @brief Get the current step index in the arpeggio
     */
    int currentStep() const { return m_currentStep.load(std::memory_order_relaxed); }

    /**
     * @brief Get the last note that was output
     */
    uint8_t currentNote() const { return m_currentNote.load(std::memory_order_relaxed); }

    /**
     * @brief Reset arpeggiator to initial state (keeps held notes)
     */
    void reset();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Arpeggiator"; }

    // Audio thread interface
    void generateBlock(uint32_t frameCount) override;

    /// @}

protected:
    void onTrigger() override;

private:
    // Held notes (input from MidiReceiver)
    struct HeldNote {
        uint8_t note = 0;
        float velocity = 0.0f;
        uint32_t order = 0;  // For Order mode (track press sequence)
    };

    std::array<HeldNote, MAX_HELD_NOTES> m_heldNotes = {};
    std::atomic<int> m_heldNoteCount{0};
    uint32_t m_noteOrderCounter = 0;

    // Arpeggiator state
    ArpMode m_mode = ArpMode::Up;
    std::atomic<int> m_currentStep{0};
    std::atomic<uint8_t> m_currentNote{0};
    std::atomic<bool> m_triggeredFlag{false};
    bool m_goingUp = true;  // For UpDown mode

    // MIDI routing
    std::string m_targetName;
    MidiReceiver* m_cachedTarget = nullptr;
    std::string m_midiOutName;
    MidiSender* m_cachedMidiOut = nullptr;
    Chain* m_chain = nullptr;

    // Note tracking
    uint8_t m_lastPlayedNote = 0;
    bool m_noteIsPlaying = false;

    // Trigger tracking
    bool m_pendingTrigger = false;
    uint64_t m_lastTriggerCount = 0;

    // Internal helpers
    void advanceArpeggio();
    void playCurrentNote();
    void stopCurrentNote();
    void sendNoteOn(uint8_t note, float velocity);
    void sendNoteOff(uint8_t note);
    void resolveTargets();
    int getNextNoteIndex();
    void sortHeldNotes();  // Sort by pitch for Up/Down modes
};

} // namespace vivid::audio
