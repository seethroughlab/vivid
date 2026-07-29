#pragma once
// Note-EVENT bus — the discrete on/off counterpart to the active-notes (held-set) bus (note_bus.h).
// Where note_bus.h answers "which notes are held right now", this answers "which notes just started
// or stopped this frame", carrying per-voice identity (note_id). It lets a one-shot visual op spawn
// on each note-on — including a re-struck held pitch, which membership can't distinguish. Host-
// provided functions, resolved at dlopen like vivid_track_active_notes.
//
// Addressed by track STABLE id, exactly like the active-notes bus. The host drains each track's
// audio-thread event ring once per frame and publishes a snapshot here; ops read it within the same
// topo-ordered pass (never keep the pointer past process_gpu).
#include <stdint.h>

#define VIVID_MAX_NOTE_EVENTS   64
#define VIVID_NOTE_EVENT_TRACKS 32

#ifdef __cplusplus
extern "C" {
#endif

// kind: 1 = note-on, 0 = note-off.
typedef struct { int kind; int pitch; float velocity; int note_id; } VividNoteHit;

// --- consumer side (a GPU op, render thread) ---
// Copy up to `max` of the events published for the track with STABLE id `track_id` into `out`.
// Returns the count (0 if that track isn't live / had no events this frame). Cheap, lock-free snapshot.
uint32_t vivid_track_note_events(int track_id, VividNoteHit* out, uint32_t max);

// --- host wiring (the app, UI/frame thread) ---
// Publish `count` events into position `slot` (0..VIVID_NOTE_EVENT_TRACKS-1), tagged with the track's
// stable `track_id`. Pass track_id < 0 to free a slot no live track occupies. Called once per frame.
void     vivid_note_event_bus_publish(int slot, int track_id, const VividNoteHit* ev, uint32_t count);

#ifdef __cplusplus
}
#endif
