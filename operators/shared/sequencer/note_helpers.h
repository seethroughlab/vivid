#pragma once
#include "operator_api/note_types.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// Emit helpers for the native note transport (VividNoteBuffer).
//
// Every emitter calls these to append events to its outbound buffer:
//
//   note_on(buf, note_number, velocity_0_1, note_id, frame_offset)
//   note_off(buf, note_id, frame_offset)
//   note_pitch_bend(buf, note_id, semis, frame_offset)
//   note_pressure(buf, note_id, value_0_1, frame_offset)
//   note_timbre(buf, note_id, value_0_1, frame_offset)
//
// Per the Phase 1 contract every per-note event MUST carry a non-zero
// note_id. Helpers reject zero ids in debug builds (assert) and silently
// drop the event in release. note_id == 0 is reserved for graph-wide
// global events (sustain pedal, program change, etc.) which Phase 1 does
// not yet emit.
//
// All helpers return false (and append nothing) when the buffer is full.
// ---------------------------------------------------------------------------

namespace vivid_sequencers {

namespace detail {

inline VividNoteEvent* push(VividNoteBuffer& buf) {
    if (buf.count >= VIVID_NOTE_BUFFER_CAPACITY) return nullptr;
    VividNoteEvent& ev = buf.events[buf.count++];
    ev.type        = 0;
    ev.note_number = 0;
    ev.reserved    = 0;
    ev.frame_offset_samples = 0;
    ev.note_id     = 0;
    ev.value       = 0.0f;
    return &ev;
}

}  // namespace detail

inline bool note_on(VividNoteBuffer& buf, uint8_t note_number,
                    float velocity_0_1, uint64_t note_id,
                    uint32_t frame_offset = 0) {
    if (note_id == 0) return false;
    VividNoteEvent* ev = detail::push(buf);
    if (!ev) return false;
    ev->type                 = VIVID_NOTE_ON;
    ev->note_number          = note_number;
    ev->frame_offset_samples = frame_offset;
    ev->note_id              = note_id;
    ev->value                = velocity_0_1;
    return true;
}

inline bool note_off(VividNoteBuffer& buf, uint64_t note_id,
                     uint32_t frame_offset = 0) {
    if (note_id == 0) return false;
    VividNoteEvent* ev = detail::push(buf);
    if (!ev) return false;
    ev->type                 = VIVID_NOTE_OFF;
    ev->frame_offset_samples = frame_offset;
    ev->note_id              = note_id;
    return true;
}

inline bool note_pitch_bend(VividNoteBuffer& buf, uint64_t note_id,
                            float semis, uint32_t frame_offset = 0) {
    if (note_id == 0) return false;
    VividNoteEvent* ev = detail::push(buf);
    if (!ev) return false;
    ev->type                 = VIVID_NOTE_PITCH_BEND;
    ev->frame_offset_samples = frame_offset;
    ev->note_id              = note_id;
    ev->value                = semis;
    return true;
}

inline bool note_pressure(VividNoteBuffer& buf, uint64_t note_id,
                          float value_0_1, uint32_t frame_offset = 0) {
    if (note_id == 0) return false;
    VividNoteEvent* ev = detail::push(buf);
    if (!ev) return false;
    ev->type                 = VIVID_NOTE_PRESSURE;
    ev->frame_offset_samples = frame_offset;
    ev->note_id              = note_id;
    ev->value                = value_0_1;
    return true;
}

inline bool note_timbre(VividNoteBuffer& buf, uint64_t note_id,
                        float value_0_1, uint32_t frame_offset = 0) {
    if (note_id == 0) return false;
    VividNoteEvent* ev = detail::push(buf);
    if (!ev) return false;
    ev->type                 = VIVID_NOTE_TIMBRE;
    ev->frame_offset_samples = frame_offset;
    ev->note_id              = note_id;
    ev->value                = value_0_1;
    return true;
}

}  // namespace vivid_sequencers
