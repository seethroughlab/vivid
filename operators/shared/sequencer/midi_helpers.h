#pragma once
#include "operator_api/midi_types.h"
#include <cstdint>

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
