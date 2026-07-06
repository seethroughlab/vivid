#pragma once
#include <cstdint>
#include <string>
#include "midi/midi_clip.h"   // ClipNote (clip editing API)

namespace vivid { class OpRegistry; }   // shared operator registry (native audio ops, AO-1)

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

// Live MIDI input / record-arm (M6). Arm a track (by index; stored as a stable id) to
// monitor live notes through its instrument. note_on/off feed the session live-input
// queue from any producer thread (musical typing, CoreMIDI) — lock-free on the audio side.
void session_set_armed_track(Session*, int track_index);   // -1 (or out-of-range) clears
int  session_armed_track(Session*);                        // armed track index, -1 if none
void session_note_on(Session*, int pitch, float vel);      // routed to the armed instrument track
void session_note_off(Session*, int pitch);
// Editor keyboard audition: play/stop a note on a specific track, independent of the arm.
void session_preview_note(Session*, int track, int pitch, float vel);
void session_preview_off(Session*, int track, int pitch);
// Recording: start (on=true) snaps the capture origin after an optional count-in; stop
// (on=false) overdubs the captured notes into the armed track's active clip.
void session_set_recording(Session*, bool on, double count_in_beats);
int  session_is_recording(Session*);
void session_set_metronome(Session*, int on);
int  session_get_metronome(Session*);

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
double session_audio_loop_beats(Session*, int track, int scene);   // clip's loop length (beats)
void session_get_audio_trim(Session*, int track, int scene, float* t0, float* t1);   // loop window [0,1]
void session_set_audio_trim(Session*, int track, int scene, float t0, float t1);

// Audio-clip warp/shaping (A2). set_warp inits the per-slot pitch-preserving stretcher on
// the calling (UI/main) thread; mode: 0=Complex, 1=Beats, 2=Repitch. Pitch in semitones.
void  session_set_audio_warp   (Session*, int track, int scene, int enabled, int mode);
int   session_get_audio_warp   (Session*, int track, int scene);   // -1 off, else the mode 0..2
void  session_set_audio_pitch  (Session*, int track, int scene, float semitones);
float session_get_audio_pitch  (Session*, int track, int scene);
void  session_set_audio_gain   (Session*, int track, int scene, float gain);
float session_get_audio_gain   (Session*, int track, int scene);
void  session_set_audio_reverse(Session*, int track, int scene, int on);
int   session_get_audio_reverse(Session*, int track, int scene);
void  session_set_audio_fades  (Session*, int track, int scene, float in_ms, float out_ms, float xfade_ms);
void  session_get_audio_fades  (Session*, int track, int scene, float* in_ms, float* out_ms, float* xfade_ms);

// Auto-warp: detect transients on the clip's PCM, place a warp marker per hit at the nearest
// beat (tempo from src_bpm or estimated), and enable Complex warp. Returns the marker count.
int   session_audio_auto_warp    (Session*, int track, int scene, float sensitivity);
// Marker / transient positions as normalized buffer fractions [0,1] (for the editor overlay).
int   session_audio_get_warp_pts (Session*, int track, int scene, float* out_norm, int cap);
int   session_audio_get_warp_beats(Session*, int track, int scene, double* out_beats, int cap);
int   session_audio_get_transients(Session*, int track, int scene, float* out_norm, int cap);
void  session_audio_clear_warp   (Session*, int track, int scene);   // drop markers, warp off
// Replace the warp markers from parallel (normalized-sample, beat) arrays (editor drag/add/delete).
void  session_audio_set_warp_pts (Session*, int track, int scene, const float* norm, const double* beats, int n);
// Slice boundaries (normalized 0..1) for slice mode: 1=transients, 2=manual, 3=16-grid.
int   session_audio_slices       (Session*, int track, int scene, int mode, float* out_norm, int cap);

// MIDI clip editing (P14). The editor reads a snapshot, edits, and writes back;
// the audio thread applies the change at the top of the next process block.
// Clip pool: loose clips stashed outside the track grid (browser sidebar). UI/main-thread
// only — the audio thread never touches the pool. Fully portable MidiClips (notes + length).
int         session_pool_count(Session*);
int         session_pool_get(Session*, int index, ClipNote* out, int max);   // returns count
double      session_pool_length(Session*, int index);
const char* session_pool_name(Session*, int index);
int         session_pool_add(Session*, const ClipNote* notes, int n, double length, const char* name);  // -> index
void        session_pool_remove(Session*, int index);
void        session_pool_clear(Session*);
// Audio clips in the pool (Samplers). A pool entry is either MIDI or audio (is_audio).
bool        session_pool_is_audio(Session*, int index);
int         session_pool_audio_bpm(Session*, int index);
int         session_pool_audio_waveform(Session*, int index, float* out_bins, int n_bins);
int         session_pool_stash_audio(Session*, int track, int scene, const char* name);  // MOVE grid->pool, -> index
bool        session_pool_place_audio(Session*, int index, int track, int scene);         // copy pool->grid

int    session_clip_note_count(Session*, int track, int scene);
int    session_get_clip(Session*, int track, int scene, ClipNote* out, int max);  // returns count
double session_clip_length(Session*, int track, int scene);
void   session_set_clip(Session*, int track, int scene, const ClipNote* notes, int n, double length);
// In-clip loop region (beats). loop_end <= loop_start disables it (loop the whole clip).
void   session_set_clip_loop(Session*, int track, int scene, double loop_start, double loop_end);
void   session_get_clip_loop(Session*, int track, int scene, double* loop_start, double* loop_end);

// Native audio operators (AO-1). The shared registry is set once at init; a track can have
// a native instrument (source op) + a chain of native audio effects, alongside VST3.
// index -1 addresses the instrument slot; index >= 0 an effect in the chain.
void        session_set_op_registry(Session*, vivid::OpRegistry* reg);
int         session_add_audio_effect(Session*, int track, const char* op_type);   // -> effect index, -1 on failure
void        session_remove_audio_effect(Session*, int track, int index);
int         session_audio_effect_count(Session*, int track);
const char* session_audio_op_type(Session*, int track, int index);
int         session_set_track_audio_instrument(Session*, int track, const char* op_type);  // "" clears; 1 on success
int         session_audio_op_param_count(Session*, int track, int index);
const char* session_audio_op_param_name(Session*, int track, int index, int param);
float       session_audio_op_param_get(Session*, int track, int index, int param);
float       session_audio_op_param_min(Session*, int track, int index, int param);   // Param<> range (UI normalization)
float       session_audio_op_param_max(Session*, int track, int index, int param);
void        session_audio_op_param_set(Session*, int track, int index, int param, float value);

// Native audio-operator catalog for the device-chain pickers. want_source: 1 =
// instruments/generators (no audio input), 0 = effects (has audio input). Names are
// stable registry keys usable with session_add_audio_effect / _set_track_audio_instrument.
int         session_available_audio_op_count(Session*, int want_source);
const char* session_available_audio_op_name(Session*, int want_source, int idx);

// Audio thread: render `frames` interleaved stereo into `out` (mix of all tracks).
// `playing` false = paused: instruments emit no new notes and the sampler is silent
// (release tails still ring). `release_all` (the play->stop edge) flushes every
// instrument's held notes as note-offs so nothing hangs when you pause.
bool session_process(Session*, float* out, uint32_t frames, uint32_t sample_rate,
                     double bpm, double beats, uint32_t beats_per_bar,
                     bool playing = true, bool release_all = false);

}  // namespace vivid::session
