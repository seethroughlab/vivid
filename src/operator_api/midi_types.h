#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// MIDI port types for VIVID_PORT_HANDLE
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
// Operators write messages into this buffer; consumers read and clear it.
#define VIVID_MIDI_BUFFER_CAPACITY 64
typedef struct VividMidiBuffer {
    VividMidiMessage messages[VIVID_MIDI_BUFFER_CAPACITY];
    uint32_t         count;        // number of valid messages (0..VIVID_MIDI_BUFFER_CAPACITY)
} VividMidiBuffer;

#ifdef __cplusplus
}
#endif
