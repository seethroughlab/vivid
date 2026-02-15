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
#include <vivid/operator_registry.h>
#include <vivid/midi/midi_event.h>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <unordered_map>

namespace vivid {
class Chain;  // Forward declaration
}

namespace vivid::audio {
class MidiReceiver;  // Forward declaration
}

namespace vivid::midi {

/**
 * @brief CC-to-parameter mapping for routing CCs to operator parameters
 */
struct CCMapping {
    uint8_t cc;                  ///< MIDI CC number (0-127)
    std::string targetOp;        ///< Target operator name
    std::string paramName;       ///< Target parameter name
    float minVal = 0.0f;         ///< Minimum output value
    float maxVal = 1.0f;         ///< Maximum output value
};

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
 
 * @see MidiOut, MidiFilePlayer, Synth, FMSynth, Sampler, OscIn
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
    [[nodiscard]] bool isOpen() const;

    /// @brief Get the name of the open port
    [[nodiscard]] const std::string& portName() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Event Access (poll each frame)
    /// @{

    /// @brief Get all MIDI events received this frame
    [[nodiscard]] const std::vector<MidiEvent>& events() const { return m_frameEvents; }

    /// @brief Check if any note-on occurred this frame
    [[nodiscard]] bool noteOn() const { return m_hasNoteOn; }

    /// @brief Check if a specific note was pressed this frame
    [[nodiscard]] bool noteOn(uint8_t noteNumber) const;

    /// @brief Get the most recent note-on note number (0-127)
    [[nodiscard]] uint8_t note() const { return m_lastNote; }

    /// @brief Get the most recent note-on velocity (0.0-1.0)
    [[nodiscard]] float velocity() const { return m_lastVelocity; }

    /// @brief Check if any note-off occurred this frame
    [[nodiscard]] bool noteOff() const { return m_hasNoteOff; }

    /// @brief Check if any CC message was received this frame
    [[nodiscard]] bool ccReceived() const { return m_hasCC; }

    /// @brief Check if a specific CC was received this frame
    [[nodiscard]] bool ccReceived(uint8_t ccNumber) const;

    /// @brief Get current CC value (0.0-1.0) for a controller
    /// @note Returns last known value, even if not received this frame
    [[nodiscard]] float cc(uint8_t ccNumber) const;

    /// @brief Check if pitch bend was received this frame
    [[nodiscard]] bool pitchBendReceived() const { return m_hasPitchBend; }

    /// @brief Get current pitch bend value (-1.0 to +1.0)
    [[nodiscard]] float pitchBend() const { return m_pitchBendValue; }

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
    /// @name Native MIDI Routing
    /// @{

    /**
     * @brief Route MIDI notes to a named synth operator
     * @param targetName Name of the synth operator (must implement MidiReceiver)
     *
     * When a target is set, all note-on, note-off, and pitch bend events
     * are automatically forwarded to the target synth without manual polling.
     *
     * @par Example
     * @code
     * auto& midiIn = chain.add<MidiIn>("midi");
     * auto& synth = chain.add<PolySynth>("synth");
     * midiIn.setTarget("synth");  // Notes now play on synth automatically
     * @endcode
     */
    void setTarget(const std::string& targetName);

    /**
     * @brief Clear the MIDI target (stop auto-routing)
     */
    void clearTarget();

    /**
     * @brief Get the current target name
     * @return Target operator name, or empty string if not set
     */
    [[nodiscard]] const std::string& targetName() const { return m_targetName; }

    /**
     * @brief Route MIDI clock events to a Clock operator
     * @param targetName Name of the Clock operator
     *
     * When set, MIDI clock (24 PPQ), start, stop, and continue messages
     * are forwarded to sync the Clock's tempo to external gear.
     */
    void setClockTarget(const std::string& targetName);

    /**
     * @brief Clear the clock target
     */
    void clearClockTarget();

    /// @}
    // -------------------------------------------------------------------------
    /// @name CC-to-Parameter Mapping
    /// @{

    /**
     * @brief Map a MIDI CC to an operator parameter
     * @param cc MIDI controller number (0-127)
     * @param targetOp Name of the target operator
     * @param paramName Name of the parameter to control
     * @param minVal Minimum output value (default 0.0)
     * @param maxVal Maximum output value (default 1.0)
     *
     * When the specified CC is received, its value (0-127) is scaled to
     * the minVal-maxVal range and applied to the target parameter.
     *
     * @par Example
     * @code
     * midiIn.mapCC(1, "synth", "filterCutoff");              // Mod wheel -> filter
     * midiIn.mapCC(74, "synth", "volume", 0.0f, 0.8f);       // CC74 -> volume (max 80%)
     * midiIn.mapCC(91, "reverb", "mix", 0.0f, 0.6f);         // CC91 -> reverb mix
     * @endcode
     */
    void mapCC(uint8_t cc, const std::string& targetOp,
               const std::string& paramName,
               float minVal = 0.0f, float maxVal = 1.0f);

    /**
     * @brief Remove a CC mapping
     * @param cc MIDI controller number to unmap
     */
    void unmapCC(uint8_t cc);

    /**
     * @brief Remove all CC mappings
     */
    void clearCCMappings();

    /**
     * @brief Get all CC mappings
     * @return Vector of all current mappings
     */
    [[nodiscard]] const std::vector<CCMapping>& ccMappings() const { return m_ccMappings; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "MidiIn"; }
    OutputKind outputKind() const override { return OutputKind::Value; }
    bool drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) override;

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

    // MIDI routing
    std::string m_targetName;                           ///< Target synth name
    audio::MidiReceiver* m_cachedTarget = nullptr;      ///< Cached target pointer
    std::string m_clockTargetName;                      ///< Clock sync target name

    // CC mappings
    std::vector<CCMapping> m_ccMappings;

    // Cached chain pointer for resolving targets
    Chain* m_chain = nullptr;

    // Operator name (cached from chain for MidiMapStore)
    std::string m_operatorName;

    // MidiMapStore sync
    uint32_t m_lastMapStoreVersion = 0;
    void syncMappingsFromStore();

    void clearFrameState();
    void processMessage(const std::vector<unsigned char>& message);
    void routeToTarget(const MidiEvent& event);
    void applyCCMapping(uint8_t cc, float value);
};

} // namespace vivid::midi
