#pragma once

/**
 * @file midi_out.h
 * @brief Hardware MIDI output operator
 *
 * Sends MIDI messages to external hardware or software via RtMidi.
 */

#include <vivid/operator.h>
#include <vivid/operator_registry.h>
#include <vivid/audio/midi_sender.h>
#include <vivid/midi/midi_event.h>
#include <string>
#include <vector>
#include <memory>

namespace vivid::midi {

/**
 * @brief Hardware MIDI output operator
 *
 * Sends MIDI messages to external synthesizers, DAWs, or other
 * MIDI-compatible devices.
 *
 * @par Example
 * @code
 * auto& midiOut = chain.add<MidiOut>("midiOut");
 * midiOut.openPortByName("IAC Driver");
 *
 * // In update():
 * midiOut.noteOn(0, 60, 0.8f);   // Channel 0, middle C, velocity 0.8
 * midiOut.sendCC(0, 1, 0.5f);    // Mod wheel to 50%
 * @endcode
 
 * @see MidiIn, MidiFilePlayer, Sequencer, OscOut
 */
class MidiOut : public Operator, public audio::MidiSender {
public:

    MidiOut();
    ~MidiOut() override;

    // -------------------------------------------------------------------------
    /// @name Device Selection
    /// @{

    /// @brief List available MIDI output ports
    static std::vector<std::string> listPorts();

    /// @brief Open a MIDI port by index
    void openPort(unsigned int portIndex);

    /// @brief Open a MIDI port by name (partial match)
    void openPortByName(const std::string& name);

    /// @brief Close the current port
    void closePort();

    /// @brief Check if a port is open
    [[nodiscard]] bool isOpen() const;

    /// @brief Get the name of the open port
    [[nodiscard]] const std::string& portName() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Send MIDI Messages
    /// @{

    /// @brief Send note-on message
    /// @param channel MIDI channel (0-15)
    /// @param note Note number (0-127)
    /// @param velocity Velocity (0.0-1.0)
    void noteOn(uint8_t channel, uint8_t note, float velocity);

    /// @brief Send note-off message
    /// @param channel MIDI channel (0-15)
    /// @param note Note number (0-127)
    void noteOff(uint8_t channel, uint8_t note);

    /// @brief Send control change message
    /// @param channel MIDI channel (0-15)
    /// @param cc Controller number (0-127)
    /// @param value Value (0.0-1.0)
    void sendCC(uint8_t channel, uint8_t cc, float value) override;

    /// @brief Send program change message
    /// @param channel MIDI channel (0-15)
    /// @param program Program number (0-127)
    void programChange(uint8_t channel, uint8_t program);

    /// @brief Send pitch bend message
    /// @param channel MIDI channel (0-15)
    /// @param bend Bend amount (-1.0 to +1.0)
    void sendPitchBend(uint8_t channel, float bend) override;

    /// @brief Send a raw MIDI event
    void send(const MidiEvent& event);

    /// @brief Send all notes off on a channel (panic)
    void allNotesOff(uint8_t channel);

    /// @brief Send all notes off on all channels (panic)
    void panic();

    /// @}
    // -------------------------------------------------------------------------
    /// @name MidiSender Interface
    /// @{

    /// @brief MidiSender: Send note-on (routes to noteOn)
    void sendNoteOn(uint8_t channel, uint8_t note, float velocity) override {
        noteOn(channel, note, velocity);
    }

    /// @brief MidiSender: Send note-off (routes to noteOff)
    void sendNoteOff(uint8_t channel, uint8_t note) override {
        noteOff(channel, note);
    }

    /// @brief MidiSender: Send all notes off (routes to allNotesOff)
    void sendAllNotesOff(uint8_t channel) override {
        allNotesOff(channel);
    }

    // Note: sendCC and sendPitchBend already exist with matching signatures,
    // so they automatically satisfy the MidiSender interface requirements

    /// @}
    // -------------------------------------------------------------------------
    /// @name MIDI Clock/Transport
    /// @{

    /// @brief Send MIDI clock tick (0xF8)
    void sendClock();

    /// @brief Send MIDI Start message (0xFA)
    void sendStart();

    /// @brief Send MIDI Stop message (0xFC)
    void sendStop();

    /// @brief Send MIDI Continue message (0xFB)
    void sendContinue();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "MidiOut"; }
    OutputKind outputKind() const override { return OutputKind::Value; }
    bool drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) override;

    /// @}

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    void sendRaw(const std::vector<unsigned char>& message);
};

} // namespace vivid::midi
