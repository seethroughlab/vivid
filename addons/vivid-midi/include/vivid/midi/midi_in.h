#pragma once

/**
 * @file midi_in.h
 * @brief Hardware MIDI input operator
 *
 * Receives MIDI messages from hardware controllers via RtMidi.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/param_registry.h>
#include <vivid/midi/midi_event.h>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <functional>

namespace vivid::midi {

/**
 * @brief Hardware MIDI input operator
 *
 * Receives MIDI messages from hardware controllers and makes them
 * available for polling each frame. Supports hot-pluggable devices.
 *
 * @par Example
 * @code
 * auto& midiIn = chain.add<MidiIn>("midi");
 * midiIn.openPortByName("Arturia");
 * midiIn.channel = 0;  // 0 = omni (all channels)
 *
 * // In update():
 * for (const auto& e : midiIn.events()) {
 *     if (e.type == MidiEventType::NoteOn) {
 *         synth.noteOn(midiToFreq(e.note));
 *     }
 * }
 * @endcode
 */
class MidiIn : public Operator, public ParamRegistry {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<int> channel{"channel", 0, 0, 16};  ///< Channel filter (0 = omni)

    /// @}
    // -------------------------------------------------------------------------

    MidiIn();
    ~MidiIn() override;

    // -------------------------------------------------------------------------
    /// @name Device Selection
    /// @{

    /// @brief List available MIDI input ports
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
    /// @name Event Access (poll each frame)
    /// @{

    /// @brief Get all MIDI events received this frame
    const std::vector<MidiEvent>& events() const { return m_frameEvents; }

    /// @brief Check if any note-on occurred this frame
    bool noteOn() const { return m_hasNoteOn; }

    /// @brief Check if a specific note was pressed this frame
    bool noteOn(uint8_t noteNumber) const;

    /// @brief Get the most recent note-on note number (0-127)
    uint8_t note() const { return m_lastNote; }

    /// @brief Get the most recent note-on velocity (0.0-1.0)
    float velocity() const { return m_lastVelocity; }

    /// @brief Check if any note-off occurred this frame
    bool noteOff() const { return m_hasNoteOff; }

    /// @brief Check if any CC message was received this frame
    bool ccReceived() const { return m_hasCC; }

    /// @brief Check if a specific CC was received this frame
    bool ccReceived(uint8_t ccNumber) const;

    /// @brief Get current CC value (0.0-1.0) for a controller
    /// @note Returns last known value, even if not received this frame
    float cc(uint8_t ccNumber) const;

    /// @brief Check if pitch bend was received this frame
    bool pitchBendReceived() const { return m_hasPitchBend; }

    /// @brief Get current pitch bend value (-1.0 to +1.0)
    float pitchBend() const { return m_pitchBendValue; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Callbacks (alternative to polling)
    /// @{

    /// @brief Set callback for note-on events
    void onNoteOn(std::function<void(uint8_t note, float velocity, uint8_t channel)> cb);

    /// @brief Set callback for note-off events
    void onNoteOff(std::function<void(uint8_t note, uint8_t channel)> cb);

    /// @brief Set callback for CC events
    void onCC(std::function<void(uint8_t cc, float value, uint8_t channel)> cb);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "MidiIn"; }
    OutputKind outputKind() const override { return OutputKind::Value; }

    std::vector<ParamDecl> params() override { return registeredParams(); }
    bool getParam(const std::string& name, float out[4]) override {
        return getRegisteredParam(name, out);
    }
    bool setParam(const std::string& name, const float value[4]) override {
        return setRegisteredParam(name, value);
    }

    /// @}

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    // Per-frame event buffer
    std::vector<MidiEvent> m_frameEvents;

    // Cached state
    std::array<float, 128> m_ccValues{};      // CC values (0.0-1.0)
    std::array<bool, 128> m_ccReceivedThisFrame{};
    std::array<bool, 128> m_noteOnThisFrame{};

    bool m_hasNoteOn = false;
    bool m_hasNoteOff = false;
    bool m_hasCC = false;
    bool m_hasPitchBend = false;

    uint8_t m_lastNote = 60;
    float m_lastVelocity = 0.0f;
    float m_pitchBendValue = 0.0f;

    // Callbacks
    std::function<void(uint8_t, float, uint8_t)> m_noteOnCallback;
    std::function<void(uint8_t, uint8_t)> m_noteOffCallback;
    std::function<void(uint8_t, float, uint8_t)> m_ccCallback;

    void clearFrameState();
    void processMessage(const std::vector<unsigned char>& message);
};

} // namespace vivid::midi
