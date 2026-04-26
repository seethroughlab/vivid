#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// MIDI 1.0 message types — INTERNAL parser intermediates only.
//
// These structs live here for use inside boundary operators (MidiInput,
// MidiFilePlayer) that parse external MIDI 1.0 wire data. They are NOT a
// registered custom-ref port type, so connecting one to a port will fail
// graph compilation. Inside the graph, every note stream uses
// VividNoteBuffer (operator_api/note_types.h) — the native note transport
// keyed by note_id.
//
// See docs/plans/midi-native-protocol/phase-1-wire-format.md.
// ---------------------------------------------------------------------------

// A single timestamped MIDI message within a frame window.
typedef struct VividMidiMessage {
    uint8_t  status;               // MIDI status byte (e.g. 0x90 = note-on ch1)
    uint8_t  data1;                // first data byte (e.g. note number)
    uint8_t  data2;                // second data byte (e.g. velocity)
    uint8_t  reserved;             // padding
    uint32_t frame_offset_samples; // sample offset within the current buffer
} VividMidiMessage;

// A fixed-capacity ring of MIDI messages for one audio buffer tick.
// Used as a parser intermediate inside boundary operators only.
#define VIVID_MIDI_BUFFER_CAPACITY 64
typedef struct VividMidiBuffer {
    VividMidiMessage messages[VIVID_MIDI_BUFFER_CAPACITY];
    uint32_t         count;        // number of valid messages (0..VIVID_MIDI_BUFFER_CAPACITY)
} VividMidiBuffer;

#ifdef __cplusplus
}
#endif
