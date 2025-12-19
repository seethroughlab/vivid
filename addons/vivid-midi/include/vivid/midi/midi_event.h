#pragma once

/**
 * @file midi_event.h
 * @brief MIDI event types and structures
 */

#include <cstdint>
#include <algorithm>

namespace vivid::midi {

/**
 * @brief MIDI event types
 */
enum class MidiEventType : uint8_t {
    NoteOn,         ///< Note pressed (velocity > 0)
    NoteOff,        ///< Note released
    ControlChange,  ///< CC message (knob, slider, etc.)
    ProgramChange,  ///< Patch/program change
    PitchBend,      ///< Pitch wheel
    Aftertouch,     ///< Channel pressure
    PolyPressure,   ///< Polyphonic key pressure
    Clock,          ///< MIDI clock tick (24 ppq)
    Start,          ///< Sequence start
    Stop,           ///< Sequence stop
    Continue,       ///< Sequence continue
};

/**
 * @brief MIDI event data
 *
 * Represents a single MIDI message with all relevant data fields.
 * Not all fields are used for every event type.
 */
struct MidiEvent {
    MidiEventType type = MidiEventType::NoteOff;
    uint8_t channel = 0;      ///< MIDI channel (0-15)
    uint8_t note = 60;        ///< Note number (0-127), middle C = 60
    uint8_t velocity = 0;     ///< Velocity (0-127) for note events
    uint8_t cc = 0;           ///< Controller number for CC events
    uint8_t value = 0;        ///< Value for CC/program change (0-127)
    int16_t pitchBend = 0;    ///< Pitch bend (-8192 to +8191)
    uint32_t timestamp = 0;   ///< Sample offset within frame (for SMF playback)

    // -------------------------------------------------------------------------
    /// @name Factory Methods
    /// @{

    static MidiEvent noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
        MidiEvent e;
        e.type = MidiEventType::NoteOn;
        e.channel = ch;
        e.note = note;
        e.velocity = vel;
        return e;
    }

    static MidiEvent noteOff(uint8_t ch, uint8_t note, uint8_t vel = 0) {
        MidiEvent e;
        e.type = MidiEventType::NoteOff;
        e.channel = ch;
        e.note = note;
        e.velocity = vel;
        return e;
    }

    static MidiEvent controlChange(uint8_t ch, uint8_t controller, uint8_t val) {
        MidiEvent e;
        e.type = MidiEventType::ControlChange;
        e.channel = ch;
        e.cc = controller;
        e.value = val;
        return e;
    }

    static MidiEvent programChange(uint8_t ch, uint8_t program) {
        MidiEvent e;
        e.type = MidiEventType::ProgramChange;
        e.channel = ch;
        e.value = program;
        return e;
    }

    static MidiEvent pitchBendEvent(uint8_t ch, int16_t bend) {
        MidiEvent e;
        e.type = MidiEventType::PitchBend;
        e.channel = ch;
        e.pitchBend = bend;
        return e;
    }

    /// @}
};

// -------------------------------------------------------------------------
/// @name Conversion Utilities
/// @{

/// @brief Convert MIDI velocity (0-127) to normalized float (0.0-1.0)
inline float velocityToFloat(uint8_t vel) {
    return static_cast<float>(vel) / 127.0f;
}

/// @brief Convert normalized float (0.0-1.0) to MIDI velocity (0-127)
inline uint8_t floatToVelocity(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 127.0f);
}

/// @brief Convert MIDI CC value (0-127) to normalized float (0.0-1.0)
inline float ccToFloat(uint8_t val) {
    return static_cast<float>(val) / 127.0f;
}

/// @brief Convert normalized float (0.0-1.0) to MIDI CC value (0-127)
inline uint8_t floatToCC(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 127.0f);
}

/// @brief Convert pitch bend (-8192 to +8191) to normalized float (-1.0 to +1.0)
inline float pitchBendToFloat(int16_t bend) {
    return static_cast<float>(bend) / 8192.0f;
}

/// @brief Convert normalized float (-1.0 to +1.0) to pitch bend (-8192 to +8191)
inline int16_t floatToPitchBend(float v) {
    return static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 8192.0f);
}

/// @}

// -------------------------------------------------------------------------
/// @name Common CC Numbers
/// @{

namespace CC {
    constexpr uint8_t ModWheel = 1;
    constexpr uint8_t BreathController = 2;
    constexpr uint8_t FootController = 4;
    constexpr uint8_t PortamentoTime = 5;
    constexpr uint8_t DataEntry = 6;
    constexpr uint8_t Volume = 7;
    constexpr uint8_t Balance = 8;
    constexpr uint8_t Pan = 10;
    constexpr uint8_t Expression = 11;
    constexpr uint8_t Sustain = 64;
    constexpr uint8_t Portamento = 65;
    constexpr uint8_t Sostenuto = 66;
    constexpr uint8_t SoftPedal = 67;
    constexpr uint8_t AllSoundOff = 120;
    constexpr uint8_t ResetAllControllers = 121;
    constexpr uint8_t AllNotesOff = 123;
}

/// @}

} // namespace vivid::midi
