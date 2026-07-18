#pragma once
#include <cstdint>
#include <string>
#include "midi/midi_clip.h"   // ClipNote (clip editing API)

namespace vivid { class OpRegistry; }   // shared operator registry (native audio ops)

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

// Upper bound on scenes (grid rows). Per-track clip vectors are reserved to this cap at
// construction so appending a scene never reallocates — the audio thread holds a raw
// pointer into each track's clip vector (Track::sched), so growth must be append-only
// within reserved capacity. Also the fixed size of the per-scene audio-trim arrays.
constexpr int kMaxScenes = 8;

// Optional startup load-progress hook: session_create blocks while it scans + loads the
// default project's VST3 instruments (seconds). Set this before session_create to drive a
// splash frame after each load phase; `status` is a short human label. Cleared by passing
// nullptr. Runs on the calling (main) thread. Off by default.
using SessionLoadCb = void(*)(void* user, const char* status);
void     session_set_load_progress(SessionLoadCb cb, void* user);

Session* session_create(uint32_t sample_rate);
void     session_destroy(Session*);

int  session_track_count(Session*);
int  session_scene_count(Session*);
// Append a scene (grid row): grows every track's clip vector by one empty clip. Returns the
// new scene index, or -1 if already at kMaxScenes. UI/main thread only (append is RT-safe
// because clip vectors are reserved to kMaxScenes, so no reallocation occurs).
int  session_add_scene(Session*);
// Load-time only: set the scene count (clamped to [1, kMaxScenes]) BEFORE tracks are
// recreated, so each track is born with the right number of clip slots. Does not touch
// existing tracks.
void session_set_scene_count(Session*, int scenes);
const char* session_track_name(Session*, int track);
// Stable per-track id (monotonic; survives reorders/deletes). The audio->visual bridge
// keys mapping sources by this, not the positional index, so deleting a track never
// re-points another track's mappings. -1 if the index is out of range.
int  session_track_id(Session*, int track);
void session_set_track_id(Session*, int track, int id);   // load-time restore of a saved id

// Live MIDI input / record-arm. Arm a track (by index; stored as a stable id) to
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
int  session_add_graph_track(Session*, const char* name);            // bare native track for a loaded audio graph
bool session_remove_track(Session*, int track);                       // retires the track (freed at shutdown)
// Instrument catalog offered in the "+ Track" menu (resolved to a plugin on add).
// (The hard-coded 5-instrument / 5-effect menus are gone: names now resolve against the WHOLE
// installed catalog — audio/plugin_catalog.h. Adding is the Tab chooser; this is only for loading
// an OLD project, whose effects were saved by name rather than by path.)
bool        session_add_effect_by_name(Session*, int track, const char* name);

// Per-cell state (audio-thread truth).
int  session_active_clip(Session*, int track);   // active scene index, -1 if stopped
int  session_queued_clip(Session*, int track);   // -1 if nothing pending

// Launch (main thread, applied on the next bar).
void session_launch_clip(Session*, int track, int scene);
void session_launch_scene(Session*, int scene);   // launches scene on every track

// Mixer.
float session_track_gain(Session*, int track);
void  session_set_track_gain(Session*, int track, float gain);
// ADR-0022 P1b.4: solo/mute (mix state). A track is silenced in the master sum if muted, or if any
// track is soloed and it is not; its own meter stays pre-mute.
bool  session_track_mute(Session*, int track);
void  session_set_track_mute(Session*, int track, bool mute);
bool  session_track_solo(Session*, int track);
void  session_set_track_solo(Session*, int track, bool solo);
// ADR-0022 P2a.2: cross-track control edges (a modulator on one track drives a param on another).
// Tracks are indices; nodes are stable agraph node ids. connect returns 1 on success, 0 on bad
// track/node or a duplicate (src,dst,param).
int   session_connect_control(Session*, int src_track, int src_node, int dst_track, int dst_node,
                              int param, float amount, float curve, int invert, int bipolar);
void  session_disconnect_control(Session*, int src_track, int src_node, int dst_track, int dst_node, int param);
float session_track_level(Session*, int track);      // per-track output RMS (meters)
float session_track_transient(Session*, int track);  // per-track onset detector (0..1)
float session_track_band(Session*, int track, int band);  // 0=low 1=mid 2=high energy

// ADR-0022 P1b: the master node — the session's single sink (sums the track outputs).
float session_master_gain(Session*);
void  session_set_master_gain(Session*, float gain);
float session_master_level(Session*);               // master output RMS (meters)
float session_master_transient(Session*);           // master onset detector (0..1)
float session_master_band(Session*, int band);      // 0=low 1=mid 2=high energy

// Plugin editor: the track's IEditController, as void* (cast in main).
void* session_track_controller(Session*, int track);

// Per-track effect chain: instrument + N effects processed in series.
int         session_effect_count(Session*, int track);
const char* session_effect_name(Session*, int track, int effect);
void*       session_effect_controller(Session*, int track, int effect);  // IEditController*
// Thread-safe runtime add/remove. bundle = a .vst3 path.
bool        session_add_effect(Session*, int track, const char* bundle);
void        session_remove_effect(Session*, int track, int effect);
// Catalog offered in the device-chain "+ FX" menu (resolved to a bundle on add).

// Device parameters. device: 0 = instrument, 1+ = effect index+1.
int         session_param_count(Session*, int track, int device);
const char* session_param_name(Session*, int track, int device, int i);
uint32_t    session_param_id(Session*, int track, int device, int i);
float       session_param_value(Session*, int track, int device, int i);  // normalized 0..1
void        session_set_param(Session*, int track, int device, uint32_t param_id, float value);

bool session_track_is_audio(Session*, int track);  // sampler track (no plugin / MIDI)
int  session_audio_clip_bpm(Session*, int track, int scene);  // source tempo, 0 if generated

// Plugin preset state: "z:<base64>" of the plugin's getState(); empty for
// audio/handle-less tracks. set applies via setState. Call on the UI thread only.
std::string session_get_track_state(Session*, int track);
void        session_set_track_state(Session*, int track, const std::string& state);

// Audio waveform editing. Waveform = peak amplitude per bin (read-only).
int  session_audio_waveform(Session*, int track, int scene, float* out_bins, int n_bins);
double session_audio_loop_beats(Session*, int track, int scene);   // clip's loop length (beats)
void session_get_audio_trim(Session*, int track, int scene, float* t0, float* t1);   // loop window [0,1]
void session_set_audio_trim(Session*, int track, int scene, float t0, float t1);

// Audio-clip warp/shaping. set_warp inits the per-slot pitch-preserving stretcher on
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
// Persistence of the loop's source WAV (empty path = generated loop). save writes path/src_bpm; load
// reloads the WAV into the scene (decode on the UI thread, RT-safe clip swap).
const char* session_get_audio_path   (Session*, int track, int scene);
double      session_get_audio_src_bpm(Session*, int track, int scene);
bool        session_load_audio_clip  (Session*, int track, int scene, const char* path, double src_bpm);
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

// MIDI clip editing. The editor reads a snapshot, edits, and writes back;
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

// Native audio operators. The shared registry is set once at init; a track can have
// a native instrument (source op) + a chain of native audio effects, alongside VST3.
// index -1 addresses the instrument slot; index >= 0 an effect in the chain.
void        session_set_op_registry(Session*, vivid::OpRegistry* reg);
// Append two node-graph showcase tracks (a frequency-split FX rack + a key-range instrument split),
// built with native ops. Call once after session_set_op_registry; no-op without a registry.
void        session_build_split_showcase(Session*);
int         session_add_audio_effect(Session*, int track, const char* op_type);   // -> effect index, -1 on failure
void        session_remove_audio_effect(Session*, int track, int index);
int         session_audio_effect_count(Session*, int track);
const char* session_audio_op_type(Session*, int track, int index);
int         session_set_track_audio_instrument(Session*, int track, const char* op_type);  // "" clears; 1 on success
// CLAP plugin hosting: assign a `.clap` bundle as a track's instrument source ("" clears;
// instruments must accept note input) or append one as an effect. Loading is ASYNC — the slow
// plugin ctor runs on a background loader thread so it never blocks the main thread /
// control-server drain. They return immediately (1 = queued; "" instrument path clears inline).
// session_poll_plugin_loads() applies finished loads on the main thread (call it once per frame);
// pending()>0 means loads are in flight; last_plugin_load_error() reports failures.
int         session_request_track_clap_instrument(Session*, int track, const char* clap_path);
int         session_request_track_clap_effect(Session*, int track, const char* clap_path);
// State-carrying variants (persist): restore the saved patch `state` once the async load finishes,
// so load_project never blocks on a slow plugin ctor. Empty state = no restore.
int         session_request_track_clap_instrument_state(Session*, int track, const char* clap_path, const char* state);
int         session_request_track_clap_effect_state(Session*, int track, const char* clap_path, const char* state);
void        session_poll_plugin_loads(Session*);
int         session_plugin_loads_pending(Session*);
const char* session_last_plugin_load_error(Session*);
// CLAP identity + state for persistence ("" / empty when the track has no CLAP plugin there).
const char* session_track_clap_instrument_path(Session*, int track);
int         session_track_clap_effect_count(Session*, int track);
const char* session_track_clap_effect_path(Session*, int track, int index);
std::string session_get_track_clap_effect_state(Session*, int track, int index);   // save side (load restores via the async request)
// Generic instrument preset browse/load (no per-plugin code). scan fills the cache + returns the
// count; name/id read it by index; load applies a preset by its id. CLAP (preset-discovery +
// preset-load exts) and VST3 (`.vstpreset` files in the plugin's preset dirs) both supported.
int         session_track_preset_scan(Session*, int track, const char* filter);   // filter = "" for all
int         session_track_preset_count(Session*, int track);
const char* session_track_preset_name(Session*, int track, int index);
const char* session_track_preset_id(Session*, int track, int index);
// Discovery metadata for a scanned preset (empty/0 when absent): bank/folder/type, whether it can
// be loaded on this host (some native formats are browse-only), and taxonomy tags.
const char* session_track_preset_category(Session*, int track, int index);
int         session_track_preset_loadable(Session*, int track, int index);
int         session_track_preset_tag_count(Session*, int track, int index);
const char* session_track_preset_tag(Session*, int track, int index, int tag);
bool        session_track_preset_load(Session*, int track, const char* id);
int         session_audio_op_param_count(Session*, int track, int index);
const char* session_audio_op_param_name(Session*, int track, int index, int param);
int         session_audio_op_param_hint(Session*, int track, int index, int param);   // VividDisplayHint (0 = DEFAULT)
float       session_audio_op_param_get(Session*, int track, int index, int param);
float       session_audio_op_param_min(Session*, int track, int index, int param);   // Param<> range (UI normalization)
float       session_audio_op_param_max(Session*, int track, int index, int param);
void        session_audio_op_param_set(Session*, int track, int index, int param, float value);

// Native audio-operator catalog for the device-chain pickers. want_source: 1 =
// instruments/generators (no audio input), 0 = effects (has audio input). Names are
// stable registry keys usable with session_add_audio_effect / _set_track_audio_instrument.
int         session_available_note_op_count(Session*);              // ADR-0015: native note effects
const char* session_available_note_op_name(Session*, int idx);
int         session_available_mod_op_count(Session*);               // ADR-0022: native modulators (LFO)
const char* session_available_mod_op_name(Session*, int idx);
int         session_available_audio_op_count(Session*, int want_source);
const char* session_available_audio_op_name(Session*, int want_source, int idx);

// Read-only introspection of a track's authoritative audio graph (the persistent
// per-track topology model behind the compiled RT plan). Reports what the executor actually
// runs. `graph_ok` is 1 only when the track is on the native audio-graph path (a native
// instrument + native FX, no VST3); 0 => the track runs the inline/VST3 chain and the graph
// is empty. Node kind: 0 = instrument (source), 1 = effect, 2 = output (sink). Node ids are
// stable across rebuilds; edges are (from_id -> to_id) node-id pairs. All accessors are
// bounds-checked (safe defaults on a bad index) and read under the track's graph lock.
int         session_track_audio_graph_ok(Session*, int track);
int         session_track_audio_graph_node_count(Session*, int track);
int         session_track_audio_graph_node_id(Session*, int track, int i);
int         session_track_audio_graph_node_kind(Session*, int track, int i);       // 0 inst / 1 fx / 2 output
const char* session_track_audio_graph_node_type(Session*, int track, int i);       // bound op's registry name ("" for output)
int         session_track_audio_graph_node_plugin_kind(Session*, int track, int i);// binding family: 0 native/1 vst3/2 clap/3 sampler
int         session_track_audio_graph_output_id(Session*, int track);
// Node i's live output-waveform scope (oldest→newest) → out[n]; returns samples written. Display-only.
int         session_track_audio_graph_node_scope(Session*, int track, int i, float* out, int n);
// The VST3 IEditController behind a graph node (by node id), for opening its plugin editor; null if
// the node is native/sampler/output. Returns void* (cast to IEditController* at the call site).
void*       session_audio_graph_node_controller(Session*, int track, int node_id);
int         session_track_audio_graph_edge_count(Session*, int track);
void        session_track_audio_graph_node_note_ports(Session*, int track, int i, int* note_in, int* note_out);
int         session_track_audio_graph_edge_kind(Session*, int track, int e);   // 0 audio / 1 note / 2 control
int         session_track_audio_graph_edge_from(Session*, int track, int e);
int         session_track_audio_graph_edge_to(Session*, int track, int e);
// ADR-0022: a control edge's target param + shaper (for the UI arc + MCP round-trip). dest_param
// returns -1 for a non-control edge; control_shape returns 1 on a control edge (out-ptrs may be null).
int         session_track_audio_graph_edge_dest_param(Session*, int track, int e);
int         session_track_audio_graph_edge_control_shape(Session*, int track, int e,
                                                         float* amount, float* curve, int* invert, int* bipolar);

// AG-1 step 2 — authoritative topology edits (UI thread). The first such edit flips the track
// to graph-authoritative: rebuild_track_graph stops regenerating from the linear device chain
// and the graph itself becomes the source of truth. Each edit republishes to the audio thread.
int         session_audio_graph_add_op(Session*, int track, const char* op_type);   // -> new node id, -1 fail
int         session_audio_graph_add_source(Session*, int track, const char* op_type);   // instrument source node, fan-in to Output
// A2: add a VST3/CLAP plugin as a graph NODE (the peer of add_op/add_source, which are native-only).
// `format` is a PluginFormat (audio/plugin_catalog.h); `is_source` = instrument (fans in to Output)
// vs effect (splices before Output) — take it from the plugin's CLASS, never from its port counts (a
// CLAP synth may declare audio inputs). `uid` = a VST3 class cid hex, "" to let the loader pick.
// Returns the new node id IMMEDIATELY; a CLAP's handle binds when its async load lands (the node is
// audibly a no-op until then, which is already RT-safe).
int         session_audio_graph_add_plugin(Session*, int track, const char* path, int format,
                                           int is_source, const char* uid);
int         session_audio_graph_node_plugin_ready(Session*, int track, int node_id);   // 1 bound / 0 loading / -1 not a plugin node
// 1 if this node's plugin failed to load — a TERMINAL failure (slot present, not pending, no handle
// bound), distinct from "still loading" (plugin_ready==0). The node is silent and won't recover
// without a reload. ADR-0019: surface it; do NOT badge a still-loading plugin (that would lie).
int         session_audio_graph_node_plugin_failed(Session*, int track, int node_id);
const char* session_audio_graph_node_plugin_path(Session*, int track, int node_id);    // "" if not a plugin node
const char* session_audio_graph_node_plugin_uid(Session*, int track, int node_id);     // VST3 class cid hex ("" if none)
// A plugin node's patch (base64) — so a user-spawned plugin keeps its sound across save + load.
std::string session_audio_graph_node_get_state(Session*, int track, int node_id);
void        session_audio_graph_node_set_state(Session*, int track, int node_id, const std::string& state);
// Load-time twin of add_plugin: recreate a persisted plugin node with its SAVED id and no auto-wiring
// (the edges come from the file). `state` is applied once the plugin is bound (CLAP binds async).
int         session_audio_graph_load_plugin_node(Session*, int track, int node_id, const char* path,
                                                 int format, int is_source, const char* uid,
                                                 const char* state);
int         session_audio_graph_remove_node(Session*, int track, int node_id);      // 1 ok / 0 fail (effects only)
int         session_audio_graph_connect(Session*, int track, int from_id, int to_id);   // 1 ok / 0 (dup/cycle/bad)
// ADR-0015: notes are a signal too. kind: 0 = audio (sums at the destination), 1 = note (merges).
int         session_audio_graph_connect_kind(Session*, int track, int from_id, int to_id, int kind);
// The track's note stream as an explicit NODE (clips + live MIDI + typing + MCP + preview). Wire
// its note edge into an instrument (or a note effect) to route notes.
int         session_audio_graph_add_midi_in(Session*, int track);
// A native NOTE EFFECT (e.g. "Arp"): notes in -> notes out, no audio. Wire it with note edges.
int         session_audio_graph_add_note_op(Session*, int track, const char* op_type);
// ADR-0022: a native MODULATOR (e.g. "LFO"): no audio, emits a 0..1 control signal. Wire its output
// to a param with connect_control below.
int         session_audio_graph_add_mod_op(Session*, int track, const char* op_type);
// Wire a modulator -> ONE param of `to_id`, shaped by amount (fraction of the param's range) /
// curve (-1..+1) / invert / bipolar (straddle the base vs. run up from it). 1 ok / 0 (dup param /
// self-loop / bad id / cycle). disconnect by the same (from,to,param) triple.
int         session_audio_graph_connect_control(Session*, int track, int from_id, int to_id,
                                                int dest_param, float amount, float curve,
                                                int invert, int bipolar);
int         session_audio_graph_disconnect_control(Session*, int track, int from_id, int to_id, int dest_param);
// ADR-0022: re-shape an existing control edge in place (amount/curve/invert/bipolar). 1 ok / 0 if absent.
int         session_audio_graph_set_control_shape(Session*, int track, int from_id, int to_id, int dest_param,
                                                  float amount, float curve, int invert, int bipolar);
int         session_audio_graph_disconnect(Session*, int track, int from_id, int to_id);// 1 ok
// A source node's MIDI key range [lo,hi] (0..127 = full). Two sources with disjoint ranges = a
// key-split; the audio thread hands each source only its in-range notes. get returns 1 on success.
void        session_audio_graph_node_key_range_set(Session*, int track, int node_id, int lo, int hi);
int         session_audio_graph_node_key_range_get(Session*, int track, int node_id, int* lo, int* hi);
// Node-id-keyed param access (works for derived + authoritative graphs; the chain-index API
// can't address nodes in a non-linear graph). node_id comes from the introspection accessors.
int         session_audio_graph_node_param_count(Session*, int track, int node_id);
const char* session_audio_graph_node_param_name (Session*, int track, int node_id, int p);
int         session_audio_graph_node_param_hint (Session*, int track, int node_id, int p);   // VividDisplayHint (0 = DEFAULT)
float       session_audio_graph_node_param_get  (Session*, int track, int node_id, int p);   // BASE (ADR-0022)
float       session_audio_graph_node_param_min  (Session*, int track, int node_id, int p);
float       session_audio_graph_node_param_max  (Session*, int track, int node_id, int p);
void        session_audio_graph_node_param_set  (Session*, int track, int node_id, int p, float v);
// ADR-0022: a param has a BASE (the user's value, above) and a RESOLVED value (base + live
// modulation from control edges). `_resolved` is what the DSP is actually using this instant — the
// UI's live dot, MCP's "value"; `_wired` is 1 iff a control edge drives it (the modulated ring).
float       session_audio_graph_node_param_resolved (Session*, int track, int node_id, int p);
int         session_audio_graph_node_param_wired    (Session*, int track, int node_id, int p);
// Richer param metadata for widget-by-type + a curated inspector (param-panel redesign). Complete
// for VST3 (derived from the plugin's step count + the controller's own value formatter); native
// ops and CLAP fall back (type=FLOAT / no choices / numeric display) as they lack this today.
int         session_audio_graph_node_param_type        (Session*, int track, int node_id, int p);   // VIVID_PARAM_* : 0 FLOAT, 1 INT/enum, 2 BOOL
int         session_audio_graph_node_param_choice_count (Session*, int track, int node_id, int p);   // >0 for a discrete/enum param (the number of named values)
const char* session_audio_graph_node_param_choice_label (Session*, int track, int node_id, int p, int choice);  // plugin's label for choice; "" otherwise
const char* session_audio_graph_node_param_display      (Session*, int track, int node_id, int p);   // plugin-formatted current value + units ("1.2 kHz", "On", "Lowpass")
int         session_audio_graph_node_is_plugin          (Session*, int track, int node_id);   // 1 if the node is a VST3 or CLAP plugin (→ the curated inspector)
// Curated inspector (pure curation): the UI-chosen subset of params to surface for a node, by param
// index, in add order. UI thread; persisted with the session. pin is idempotent.
void        session_audio_graph_node_param_pin          (Session*, int track, int node_id, int p);
void        session_audio_graph_node_param_unpin        (Session*, int track, int node_id, int p);
int         session_audio_graph_node_param_is_pinned    (Session*, int track, int node_id, int p);   // 1 if pinned
int         session_audio_graph_node_param_pinned_count  (Session*, int track, int node_id);
int         session_audio_graph_node_param_pinned_at     (Session*, int track, int node_id, int i);   // the i-th pinned param index, or -1
// Editor node position (UI thread; persisted). set by stable node id (drag/load); get by node index
// (save/introspection) → 1 if the node has a stored position, else 0 (editor auto-lays it out).
void        session_audio_graph_node_set_pos(Session*, int track, int node_id, float x, float y);
int         session_track_audio_graph_node_pos(Session*, int track, int i, float* x, float* y);
// 1 if the track's audio graph is the authoritative source of topology (has been rewired) → its
// graph should be persisted as nodes+edges rather than the linear instrument/fx chain.
int         session_track_audio_graph_authoritative(Session*, int track);
// Graph load (persistence). Host assigns fresh node ids (remap saved ids). Sequence:
// clear -> load_node* (+ set node params) -> load_edge* -> finish_load(output). kind: 0 inst / 1 fx / 2 output.
// plugin_kind (0 native/1 vst3/2 clap/3 sampler) selects the placeholder for a non-native source/fx node:
// the VST3/CLAP handle is bound after the (async) plugin load lands (rebind on rebuild/finish_load).
void        session_audio_graph_clear      (Session*, int track);
int         session_audio_graph_load_node  (Session*, int track, int kind, int plugin_kind, const char* op_type);   // -> new node id
void        session_audio_graph_load_edge  (Session*, int track, int from_id, int to_id);
void        session_audio_graph_load_edge_kind(Session*, int track, int from_id, int to_id, int kind);  // 0 audio / 1 note
// ADR-0022: load a control edge (target param + shaper). finish_load compiles + publishes.
void        session_audio_graph_load_edge_control(Session*, int track, int from_id, int to_id,
                                                  int dest_param, float amount, float curve, int invert, int bipolar);
void        session_audio_graph_finish_load(Session*, int track, int output_id);

// Slice the source audio clip into a new MIDI track driven by a native Sampler loaded
// with the clip's PCM + slices (slice_mode: 1=transients, 3=16-grid). Ascending pitches from
// C1 map to slices. Returns the new track index, or -1 on failure. UI thread only.
int         session_slice_to_midi(Session*, int src_track, int src_scene, int slice_mode);

// Audio thread: render `frames` interleaved stereo into `out` (mix of all tracks).
// `playing` false = paused: instruments emit no new notes and the sampler is silent
// (release tails still ring). `release_all` (the play->stop edge) flushes every
// instrument's held notes as note-offs so nothing hangs when you pause.
bool session_process(Session*, float* out, uint32_t frames, uint32_t sample_rate,
                     double bpm, double beats, uint32_t beats_per_bar,
                     bool playing = true, bool release_all = false);

}  // namespace vivid::session
