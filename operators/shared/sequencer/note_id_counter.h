#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// Note-id allocator for native note emitters.
//
// Every emitter (Tracker, NotePattern, Sequencer, Arpeggiator, Euclidean,
// DrumSequencer, MidiInput, MidiClip, ...) calls next_note_id() at
// each note-on to obtain a stable identity for that note. The id follows
// the note across all expression updates and the matching note-off.
//
// The counter is thread-local because audio operators run on the audio
// thread; cross-emitter id collisions don't matter because synth slot
// maps are per-instance and consumers correlate by id within a single
// stream.
// ---------------------------------------------------------------------------

namespace vivid_sequencers {

inline uint64_t next_note_id() {
    static thread_local uint64_t counter = 0;
    return ++counter;  // first allocated id is 1; 0 is reserved for global stream
}

}  // namespace vivid_sequencers
