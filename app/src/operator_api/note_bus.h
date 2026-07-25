#pragma once
// Active-notes bus — lets a GPU visual op (a sandboxed dylib on the render thread) read the set of
// MIDI notes a track is currently HOLDING, so it can draw one instance per live note (the note
// instancer: chords bloom, arps trail). Mirrors movie_audio.h: the host publishes each track's held
// notes into a small fixed per-track bus each frame; the op pulls its track's notes by index. All
// functions are host-provided (resolved at dlopen like vivid_report_gpu_error / vivid_movie_audio_*).
// The op and the host are linked by the track index alone (an ordinary int param on the op).
#include <stdint.h>

#define VIVID_MAX_ACTIVE_NOTES 32
#define VIVID_NOTE_BUS_TRACKS  32

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int pitch; float velocity; } VividActiveNote;

// --- consumer side (a GPU op, render thread) ---
// Copy up to `max` of `track`'s currently-held notes into `out`. Returns the count copied (0 if the
// track has nothing held / is out of range). Cheap, lock-free snapshot.
uint32_t vivid_track_active_notes(int track, VividActiveNote* out, uint32_t max);

// --- host wiring (the app, UI/frame thread) ---
// Publish `count` held notes for `track` into the bus (called once per frame from the engine).
void     vivid_note_bus_publish(int track, const VividActiveNote* notes, uint32_t count);

#ifdef __cplusplus
}
#endif
