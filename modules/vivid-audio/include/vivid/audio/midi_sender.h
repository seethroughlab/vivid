#pragma once

/**
 * @file midi_sender.h
 * @brief Interface for MIDI output operators
 *
 * Any operator that can send MIDI events (like MidiOut) implements this
 * interface, allowing Sequencer and Arpeggiator to route notes to external
 * devices without depending on vivid-midi directly.
 */

#include <cstdint>

namespace vivid::audio {

/**
 * @brief Interface for MIDI output operators
 *
 * Implement this interface to receive MIDI output from Sequencer, Arpeggiator,
 * or other sequencing operators. This allows routing notes to external MIDI
 * devices without circular dependencies between modules.
 *
 * @par Example Implementation (in MidiOut)
 * @code
 * class MidiOut : public Operator, public MidiSender {
 * public:
 *     void sendNoteOn(uint8_t channel, uint8_t note, float velocity) override {
 *         noteOn(channel, note, velocity);  // Use existing method
 *     }
 *
 *     void sendNoteOff(uint8_t channel, uint8_t note) override {
 *         noteOff(channel, note);
 *     }
 * };
 * @endcode
 *
 * @see MidiReceiver, Sequencer, Arpeggiator, MidiOut
 */
class MidiSender {
public:
    virtual ~MidiSender() = default;

    // -------------------------------------------------------------------------
    /// @name Required Methods
    /// @{

    /**
     * @brief Send MIDI note-on event
     * @param channel MIDI channel (0-15)
     * @param note MIDI note number (0-127)
     * @param velocity Note velocity (0.0-1.0)
     */
    virtual void sendNoteOn(uint8_t channel, uint8_t note, float velocity) = 0;

    /**
     * @brief Send MIDI note-off event
     * @param channel MIDI channel (0-15)
     * @param note MIDI note number (0-127)
     */
    virtual void sendNoteOff(uint8_t channel, uint8_t note) = 0;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Optional Methods (defaults provided)
    /// @{

    /**
     * @brief Send MIDI control change
     * @param channel MIDI channel (0-15)
     * @param cc Controller number (0-127)
     * @param value Controller value (0.0-1.0)
     */
    virtual void sendCC(uint8_t channel, uint8_t cc, float value) {
        (void)channel;
        (void)cc;
        (void)value;
    }

    /**
     * @brief Send MIDI pitch bend
     * @param channel MIDI channel (0-15)
     * @param bend Bend amount (-1.0 to +1.0)
     */
    virtual void sendPitchBend(uint8_t channel, float bend) {
        (void)channel;
        (void)bend;
    }

    /**
     * @brief Send all notes off on a channel
     * @param channel MIDI channel (0-15)
     */
    virtual void sendAllNotesOff(uint8_t channel) {
        (void)channel;
    }

    /// @}
};

} // namespace vivid::audio
