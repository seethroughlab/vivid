#pragma once
// Active-notes bus — lets a GPU visual op (a sandboxed dylib on the render thread) read the set of
// MIDI notes a track is currently HOLDING, so it can draw one instance per live note (the note
// instancer: chords bloom, arps trail). Mirrors movie_audio.h: the host fills a small fixed set of
// slots each frame; the op pulls a track's notes. All functions are host-provided (resolved at dlopen
// like vivid_report_gpu_error / vivid_movie_audio_*).
//
// The op addresses a track by its STABLE id (the same id `track_<id>.*` bridge sources use), NOT a
// position — so a Notes node keeps following its track across reorder/delete, consistent with every
// other visual source. The host publishes into position slots (bounded) but TAGS each slot with the
// stable id currently occupying it; the op searches the slots for its id.
#include <stdint.h>

#define VIVID_MAX_ACTIVE_NOTES 32
#define VIVID_NOTE_BUS_TRACKS  32

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int pitch; float velocity; } VividActiveNote;

// --- consumer side (a GPU op, render thread) ---
// Copy up to `max` of the held notes of the track with STABLE id `track_id` into `out`. Returns the
// count copied (0 if that track isn't live / holds nothing). Cheap, lock-free snapshot.
uint32_t vivid_track_active_notes(int track_id, VividActiveNote* out, uint32_t max);

// --- host wiring (the app, UI/frame thread) ---
// Publish `count` held notes into position `slot` (0..VIVID_NOTE_BUS_TRACKS-1), tagged with the track's
// stable `track_id`. Pass track_id < 0 to free a slot no live track occupies. Called once per frame.
void     vivid_note_bus_publish(int slot, int track_id, const VividActiveNote* notes, uint32_t count);

#ifdef __cplusplus
}
#endif
