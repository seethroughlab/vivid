#pragma once
#include <cstdint>
#include <string>
#include "midi/midi_clip.h"   // ClipNote (clip editing API)

// Multi-track session façade over the extracted VST3 host (vst3_host_common.h is
// an anonymous-namespace header, so the work lives in vst3_host.cpp and main only
// talks to these C-style entry points).
//
// A Session = N tracks. Each track hosts one VST3 instrument and has one MIDI
// clip per scene. Clip launches are queued on the main thread and applied on the
// audio thread at the next bar boundary (Ableton-style). The audio thread mixes
// all tracks (per-track gain) into the master output.
namespace vivid::session {

struct Session;  // opaque

// Upper bound on live tracks. Bounds the audio-thread track-view reserve (so the
// gen-counter try_lock swap never allocates) and the per-track UI arrays in window.h.
constexpr int kMaxTracks = 32;

Session* session_create(uint32_t sample_rate);
void     session_destroy(Session*);

int  session_track_count(Session*);
int  session_scene_count(Session*);
const char* session_track_name(Session*, int track);
// Stable per-track id (monotonic; survives reorders/deletes). The audio->visual bridge
// keys mapping sources by this, not the positional index, so deleting a track never
// re-points another track's mappings. -1 if the index is out of range.
int  session_track_id(Session*, int track);
void session_set_track_id(Session*, int track, int id);   // load-time restore of a saved id

// Dynamic tracks: create/delete at runtime (UI/main thread). The audio thread sees the
// change at the next block via a generation-counter try_lock swap of its track view.
// add_* return the new track index, or -1 (catalog/path didn't resolve, or kMaxTracks).
int  session_add_instrument_track(Session*, const char* instrument);  // catalog label OR a .vst3 path
int  session_add_audio_track(Session*);                               // a sampler track
bool session_remove_track(Session*, int track);                       // retires the track (freed at shutdown)
// Instrument catalog offered in the "+ Track" menu (resolved to a plugin on add).
int         session_available_instrument_count();
const char* session_available_instrument_name(int i);

// Per-cell state (audio-thread truth).
int  session_active_clip(Session*, int track);   // active scene index, -1 if stopped
int  session_queued_clip(Session*, int track);   // -1 if nothing pending

// Launch (main thread, applied on the next bar).
void session_launch_clip(Session*, int track, int scene);
void session_launch_scene(Session*, int scene);   // launches scene on every track

// Mixer.
float session_track_gain(Session*, int track);
void  session_set_track_gain(Session*, int track, float gain);
float session_track_level(Session*, int track);      // per-track output RMS (meters)
float session_track_transient(Session*, int track);  // per-track onset detector (0..1)
float session_track_band(Session*, int track, int band);  // 0=low 1=mid 2=high energy

// Plugin editor (P10): the track's IEditController, as void* (cast in main).
void* session_track_controller(Session*, int track);

// Per-track effect chain (P22): instrument + N effects processed in series.
int         session_effect_count(Session*, int track);
const char* session_effect_name(Session*, int track, int effect);
void*       session_effect_controller(Session*, int track, int effect);  // IEditController*
// Thread-safe runtime add/remove (P23). bundle = a .vst3 path.
bool        session_add_effect(Session*, int track, const char* bundle);
void        session_remove_effect(Session*, int track, int effect);
// Catalog offered in the device-chain "+ FX" menu (resolved to a bundle on add).
int         session_available_effect_count();
const char* session_available_effect_name(int i);
bool        session_add_effect_by_index(Session*, int track, int i);

// Device parameters (P24). device: 0 = instrument, 1+ = effect index+1.
int         session_param_count(Session*, int track, int device);
const char* session_param_name(Session*, int track, int device, int i);
uint32_t    session_param_id(Session*, int track, int device, int i);
float       session_param_value(Session*, int track, int device, int i);  // normalized 0..1
void        session_set_param(Session*, int track, int device, uint32_t param_id, float value);

bool session_track_is_audio(Session*, int track);  // sampler track (no plugin / MIDI)
int  session_audio_clip_bpm(Session*, int track, int scene);  // source tempo, 0 if generated

// Plugin preset state (P18): "z:<base64>" of the plugin's getState(); empty for
// audio/handle-less tracks. set applies via setState. Call on the UI thread only.
std::string session_get_track_state(Session*, int track);
void        session_set_track_state(Session*, int track, const std::string& state);

// Audio waveform editing (P15). Waveform = peak amplitude per bin (read-only).
int  session_audio_waveform(Session*, int track, int scene, float* out_bins, int n_bins);
void session_get_audio_trim(Session*, int track, int scene, float* t0, float* t1);   // loop window [0,1]
void session_set_audio_trim(Session*, int track, int scene, float t0, float t1);

// MIDI clip editing (P14). The editor reads a snapshot, edits, and writes back;
// the audio thread applies the change at the top of the next process block.
int    session_clip_note_count(Session*, int track, int scene);
int    session_get_clip(Session*, int track, int scene, ClipNote* out, int max);  // returns count
double session_clip_length(Session*, int track, int scene);
void   session_set_clip(Session*, int track, int scene, const ClipNote* notes, int n, double length);

// Audio thread: render `frames` interleaved stereo into `out` (mix of all tracks).
// `playing` false = paused: instruments emit no new notes and the sampler is silent
// (release tails still ring). `release_all` (the play->stop edge) flushes every
// instrument's held notes as note-offs so nothing hangs when you pause.
bool session_process(Session*, float* out, uint32_t frames, uint32_t sample_rate,
                     double bpm, double beats, uint32_t beats_per_bar,
                     bool playing = true, bool release_all = false);

}  // namespace vivid::session
