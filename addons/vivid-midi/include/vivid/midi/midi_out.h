#pragma once

/**
 * @file midi_out.h
 * @brief Hardware MIDI output operator
 *
 * Sends MIDI messages to external hardware or software via RtMidi.
 */

#include <vivid/operator.h>
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
 */
class MidiOut : public Operator {
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
    bool isOpen() const;

    /// @brief Get the name of the open port
    std::string portName() const;

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
    void sendCC(uint8_t channel, uint8_t cc, float value);

    /// @brief Send program change message
    /// @param channel MIDI channel (0-15)
    /// @param program Program number (0-127)
    void programChange(uint8_t channel, uint8_t program);

    /// @brief Send pitch bend message
    /// @param channel MIDI channel (0-15)
    /// @param bend Bend amount (-1.0 to +1.0)
    void sendPitchBend(uint8_t channel, float bend);

    /// @brief Send a raw MIDI event
    void send(const MidiEvent& event);

    /// @brief Send all notes off on a channel (panic)
    void allNotesOff(uint8_t channel);

    /// @brief Send all notes off on all channels (panic)
    void panic();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "MidiOut"; }
    OutputKind outputKind() const override { return OutputKind::Value; }
    bool drawVisualization(ImDrawList* dl, float minX, float minY, float maxX, float maxY) override;

    /// @}

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    void sendRaw(const std::vector<unsigned char>& message);
};

} // namespace vivid::midi
