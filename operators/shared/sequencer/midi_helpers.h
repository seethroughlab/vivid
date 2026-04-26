#pragma once
#include "operator_api/midi_types.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// Internal MIDI 1.0 helpers — used by boundary operators (MidiInput,
// MidiFilePlayer) for parsing and emitting raw MIDI 1.0 messages on the
// external side of the I/O boundary.
//
// Inside the graph, operators use the native note transport
// (operators/shared/sequencer/note_helpers.h + VividNoteBuffer); these
// MIDI 1.0 helpers are NOT for emitting wire data between operators.
//
// See docs/plans/midi-native-protocol/phase-1-wire-format.md.
// ---------------------------------------------------------------------------

namespace vivid_sequencers {

inline bool midi_note_on(VividMidiBuffer& buf, uint8_t note, uint8_t velocity,
                         uint8_t channel = 0, uint32_t frame_offset = 0) {
    if (buf.count >= VIVID_MIDI_BUFFER_CAPACITY) return false;
    VividMidiMessage& msg = buf.messages[buf.count++];
    msg.status = static_cast<uint8_t>(0x90 | (channel & 0x0F));
    msg.data1 = note;
    msg.data2 = velocity;
    msg.reserved = 0;
    msg.frame_offset_samples = frame_offset;
    return true;
}

inline bool midi_note_off(VividMidiBuffer& buf, uint8_t note,
                          uint8_t channel = 0, uint32_t frame_offset = 0) {
    if (buf.count >= VIVID_MIDI_BUFFER_CAPACITY) return false;
    VividMidiMessage& msg = buf.messages[buf.count++];
    msg.status = static_cast<uint8_t>(0x80 | (channel & 0x0F));
    msg.data1 = note;
    msg.data2 = 0;
    msg.reserved = 0;
    msg.frame_offset_samples = frame_offset;
    return true;
}

} // namespace vivid_sequencers
