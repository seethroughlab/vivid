#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Native note transport — vivid's internal per-note event protocol.
//
// VividNoteBuffer is the canonical wire type for note streams between
// operators inside the graph. Every per-note event carries a stable
// `note_id` (uint64) allocated by the emitter at note-on, kept across all
// follow-up expression updates, and reused on the matching note-off.
// Re-triggering the same MIDI pitch produces a fresh `note_id` — so legato
// retriggers and same-pitch overlap allocate distinct synth voices.
//
// External MIDI 1.0 and MPE live at the I/O boundary (MidiInput,
// MidiFilePlayer, future MpeOutput). Inside the graph everything speaks
// VividNoteBuffer.
//
// See docs/plans/midi-native-protocol/phase-1-wire-format.md for the full
// design and migration plan.
// ---------------------------------------------------------------------------

typedef enum VividNoteEventType {
    VIVID_NOTE_ON         = 0,  // start a note: value = velocity 0..1
    VIVID_NOTE_OFF        = 1,  // end a note: value unused
    VIVID_NOTE_PITCH_BEND = 2,  // per-note pitch bend: value = semitones
    VIVID_NOTE_PRESSURE   = 3,  // per-note pressure (Z-axis): value = 0..1
    VIVID_NOTE_TIMBRE     = 4,  // per-note timbre / Y-axis: value = 0..1
} VividNoteEventType;

// A single timestamped note event within a frame window.
typedef struct VividNoteEvent {
    uint8_t  type;                 // VividNoteEventType
    uint8_t  note_number;          // 0..127 — meaningful for ON/OFF; consumers
                                   // may also stash the originating note number
                                   // on per-note expression events for debugging.
    uint16_t reserved;             // padding to 4 bytes
    uint32_t frame_offset_samples; // sample offset within the current buffer
    uint64_t note_id;              // non-zero for every per-note event;
                                   // 0 reserved for graph-wide global events
                                   // (sustain pedal, program change, etc.)
    float    value;                // ON: velocity 0..1
                                   // OFF: unused (set to 0)
                                   // PITCH_BEND: signed semitones
                                   // PRESSURE / TIMBRE: 0..1
} VividNoteEvent;

// A fixed-capacity buffer of note events for one audio buffer tick.
// Operators write events into this buffer; consumers read and clear it.
#define VIVID_NOTE_BUFFER_CAPACITY 64
typedef struct VividNoteBuffer {
    VividNoteEvent events[VIVID_NOTE_BUFFER_CAPACITY];
    uint32_t       count;          // number of valid events (0..VIVID_NOTE_BUFFER_CAPACITY)
} VividNoteBuffer;

#ifdef __cplusplus
}

#include "operator_api/type_id.h"
VIVID_DECLARE_CUSTOM_REF_TYPE(VividNoteBuffer,
                              "seethroughlab.vivid.note_buffer_v1",
                              "VividNoteBuffer",
                              false);
#endif
