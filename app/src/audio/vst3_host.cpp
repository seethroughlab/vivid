// Multi-track session — N tracks, each a hosted VST3 instrument + per-scene MIDI
// clips, mixed (per-track gain) to the master output with bar-quantized launch.
// Built on classic's extracted host (vst3_host_common.h, anonymous namespace).
#include "vst3_host_common.h"
#include "vst3_host.h"
#include "midi/midi_clip.h"
#include "audio/sampler.h"
#include "audio/clip_dsp.h"                           // A2: per-clip warp stretcher (ClipDsp + process_clip)
#include "audio/audio_op_runtime.h"                   // AO-1: native audio operators (opaque; no operator_api leak)
#include "audio/audio_graph.h"                        // AG-0: per-track audio signal graph (ADR-0012)
#include "audio/clap_host.h"                           // CLAP plugin hosting (ClapHandle, clap_run, clap_load_plugin)
#include "audio/plugin_catalog.h"                     // A2: PluginFormat (kFmtVST3 / kFmtCLAP)
#include "audio/vst3_presets.h"                         // VST3 preset discovery/load (.vstpreset + Serum/Pigments adapters)
#include "pluginterfaces/vst/ivstnoteexpression.h"   // kTuningTypeID / kBrightnessTypeID

#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <deque>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <utility>
#include <filesystem>
#include <dirent.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vivid::session {

// Live MIDI input (M6): a lock-free ring of note on/off transitions from the UI/hardware
// producers (musical typing on the main thread, CoreMIDI on its own thread) to the audio
// thread, which drains it into a track's instrument each block (armed monitoring via the
// session queue, or editor keyboard-audition via a per-track queue). Producers serialize
// on `push_mtx` (rarely contended); the audio consumer (`pop`) never locks. `t_beats`
// stamps the transport position so the recorder (M6.3) can place captured notes.
struct LiveMidi {
    struct Ev { uint8_t on; uint8_t pitch; float vel; double t_beats; };
    static constexpr int kCap = 512;
    Ev                    ring[kCap];
    std::atomic<uint32_t> head{0};   // producer index
    std::atomic<uint32_t> tail{0};   // consumer index
    std::mutex            push_mtx;  // serializes producers; the audio consumer is lock-free

    void push(uint8_t on, uint8_t pitch, float vel, double t_beats) {
        std::lock_guard<std::mutex> lk(push_mtx);
        const uint32_t h = head.load(std::memory_order_relaxed);
        const uint32_t n = (h + 1) % kCap;
        if (n == tail.load(std::memory_order_acquire)) return;  // full — drop (never blocks audio)
        ring[h] = { on, pitch, vel, t_beats };
        head.store(n, std::memory_order_release);
    }
    // Audio thread: pop one event; returns false when drained. Never locks.
    bool pop(Ev& out) {
        const uint32_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) return false;
        out = ring[t];
        tail.store((t + 1) % kCap, std::memory_order_release);
        return true;
    }
};

// --- AG-0: per-track audio graph runtime (ADR-0012) --------------------------------------
// The graph executor currently runs pure-native tracks (a native instrument + native FX, no
// VST3): each node is dispatched by kind, reading a per-block context. VST3 and sampler-loop
// tracks stay on the inline path (gok=false). The edit-mirror (ggen/gmtx) publishes a
// UI-compiled plan (topology) + POD node bindings that the audio thread copies into
// reserved working buffers — no RT alloc/free. A node's index == its out_buf (see
// AudioGraph::compile), so bindings are addressed by out_buf.
constexpr int      kGraphMaxNodes = 64;
constexpr uint32_t kGraphMaxBlock = 4096;
// ADR-0015: capacity of ONE note buffer. Matches audio_op_runtime's kMaxNotes — a block that
// somehow carried more notes than this would be truncated rather than allocate on the RT thread.
constexpr size_t   kGraphMaxNotes = 512;
constexpr int      kScopeN        = 128;   // per-node output-waveform ring length (UI preview)
constexpr int      kScopePerBlock = 8;     // decimated samples pushed into the ring each block

// MidiIn (ADR-0015): the track's note stream as an explicit NODE — clips + live MIDI + musical
// typing + MCP + preview, i.e. exactly what fills t.nev. It emits notes on a note edge instead of
// the old invisible per-track broadcast.
enum class GNKind : uint8_t { NativeInst, NativeFx, Vst3Inst, Vst3Fx, ClapInst, ClapFx, Sampler, Output, MidiIn, NativeNoteFx, NativeMod };
// POD; trivially copyable (required for the try_lock swap of gbinds). `op` = native inst/fx;
// `handle` = VST3 inst/fx; `clap` = CLAP inst/fx (all raw, non-owning — Track owns them).
// Sampler carries no pointer (its active clip is scene-dependent, re-read from Track& at process).
// `key_lo`/`key_hi` = the MIDI key range a *source* node voices (0..127 = full range = no
// filtering). Two source nodes with disjoint ranges = a key-split; the audio thread filters the
// shared note stream per source (run_track_graph). Ignored for effect/output nodes.
struct GNodeBind {
    GNKind kind = GNKind::Output;
    vivid::AudioOp* op = nullptr;
    Vst3Handle* handle = nullptr;
    ClapHandle* clap = nullptr;
    uint8_t key_lo = 0, key_hi = 127;
    // A2: which per-track PluginSlot owns this node's plugin handle (-1 = none / a native op / a
    // legacy chain-derived node). This is what lets a plugin node live ANYWHERE in the graph: the
    // old rebind re-attached handles to nodes by kind+ORDER, which only works while the graph is a
    // faithful copy of the linear chain. A user-spawned plugin node breaks that coupling.
    int32_t pslot = -1;
};
// The audio thread swaps gbinds under a try_lock as a plain copy (reserved capacity, no move) —
// that is only RT-safe while the bind is trivially copyable. Adding a non-POD member here would
// silently break the real-time contract, so pin it.
static_assert(std::is_trivially_copyable<GNodeBind>::value, "GNodeBind must stay POD for the RT gbinds swap");

// Per-block audio-thread context shared by a track's node processors this block.
struct GraphBlockCtx {
    uint32_t frames = 0, sample_rate = 48000;
    float    bpm = 120.f; uint32_t bpb = 4; double beats = 0.0;
    const NoteEvent* notes = nullptr; uint32_t note_count = 0;
    // AG-0: extra per-block context the VST3/sampler node dispatch consumes (native ops ignore
    // these). `steady` is the track's running sample counter (also on Track); `delta` is
    // beats-per-block; `playing` gates the sampler. Filled alongside the native fields each block.
    uint64_t steady = 0; double delta = 0.0; bool playing = false;
    // ADR-0022 P2a.1: the session control pool + this track's region base (in floats). The apply
    // reads ctl_pool[ctl_base + src_buf·kGraphMaxBlock]; a modulator writes ctl_pool[ctl_base +
    // control_out_buf·kGraphMaxBlock]. Set per track in session_process (== tracks_view index · stride).
    float* ctl_pool = nullptr; size_t ctl_base = 0;
};

struct Track {
    Vst3Handle*           handle = nullptr;
    std::string           name;
    int                   id = -1;   // stable identity (monotonic; survives reorders/deletes)
    std::vector<MidiClip> clips;          // one per scene
    ClipScheduler         sched;
    std::atomic<int>      active{0};
    std::atomic<int>      queued{-1};
    std::atomic<float>    gain{0.8f};
    // ADR-0022 P1b.4: solo/mute. `mute`/`solo` are the UI-set state; `mix_scale` is the derived
    // per-track master-sum multiplier (1 = audible, 0 = silenced by mute or by another track's solo),
    // recomputed on the UI thread whenever any track's mute/solo changes and read by the audio thread
    // in the master sum. Meters stay PRE-mute — a muted track still shows its own level.
    std::atomic<bool>     mute{false}, solo{false};
    std::atomic<float>    mix_scale{1.f};
    std::atomic<float>    level{0.f};
    std::atomic<float>    transient{0.f};
    float                 tr_baseline = 0.f;  // onset detector baseline (audio thread)
    std::atomic<float>    band_low{0.f}, band_mid{0.f}, band_high{0.f};  // 3-band energy
    float                 flt_lo = 0.f, flt_hi = 0.f;  // one-pole crossover states
    std::vector<NoteEvent> nev;
    std::vector<NoteEvent> scene_rel;      // scene-switch note-offs for the CLAP path (VST3 gets them via vev)
    std::vector<ExprEvent> eev;            // per-note expression scratch (M3), pre-reserved
    Vst3EventList          vev;            // VST3 event list for this block (scene-switch releases +
                                           // notes); on the Track so both the inline path AND the
                                           // audio-graph Vst3Inst node dispatch share the same list.
    // Key-range routing scratch (a key-split track has >1 source, each voicing one pitch range).
    // Source nodes run sequentially in run_track_graph, so ONE filtered buffer per track is reused
    // across sources (like t.vev). Reserved off the audio thread; src_vev is fixed-capacity (256).
    std::vector<NoteEvent> src_nev;
    std::vector<ExprEvent> src_eev;
    Vst3EventList          src_vev;
    LiveMidi              preview_in;       // editor keyboard-audition notes (drained every block, any arm state)
    uint64_t              steady = 0;
    std::vector<Vst3Handle*> effects;      // post-generator FX chain (audio working copy)
    std::vector<float>    fxl, fxr;         // effect I/O scratch
    // Thread-safe FX edits (mirror of the clip-edit pattern): the UI mutates
    // effects_edit; the audio thread copies it into `effects` when fx_gen bumps.
    std::mutex               fx_mtx;
    std::vector<Vst3Handle*> effects_edit;
    std::atomic<uint64_t>    fx_gen{0};
    uint64_t                 fx_gen_seen = 0;
    std::vector<Vst3Handle*> fx_retired;   // removed handles, freed at shutdown (no audio free)
    // Native audio operators (AO-1): a source (instrument/generator) slot + an effect
    // chain, parallel to the VST3 instrument/effects. Same edit-mirror as the FX chain:
    // the UI mutates the *_edit copies under op_fx_mtx + bumps op_fx_gen; the audio thread
    // try_lock-copies into the working ones. Removed ops go to op_retired (freed at shutdown).
    vivid::AudioOp*              op_instrument = nullptr;   // audio working copy
    std::vector<vivid::AudioOp*> op_effects;                // audio working copy
    std::mutex                   op_fx_mtx;
    vivid::AudioOp*              op_instrument_edit = nullptr;
    std::vector<vivid::AudioOp*> op_effects_edit;
    // Additional graph source ops beyond the primary op_instrument_edit (a key-split track has
    // >1 instrument source). Ownership only — the audio thread reaches every source op via
    // gbinds; these are retired at teardown. Only ever non-empty on an authoritative graph.
    std::vector<vivid::AudioOp*> op_sources_edit;
    std::atomic<uint64_t>        op_fx_gen{0};
    uint64_t                     op_fx_gen_seen = 0;
    std::vector<vivid::AudioOp*> op_retired;
    // CLAP instrument + FX chain (raw, owned by the Track; freed at shutdown via clap_retired).
    // A track's single instrument source is CLAP > native > VST3 by precedence (rebuild_track_graph).
    // Set on the UI thread + republished via ggen; the audio thread only reads them through gbinds.
    ClapHandle*              clap_inst = nullptr;
    std::vector<ClapHandle*> clap_effects;
    std::vector<ClapHandle*> clap_retired;
    // A2: plugin nodes the USER spawned into the graph (as opposed to those derived from the linear
    // chain). Each slot owns one plugin handle and is addressed by a stable index that is never
    // recycled, so a node keeps its handle no matter where it sits or what is added/removed around
    // it. UI thread only, under gmtx; the audio thread only ever sees the resolved raw pointers
    // copied into gbinds. A dead slot keeps its entry (the array is append-only) so a late async
    // completion can tell "this node is gone" from "this slot is someone else's now".
    struct PluginSlot {
        int         format = 0;          // PluginFormat (kFmtVST3 / kFmtCLAP)
        bool        is_source = false;   // instrument (fan-in) vs effect (splice)
        std::string path, uid;
        Vst3Handle* vst3 = nullptr;      // owned; null while an async load is in flight, or on failure
        ClapHandle* clap = nullptr;      // owned
        bool        pending = false;     // an async load is in flight for this slot
        bool        dead = false;        // its node was removed; the handle is retired, not freed
    };
    std::vector<PluginSlot> pslots;
    // Preset browse cache (UI thread only): the track instrument's presets (name/id + discovery
    // metadata), filled by session_track_preset_scan and read by the count/name/id/... accessors.
    std::vector<PresetEntry> preset_cache;
    // AG-0 audio graph (ADR-0012). Working copies (audio thread) + edit copies (UI thread),
    // published via ggen/gmtx like the FX chain. `gok` gates the graph path per track (false
    // => inline). Working buffers are reserved to capacity so the audio-thread copy from the
    // edit copies never reallocates. `blk` is transient (filled each block).
    vivid::audio::CompiledAudioGraph gcg, gcg_edit;   // topology plan (POD steps)
    std::vector<GNodeBind>           gbinds, gbinds_edit;
    // ADR-0022 P1b.2: the publish HANDOFF buffer. The UI thread copies the freshly-built edit plan
    // (gcg_edit/gbinds_edit/gok_edit) into these under gmtx (publish_track_plan); the audio thread
    // then pointer-SWAPS them into the working plan (gcg/gbinds/gok) under a try_lock — so the
    // callback exchanges buffers instead of copying the steps/binds vectors. The UI's authoritative
    // *_edit copies are never swapped (UI queries still read them). Reserved to kGraphMaxNodes so
    // both the UI copy and the audio swap stay allocation-free.
    vivid::audio::CompiledAudioGraph gcg_ho;
    std::vector<GNodeBind>           gbinds_ho;
    bool                             gok_ho = false;
    // ADR-0015: the NOTE buffer pool — one fixed-capacity note list per note-emitting node
    // (CompiledAudioGraph::note_buf_count). Sized + reserved on the UI thread when a plan is
    // published, so the audio thread only ever clear()s and push_back()s within capacity: no
    // allocation in the callback. Empty (and free) for any graph without note edges.
    std::vector<std::vector<vivid::session::NoteEvent>> npool;
    std::vector<vivid::session::NoteEvent> nmerge;   // scratch: >1 note edge into one node (reserved)
    std::vector<float>               gpool;            // node-buffer pool
    // ADR-0022 P2a.1: the CONTROL pool moved to the Session (`Session::ctl_pool`, per-track regions),
    // so cross-track modulation can read across track boundaries. A modulator writes its 0..1 signal
    // into this track's region; a consumer reads it via `blk.ctl_pool[blk.ctl_base + src_buf·...]`.
    // Per-node output-waveform scope (UI preview): the audio thread pushes kScopePerBlock decimated
    // samples of each node's output into a fixed ring (indexed by out_buf); the UI reads a snapshot to
    // draw a live waveform. Display-only — no alloc/lock; a torn head read is a harmless visual blip.
    std::vector<float>               node_scope;       // kGraphMaxNodes * kScopeN
    std::vector<uint32_t>            node_scope_head;  // kGraphMaxNodes write positions
    // ADR-0022: each modulator node's latest 0..1 output, published for the UI (indexed by node
    // index == out_buf). The UI applies the SAME control_resolve() the audio thread uses, so the
    // live dot on a modulated knob never drifts from what you hear. Display-only, single float —
    // a torn read is a harmless one-frame blip, exactly like node_scope.
    std::unique_ptr<std::atomic<float>[]> ctl_pub;     // kGraphMaxNodes
    bool                             gok = false, gok_edit = false;
    std::mutex                       gmtx;
    std::atomic<uint64_t>            ggen{0};
    uint64_t                         ggen_seen = 0;
    GraphBlockCtx                    blk;
    // AG-1: the persistent, authoritative topology model (UI thread; guarded by gmtx alongside
    // the edit plan). rebuild_track_graph populates it and compiles it into gcg_edit — the
    // audio thread never touches it, only the compiled plan. `agnodes` mirrors it 1:1 (parallel
    // to nodes(), same index) so introspection can report each node's kind + bound op.
    vivid::audio::AudioGraph         agraph;
    std::vector<GNodeBind>           agnodes;
    // AG-1 step 2: once the user edits topology directly (connect/disconnect/add/remove a node),
    // the graph — not the linear device chain — is the source of truth. rebuild_track_graph then
    // stops regenerating from op_instrument_edit/op_effects_edit and only recompiles the agraph.
    bool                             graph_authoritative = false;
    // Live MIDI editing: the UI edits edit_clips; the audio thread copies them
    // into `clips` element-wise (clip addresses stay stable) when edit_gen bumps.
    std::mutex            edit_mtx;
    std::vector<MidiClip> edit_clips;
    std::atomic<uint64_t> edit_gen{0};
    uint64_t              edit_gen_seen = 0;
    // Audio track: no plugin; per-scene samples played transport-locked. `aud_clips` is
    // sized to `scenes` (an empty Sampler = empty cell). Content edits (stash/place a
    // clip) happen on the UI thread under aud_mtx; the audio thread try_locks it around
    // render (skips a block on contention) — the UI critical section is an O(1) move.
    bool                  is_audio = false;
    std::vector<Sampler>  aud_clips;
    std::vector<std::unique_ptr<ClipDsp>> aud_dsp;   // A2: per-slot warp stretcher (null until warp on)
    std::mutex            aud_mtx;
    std::atomic<float>    aud_trim0[kMaxScenes];   // per-scene loop window (fractions)
    std::atomic<float>    aud_trim1[kMaxScenes];
};

// A loose clip in the session-level pool (lives outside the track grid). Holds either a
// MIDI clip or an audio clip (Sampler). UI-thread-only storage: the audio thread never
// reads `Session::pool`, so no edit-mirror is needed.
struct PoolClip { bool is_audio = false; MidiClip clip; Sampler audio; std::string name; };

// Live monitored/recorded notes carry a note_id in a reserved range so their offs match
// their ons and never collide with clip-scheduled note_ids (which start at 0).
static constexpr int32_t kLiveNoteIdBase = 0x40000000;

// A note captured while recording (M6.3). Beats are absolute transport beats; on commit
// they map to clip-local positions (fmod by the clip length). `open` = note-on seen,
// awaiting its note-off.
struct RecNote { int pitch; double beat_on; double beat_off; float vel; bool open; };

// ADR-0022 P1b: the MASTER node — the session's single sink. It SUMS every track that rendered
// this block, applies the master gain, and publishes the master meters. This is the seed of the
// compiled master step (P1b.3): today's per-track mix loop no longer accumulates into `out`; it
// renders + meters each track, and this node does the summation. `gain` defaults to 1.0, which
// keeps the mix bit-identical with the pre-P1b inline sum. The meter fields are the master's own
// state block: `flt_lo/flt_hi/tr_baseline` are audio-thread-only running state; the atomics are
// UI-read (same layout + math as a Track's meters).
struct Master {
    std::atomic<float> gain{1.f};
    std::atomic<float> level{0.f}, transient{0.f};
    std::atomic<float> band_low{0.f}, band_mid{0.f}, band_high{0.f};
    float flt_lo = 0.f, flt_hi = 0.f, tr_baseline = 0.f;
};

struct Session {
    Vst3HostApp host;
    vivid::OpRegistry* op_reg = nullptr;   // shared operator registry (for native audio ops); set at init
    std::vector<PoolClip> pool;   // clips stashed outside the grid (browser sidebar; UI-thread-only)
    // `tracks` is the UI/main-thread-authoritative list + owner (every session_* accessor
    // indexes it). The audio thread NEVER touches it; it iterates `tracks_view`, refreshed
    // from `tracks_pub` via tracks_gen + try_lock — the same edit-mirror pattern as the
    // per-track FX list, lifted to the track list. Removed tracks move to `tracks_retired`
    // (kept alive so an in-flight audio block never sees a freed Track) and are freed at
    // shutdown. All three Track* vectors are reserved to kMaxTracks so the swap + pushes
    // never reallocate.
    std::vector<std::unique_ptr<Track>> tracks;
    std::vector<std::unique_ptr<Track>> tracks_retired;
    std::vector<Track*>   tracks_pub;     // UI-published snapshot (guarded by tracks_mtx)
    std::vector<Track*>   tracks_view;    // audio working copy (audio thread only)
    std::mutex            tracks_mtx;
    // ADR-0022 P1b: the master node + its input list. `render_list` is the set of tracks that
    // actually rendered this block (a source present), in tracks_view order — the master node sums
    // their outputs. Audio-thread-only scratch, reserved to kMaxTracks so the per-block clear +
    // push_back never allocate.
    Master                master;
    std::vector<Track*>   render_list;
    // ADR-0022 P1b.3a: ONE session-owned buffer pool for the track OUTPUTS (was per-track
    // Track::bl/br). A track's `slot` (its render order this block) writes its final L/R into
    // track_out_pool[slot] — L at slot*2*kGraphMaxBlock, R one kGraphMaxBlock later — and the master
    // node sums the slices of render_list. Sized to kMaxTracks slots × stereo × kGraphMaxBlock; the
    // audio thread only ever writes within a slot (frames ≤ kGraphMaxBlock, guarded in
    // session_process), so there is no allocation in the callback.
    std::vector<float>    track_out_pool;
    // ADR-0022 P2a.1: ONE session-owned CONTROL pool (was per-track Track::cpool), laid out as
    // per-track regions — the control analog of track_out_pool. Track at tracks_view index `i` owns
    // region `i` (base = i · kGraphMaxNodes · kGraphMaxBlock floats); a control buffer `c` in that
    // region holds a block at base + c·kGraphMaxBlock. A modulator writes its 0..1 signal into its
    // region; a consumer reads `ctl_pool[region_base + src_buf·kGraphMaxBlock]` (sample 0). One pool,
    // one source of truth for every control value — the substrate cross-track modulation reads across
    // regions (P2a.2). Sized to kMaxTracks regions (== the old aggregate per-track cpools).
    std::vector<float>    ctl_pool;
    std::atomic<uint64_t> tracks_gen{0};
    uint64_t              tracks_gen_seen = 0;
    int       next_track_id = 0;   // monotonic source of stable per-track IDs
    int       scenes = 3;
    long long last_bar = -1;
    uint32_t  sample_rate = 0;
    // Live MIDI input (M6): monitored/recorded notes flow through `live_in` to the armed
    // track's instrument. `armed_track` is a stable track id (-1 = none). Both are read on
    // the audio thread; the id is a plain atomic, resolved to a Track* each block.
    LiveMidi              live_in;
    std::atomic<int>      armed_track{-1};
    std::atomic<double>   play_beats{0.0};   // last transport beat the audio thread saw (stamps live input)
    // Recording (M6.3): while `recording`, live note on/off are captured (with their
    // absolute transport beat) into rec_notes on the producer thread; on stop they are
    // mapped to clip-local positions and overdubbed into the armed track's active clip.
    std::atomic<bool>     recording{false};
    double                rec_capture_from = 0.0;   // don't capture notes before this beat (count-in)
    std::mutex            rec_mtx;
    std::vector<RecNote>  rec_notes;
    std::atomic<bool>     metronome{false};   // audible click on each beat while enabled
    // --- Async CLAP loading. A slow plugin ctor (Surge scans its wavetable dir — seconds to
    // minutes) must NOT run on the main thread, or it wedges the frame loop + the control-server
    // drain. Requests run on one background worker; session_poll_plugin_loads() applies the
    // finished handles on the main thread (all Track/graph edits stay UI-thread). ---
    // track_id is the STABLE per-track id (not the slot index) — a load can finish after tracks are
    // reordered/removed (e.g. opening another project), so the completion must re-resolve identity.
    // `slot` >= 0 addresses a per-track PluginSlot (a graph node the user spawned); -1 means the
    // legacy destination (the track's clap_inst / clap_effects chain slots).
    struct ClapLoadReq  { int track_id; bool is_instrument; std::string path; double sr; std::string state; int slot = -1; };
    struct ClapLoadDone { int track_id; bool is_instrument; std::string path; ClapHandle* handle; std::string state; int slot = -1; };
    std::thread              clap_worker;
    std::mutex               clap_load_mtx;      // guards clap_reqs / clap_done / clap_worker_stop
    std::condition_variable  clap_load_cv;
    std::deque<ClapLoadReq>  clap_reqs;
    std::deque<ClapLoadDone> clap_done;
    std::atomic<int>         clap_pending{0};    // requested-but-not-yet-applied loads
    bool                     clap_worker_stop = false;
    std::string              clap_last_error;    // main-thread only (last failed async load)
};

static void recompute_mix_scales(Session* s);   // ADR-0022 P1b.4 (defined below)

// Republish the current track membership for the audio thread (UI/main thread only).
// Call after any add/remove; the audio thread picks it up on its next block.
static void rebuild_track_view(Session* s) {
    // ADR-0022 P1b.4: membership changed — a new track must respect an active solo, and dropping a
    // soloed track must un-silence the rest. Recompute before publishing the new view.
    recompute_mix_scales(s);
    std::lock_guard<std::mutex> lk(s->tracks_mtx);
    s->tracks_pub.clear();
    for (auto& tp : s->tracks) s->tracks_pub.push_back(tp.get());
    s->tracks_gen.fetch_add(1, std::memory_order_release);
}

static Vst3Handle* load_effect(const std::string& path, uint32_t sr, Vst3HostApp* host) {
    Vst3Handle* h = vst3_load_plugin(path.c_str(), "", sr, std::string(), host, /*as_effect*/true);
    if (!h) return nullptr;
    if (h->processor->setProcessing(true) != kResultOk) {}
    h->processing = true;
    return h;
}

// Parse a loop's source tempo from its path (e.g. ".../140 BPM/..." or "112bpm").
static double parse_bpm(const std::string& path) {
    std::string p = path;
    for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (size_t pos = p.find("bpm"); pos != std::string::npos; pos = p.find("bpm", pos + 1)) {
        long i = static_cast<long>(pos) - 1;
        while (i >= 0 && p[i] == ' ') --i;
        long end = i;
        while (i >= 0 && std::isdigit(static_cast<unsigned char>(p[i]))) --i;
        if (end > i) {
            int bpm = std::atoi(p.substr(i + 1, end - i).c_str());
            if (bpm >= 40 && bpm <= 300) return bpm;
        }
    }
    return 0.0;
}

static bool name_has(const std::string& path, const char* lower_needle) {
    std::string p = path;
    for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return p.find(lower_needle) != std::string::npos;
}

// An audio track keeps exactly `scenes` clip slots (an empty Sampler = empty cell), so
// stash/place/launch address any scene by a stable index.
static void pad_aud_clips(Track* t, int scenes) {
    t->aud_clips.reserve(kMaxScenes);   // append within reserved capacity → no realloc (RT-safe growth)
    if (static_cast<int>(t->aud_clips.size()) > scenes) t->aud_clips.resize(scenes);
    while (static_cast<int>(t->aud_clips.size()) < scenes) t->aud_clips.emplace_back();
}

// AG-0: reserve a track's audio-graph working buffers to capacity so the audio-thread copy
// from the edit copies never reallocates (RT-safe). Call once at track creation.
static void reserve_track_graph(Track* t) {
    t->gbinds.reserve(kGraphMaxNodes);       t->gbinds_edit.reserve(kGraphMaxNodes);
    t->gcg.steps.reserve(kGraphMaxNodes);    t->gcg_edit.steps.reserve(kGraphMaxNodes);
    t->gbinds_ho.reserve(kGraphMaxNodes);    t->gcg_ho.steps.reserve(kGraphMaxNodes);   // P1b.2 publish handoff
    t->gpool.assign(static_cast<size_t>(kGraphMaxNodes + 1) * 2 * kGraphMaxBlock, 0.f);
    t->node_scope.assign(static_cast<size_t>(kGraphMaxNodes) * kScopeN, 0.f);
    t->node_scope_head.assign(kGraphMaxNodes, 0u);
    t->src_nev.reserve(256);   t->src_eev.reserve(256);   // key-range filter scratch (>= any block's note count)
    // ADR-0015: the note pool — one note list per possible note-emitting node, each reserved to
    // kGraphMaxNotes. Preallocated to the same worst case as the audio pool, so the audio thread
    // only ever clear()s and push_back()s within capacity: no allocation in the callback.
    t->npool.resize(kGraphMaxNodes);
    for (auto& nb : t->npool) nb.reserve(kGraphMaxNotes);
    t->nmerge.reserve(kGraphMaxNotes);
    // ADR-0022 P2a.1: the control pool is now session-owned (Session::ctl_pool, per-track regions),
    // sized once at session init — no per-track allocation here. `ctl_pub` (the UI live-dot atomics)
    // stays per-track.
    t->ctl_pub.reset(new std::atomic<float>[kGraphMaxNodes]);
    for (int i = 0; i < kGraphMaxNodes; ++i) t->ctl_pub[i].store(0.f, std::memory_order_relaxed);
}

// AG-0: (re)compile a track's audio graph from its native chain (UI thread). The graph path
// currently runs only pure-native tracks — a native instrument + native FX, no VST3 handle
// and no VST3 effects; anything else falls back to the inline path (gok=false). Builds a
// linear graph inst -> fx0 -> ... -> Output, publishes it via the ggen/gmtx edit-mirror.
// AG-1: (re)build a track's authoritative audio graph from its native device chain and compile
// it into the edit plan. The graph is the persistent source of truth for topology (`t->agraph`);
// `t->agnodes` mirrors nodes() 1:1 (same index) carrying each node's kind + bound op, and IS the
// edit-side binding array the audio thread copies. Currently the chain is laid out linearly
// (inst → fx… → out); edge surgery for arbitrary topology builds on this same persistent model.
// AG-1 step 2: recompile the authoritative agraph into the edit plan + publish to the audio
// thread (the usual ggen/gmtx mirror). Caller MUST hold t->gmtx. Returns false if the graph does
// not compile (a cycle) — in that case gcg_edit is left untouched (AudioGraph::compile bails
// before writing `out`), so the last good plan keeps playing; the caller reverts its edit.
// ADR-0022 P1b.2: stage the just-built edit plan into the handoff buffer, then bump the gen. The
// steps/binds COPY happens HERE, on the UI thread (caller holds t->gmtx); the audio thread's apply
// only pointer-SWAPS the handoff into its working plan — no vector copy in the callback. Copies
// exactly the fields the audio thread reads (steps + buf_count + output_buf + binds + gok), matching
// the pre-P1b.2 in-callback copy. Reserved capacities keep this copy allocation-free.
static void publish_track_plan(Track* t) {
    t->gcg_ho.steps      = t->gcg_edit.steps;
    t->gcg_ho.buf_count  = t->gcg_edit.buf_count;
    t->gcg_ho.output_buf = t->gcg_edit.output_buf;
    t->gbinds_ho = t->gbinds_edit;
    t->gok_ho    = t->gok_edit;
    t->ggen.fetch_add(1, std::memory_order_release);
}

static bool republish_track_graph(Track* t) {
    if (!t->agraph.compile(t->gcg_edit)) return false;   // cycle → published plan unchanged
    t->gbinds_edit = t->agnodes;                          // parallel to nodes(): index == out_buf
    t->gok_edit    = true;
    publish_track_plan(t);
    return true;
}

// Bind the track's loaded plugin handles into its authoritative-graph placeholder nodes. A saved
// authoritative graph reloads its topology as bare Vst3Inst/ClapInst/Vst3Fx/ClapFx placeholders
// (session_audio_graph_load_node) because the plugins load asynchronously; when they land
// (session_poll_plugin_loads) their handles must be threaded back into those nodes before the plan
// republishes, or the graph keeps a null source/effect and stays silent. Matches placeholders to
// handles by kind + node order (the seed built them in the same order the plugins are saved/loaded).
// Caller MUST hold t->gmtx. Non-plugin nodes (native ops / Sampler / Output) are left untouched.
static void rebind_authoritative_plugins(Track* t) {
    size_t vfx = 0, cfx = 0;                          // next VST3/CLAP effect to bind (node order)
    for (GNodeBind& nb : t->agnodes) {
        // A2: a node that owns a plugin SLOT binds from it directly — position-independent, so the
        // node can sit anywhere in the graph. A null handle here is a plugin still loading (CLAP is
        // async) and is already safe: run_track_graph gates on handle/clap being non-null, so the
        // node passes audio through (effect) or is silent (instrument) until it binds.
        if (nb.pslot >= 0 && nb.pslot < static_cast<int>(t->pslots.size())) {
            const Track::PluginSlot& ps = t->pslots[static_cast<size_t>(nb.pslot)];
            nb.handle = ps.vst3;
            nb.clap   = ps.clap;
            continue;
        }
        // Legacy: nodes derived from the linear device chain, and projects saved before slots
        // existed. Their handles are matched by kind + order, which is only sound because such a
        // graph IS the chain, in chain order.
        switch (nb.kind) {
            case GNKind::Vst3Inst: nb.handle = t->handle;    break;   // single source per track
            case GNKind::ClapInst: nb.clap   = t->clap_inst; break;
            case GNKind::Vst3Fx:   nb.handle = (vfx < t->effects_edit.size()) ? t->effects_edit[vfx++] : nullptr; break;
            case GNKind::ClapFx:   nb.clap   = (cfx < t->clap_effects.size()) ? t->clap_effects[cfx++]  : nullptr; break;
            default: break;
        }
    }
}

static void rebuild_track_graph(Track* t) {
    std::lock_guard<std::mutex> lk(t->gmtx);
    // Once the graph is authoritative, a legacy device-chain edit must not wipe the user's
    // topology — just re-bind any (newly landed) plugin handles into their placeholder nodes and
    // recompile what's there (e.g. after a param change, or an async plugin load completing).
    if (t->graph_authoritative) { rebind_authoritative_plugins(t); republish_track_graph(t); return; }
    t->agraph.reset();   // derived linear path regenerates from scratch → deterministic 0-based ids
    t->agnodes.clear();
    // Derived chain (AG-0): source (sampler for an audio track; else a native instrument takes
    // precedence over a VST3 instrument, matching the inline path) → all VST3 FX → all native FX →
    // Output, in the EXACT order session_process applies them (VST3 FX loop before the native FX loop)
    // so the graph is parity-by-construction.
    const bool has_clap_inst   = t->clap_inst != nullptr;
    const bool has_native_inst = t->op_instrument_edit != nullptr;
    const bool has_vst3_inst   = t->handle != nullptr;
    const bool has_source = t->is_audio || has_clap_inst || has_native_inst || has_vst3_inst;
    const int  node_count = 1 + static_cast<int>(t->effects_edit.size())
                              + static_cast<int>(t->op_effects_edit.size())
                              + static_cast<int>(t->clap_effects.size()) + 1;   // source + VST3 FX + native FX + CLAP FX + output
    if (!has_source || node_count > kGraphMaxNodes) {
        t->gbinds_edit.clear();
        t->gok_edit = false;
        publish_track_plan(t);
        return;
    }
    int prev;
    if (t->is_audio) {
        t->agnodes.push_back({ GNKind::Sampler, nullptr, nullptr });   // scene-resolved at process time
        prev = t->agraph.add_node(true, false, nullptr, nullptr, "smp");
    } else if (has_clap_inst) {
        t->agnodes.push_back({ GNKind::ClapInst, nullptr, nullptr, t->clap_inst });
        prev = t->agraph.add_node(true, false, nullptr, nullptr, "clap");
    } else if (has_native_inst) {
        t->agnodes.push_back({ GNKind::NativeInst, t->op_instrument_edit, nullptr });
        prev = t->agraph.add_node(true, false, nullptr, nullptr, "inst");   // node 0 == out_buf 0
    } else {
        t->agnodes.push_back({ GNKind::Vst3Inst, nullptr, t->handle });
        prev = t->agraph.add_node(true, false, nullptr, nullptr, "vst3");
    }
    for (Vst3Handle* fx : t->effects_edit) {                                // VST3 FX first (inline order)
        t->agnodes.push_back({ GNKind::Vst3Fx, nullptr, fx });
        const int n = t->agraph.add_node(false, false, nullptr, nullptr, "vfx");
        t->agraph.connect(prev, n);
        prev = n;
    }
    for (vivid::AudioOp* op : t->op_effects_edit) {                         // then native FX
        t->agnodes.push_back({ GNKind::NativeFx, op, nullptr });
        const int fx = t->agraph.add_node(false, false, nullptr, nullptr, "fx");
        t->agraph.connect(prev, fx);
        prev = fx;
    }
    for (ClapHandle* cfx : t->clap_effects) {                              // then CLAP FX
        t->agnodes.push_back({ GNKind::ClapFx, nullptr, nullptr, cfx });
        const int fx = t->agraph.add_node(false, false, nullptr, nullptr, "cfx");
        t->agraph.connect(prev, fx);
        prev = fx;
    }
    t->agnodes.push_back({ GNKind::Output, nullptr, nullptr });
    const int out = t->agraph.add_node(false, true, nullptr, nullptr, "out");
    t->agraph.connect(prev, out);
    t->agraph.set_output_id(out);
    t->gbinds_edit = t->agnodes;                 // parallel to nodes(): index == out_buf
    t->gok_edit = t->agraph.compile(t->gcg_edit);
    publish_track_plan(t);
}

// The per-source/effect render primitives (defined below, near session_process); forward-declared
// so the graph dispatch can call the same helpers the inline path does.
static void emit_vst3(Vst3EventList& events, const std::vector<NoteEvent>& nev, const std::vector<ExprEvent>& eev);
static inline void filter_notes_by_range(const std::vector<NoteEvent>& src, uint8_t lo, uint8_t hi, std::vector<NoteEvent>& dst);
static inline void filter_expr_by_range(const std::vector<ExprEvent>& src, uint8_t lo, uint8_t hi, std::vector<ExprEvent>& dst);
static void render_vst3_instrument(Track& t, Vst3Handle* h, Vst3EventList& events, const VividAudioContext& ctx, uint32_t frames, float* L, float* R);
static void render_vst3_effect(Track& t, Vst3Handle* fx, const VividAudioContext& ctx, uint32_t frames, float* L, float* R);
static void render_clap_instrument(Track& t, ClapHandle* h, const std::vector<NoteEvent>& notes, uint32_t frames, float* L, float* R);
static void render_clap_effect(Track& t, ClapHandle* h, uint32_t frames, float* L, float* R);
static void render_sampler_block(Track& t, double beats, double delta, uint32_t frames, uint32_t sample_rate, bool playing, float* L, float* R);

// AG-0: execute a track's compiled audio graph on the audio thread (RT-safe — no alloc, no
// lock). Sums each node's inputs into the shared scratch buffer, dispatches the node's
// processor by kind, writes its output buffer; finally copies the Output node's buffer into
// L/R. `t.blk` must be filled for this block before calling.
// ADR-0015: the note stream a node CONSUMES.
//
// No note edges (n_note_in == 0) => the track-wide stream, exactly as before note edges existed.
// That fallback is what makes every existing graph bit-identical, and it is why M1 changes nothing
// audibly: nothing wires a note edge yet.
//
// One note edge => that buffer, by reference (no copy). Several => merged into a reserved scratch,
// ordered by sample offset (a plugin's event list must be in time order). RT-safe: clear + push_back
// within reserved capacity, and std::sort is in-place (std::stable_sort would allocate).
static const std::vector<NoteEvent>& graph_note_input(Track& t, const vivid::audio::CompiledStep& s) {
    if (s.n_note_in <= 0) return t.nev;
    const int n = static_cast<int>(t.npool.size());
    if (s.n_note_in == 1) {
        const int b0 = s.note_in_buf[0];
        return (b0 >= 0 && b0 < n) ? t.npool[static_cast<size_t>(b0)] : t.nev;
    }
    t.nmerge.clear();
    for (int k = 0; k < s.n_note_in; ++k) {
        const int bk = s.note_in_buf[k];
        if (bk < 0 || bk >= n) continue;
        for (const NoteEvent& e : t.npool[static_cast<size_t>(bk)]) {
            if (t.nmerge.size() >= kGraphMaxNotes) break;   // truncate rather than allocate
            t.nmerge.push_back(e);
        }
    }
    std::sort(t.nmerge.begin(), t.nmerge.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.sample_offset < b.sample_offset; });
    return t.nmerge;
}

static void drain_vst3_notes(Vst3Handle* h, std::vector<NoteEvent>& out);   // ADR-0015 (M3), below

// ADR-0015 (M2): the notes a CLAP plugin GENERATED this block (a note effect / chord generator).
// ClapEventScratch::ev_push captured them during clap_run; the host used to throw them away.
static void drain_clap_notes(ClapHandle* h, std::vector<NoteEvent>& out) {
    out.clear();
    if (!h || !h->has_note_out) return;
    for (uint32_t i = 0; i < h->events.out_count; ++i) {
        if (out.size() >= kGraphMaxNotes) break;   // truncate rather than allocate
        const clap_event_note_t& e = h->events.out_notes[i];
        const bool on = e.header.type == CLAP_EVENT_NOTE_ON;
        out.push_back(NoteEvent{ e.header.time, on, static_cast<int>(e.key),
                                 static_cast<float>(e.velocity), e.note_id, 0.f });
    }
}

static void run_track_graph(Track& t, float* L, float* R, uint32_t frames) {
    const vivid::audio::CompiledAudioGraph& cg = t.gcg;
    // RT safety net: bail to silence on any inconsistency between the plan and the working
    // buffers (e.g. a graph published before its pool/bindings were reserved). The audio thread
    // must never index past gpool/gbinds — the pool holds (buf_count + 1) stereo buffers.
    if (frames > kGraphMaxBlock || cg.output_buf < 0 || cg.steps.empty()
        || static_cast<int>(t.gbinds.size()) < cg.buf_count
        || t.gpool.size() < static_cast<size_t>(cg.buf_count + 1) * 2 * frames) {
        std::memset(L, 0, frames * sizeof(float)); std::memset(R, 0, frames * sizeof(float));
        return;
    }
    const uint32_t stride = frames;
    float* pool = t.gpool.data();
    const int scratch = cg.buf_count;
    const GraphBlockCtx& b = t.blk;
    // Rebuilt from the block context for VST3 nodes (ProcessContext); native/sampler read `b` directly.
    VividAudioContext gctx{};
    gctx.sample_rate = b.sample_rate; gctx.metronome_bpm = b.bpm;
    gctx.metronome_beats_per_bar = b.bpb; gctx.metronome_beats_elapsed = b.beats;
    for (const vivid::audio::CompiledStep& s : cg.steps) {
        float* oL = pool + static_cast<size_t>(s.out_buf) * 2 * stride;
        float* oR = oL + stride;
        const GNodeBind& nb = t.gbinds[s.out_buf];

        // ADR-0022: resolve this block's control inputs into param OVERRIDES before the node runs.
        // The modulator upstream already wrote its 0..1 signal into cpool (topo order guarantees
        // it), so we read it, combine with the LIVE BASE (audio_op_param_get reads pvals, never
        // written on this thread), and hand the op the effective value — leaving the base intact.
        // Block-rate: we sample cpool[row][0]. Native only in P0.5; VST3/CLAP land in P2 (they have
        // no host-side base to modulate around — see ADR-0022). Empty for any unmodulated node.
        vivid::AudioOpParamOverride ovr[vivid::audio::kMaxControlInputs];
        uint32_t novr = 0;
        if (s.n_control_in > 0 && nb.op) {
            for (int k = 0; k < s.n_control_in; ++k) {
                const vivid::audio::ControlIn& ci = s.control_in[k];
                if (ci.src_buf < 0 || ci.param < 0) continue;
                const float src = b.ctl_pool[b.ctl_base + static_cast<size_t>(ci.src_buf) * kGraphMaxBlock];   // sample 0, this track's region
                const float lo  = vivid::audio_op_param_min(nb.op, ci.param);
                const float hi  = vivid::audio_op_param_max(nb.op, ci.param);
                // Two modulators on one param STACK: the second resolves around the first's result,
                // not the raw base — so each adds its own swing rather than the last one winning.
                int slot = -1;
                for (uint32_t j = 0; j < novr; ++j) if (ovr[j].param == ci.param) { slot = static_cast<int>(j); break; }
                const float base = slot >= 0 ? ovr[slot].value : vivid::audio_op_param_get(nb.op, ci.param);
                const float v = vivid::audio::control_resolve(base, src, ci.shape, lo, hi);
                if (slot >= 0) ovr[slot].value = v;
                else if (novr < vivid::audio::kMaxControlInputs) ovr[novr++] = { ci.param, v };
            }
        }

        if (s.n_in == 0) {   // source node
            // ADR-0022: a MODULATOR (LFO/envelope). No sound; it writes its 0..1 signal into its
            // cpool row for the control edges downstream to read. A source, so it lands here.
            if (nb.kind == GNKind::NativeMod && nb.op) {
                std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
                float* cout = nullptr;
                if (s.control_out_buf >= 0 && s.control_out_buf < kGraphMaxNodes)
                    cout = &b.ctl_pool[b.ctl_base + static_cast<size_t>(s.control_out_buf) * kGraphMaxBlock];
                vivid::audio_op_process(nb.op, oL, oR, frames, b.sample_rate, b.bpm, b.bpb, b.beats,
                                        nullptr, 0, nullptr, 0, nullptr, ovr, novr, cout, frames);
                // Publish this block's output for the UI's live dot (node index == out_buf).
                if (cout && s.out_buf >= 0 && s.out_buf < kGraphMaxNodes)
                    t.ctl_pub[s.out_buf].store(cout[0], std::memory_order_relaxed);
                continue;
            }
            const bool full_range = (nb.key_lo == 0 && nb.key_hi == 127);
            // ADR-0015: the notes THIS node consumes — its note edge if it has one, else the
            // track-wide stream (the pre-note-edge behavior, which is what keeps parity).
            const std::vector<NoteEvent>& nsrc = graph_note_input(t, s);
            // MidiIn: the track's note stream AS A NODE. It publishes those notes on its note
            // buffer for whatever it feeds, and makes no sound of its own.
            if (nb.kind == GNKind::MidiIn) {
                std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
                if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size())) {
                    std::vector<NoteEvent>& outn = t.npool[static_cast<size_t>(s.note_out_buf)];
                    outn.clear();
                    for (const NoteEvent& e : t.nev) {
                        if (outn.size() >= kGraphMaxNotes) break;   // truncate, never allocate
                        outn.push_back(e);
                    }
                }
                continue;
            }
            // ADR-0015: a native NOTE EFFECT (Arp / chord / transpose). Notes in -> notes out; it
            // makes no sound. Its emitted notes land in its own note buffer, which is what the
            // instrument downstream reads.
            if (nb.kind == GNKind::NativeNoteFx && nb.op) {
                std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
                std::vector<NoteEvent>* outn = nullptr;
                if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size()))
                    outn = &t.npool[static_cast<size_t>(s.note_out_buf)];
                uint32_t produced = 0;
                if (outn) {
                    outn->resize(kGraphMaxNotes);   // within reserved capacity: no allocation
                    vivid::audio_op_process(nb.op, oL, oR, frames, b.sample_rate, b.bpm, b.bpb, b.beats,
                                            nsrc.data(), static_cast<uint32_t>(nsrc.size()),
                                            outn->data(), kGraphMaxNotes, &produced, ovr, novr);
                    outn->resize(produced);
                } else {
                    vivid::audio_op_process(nb.op, oL, oR, frames, b.sample_rate, b.bpm, b.bpb, b.beats,
                                            nsrc.data(), static_cast<uint32_t>(nsrc.size()),
                                            nullptr, 0, nullptr, ovr, novr);
                }
                continue;
            }
            if (nb.kind == GNKind::NativeInst && nb.op) {
                const NoteEvent* nn = nsrc.data(); uint32_t nc = static_cast<uint32_t>(nsrc.size());
                if (!full_range) {   // key-split: hand this source only its in-range notes
                    filter_notes_by_range(nsrc, nb.key_lo, nb.key_hi, t.src_nev);
                    nn = t.src_nev.data(); nc = static_cast<uint32_t>(t.src_nev.size());
                }
                vivid::audio_op_process(nb.op, oL, oR, frames, b.sample_rate, b.bpm, b.bpb, b.beats, nn, nc,
                                        nullptr, 0, nullptr, ovr, novr);
            }
            else if (nb.kind == GNKind::Vst3Inst && nb.handle) {
                std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));  // silent input, matches inline
                if (full_range) {   // t.vev is primed with scene-switch releases (identical to today)
                    emit_vst3(t.vev, nsrc, t.eev);
                    render_vst3_instrument(t, nb.handle, t.vev, gctx, frames, oL, oR);
                } else {            // key-split: independent filtered event list for this source
                    filter_notes_by_range(nsrc, nb.key_lo, nb.key_hi, t.src_nev);
                    filter_expr_by_range(t.eev, nb.key_lo, nb.key_hi, t.src_eev);
                    t.src_vev.clear();
                    emit_vst3(t.src_vev, t.src_nev, t.src_eev);
                    render_vst3_instrument(t, nb.handle, t.src_vev, gctx, frames, oL, oR);
                }
                // ADR-0015 (M3): a plugin that GENERATES notes (a chord generator, an arpeggiator)
                // publishes them on its note output, so they can drive another instrument.
                if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size()))
                    drain_vst3_notes(nb.handle, t.npool[static_cast<size_t>(s.note_out_buf)]);
            }
            else if (nb.kind == GNKind::ClapInst && nb.clap) {
                std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
                if (full_range) render_clap_instrument(t, nb.clap, nsrc, frames, oL, oR);
                else { filter_notes_by_range(nsrc, nb.key_lo, nb.key_hi, t.src_nev);   // key-split
                       render_clap_instrument(t, nb.clap, t.src_nev, frames, oL, oR); }
                // ADR-0015 (M2): a CLAP that GENERATES notes publishes them on its note output.
                if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size()))
                    drain_clap_notes(nb.clap, t.npool[static_cast<size_t>(s.note_out_buf)]);
            }
            else if (nb.kind == GNKind::Sampler) {
                std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));  // silent until the clip renders, matches inline
                render_sampler_block(t, b.beats, b.delta, frames, b.sample_rate, b.playing, oL, oR);
            }
            else { std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float)); }
            continue;
        }
        // Sum inputs into the scratch buffer (predecessors already ran — topo order).
        float* iL = pool + static_cast<size_t>(scratch) * 2 * stride;
        float* iR = iL + stride;
        const float* a0L = pool + static_cast<size_t>(s.in_buf[0]) * 2 * stride;
        std::memcpy(iL, a0L, frames * sizeof(float));
        std::memcpy(iR, a0L + stride, frames * sizeof(float));
        for (int k = 1; k < s.n_in; ++k) {
            const float* akL = pool + static_cast<size_t>(s.in_buf[k]) * 2 * stride;
            const float* akR = akL + stride;
            for (uint32_t i = 0; i < frames; ++i) { iL[i] += akL[i]; iR[i] += akR[i]; }
        }
        std::memcpy(oL, iL, frames * sizeof(float));   // start from the summed input
        std::memcpy(oR, iR, frames * sizeof(float));
        if (nb.kind == GNKind::NativeFx && nb.op)       // effect: transform in place
            vivid::audio_op_process(nb.op, oL, oR, frames, b.sample_rate, b.bpm, b.bpb, b.beats, nullptr, 0,
                                    nullptr, 0, nullptr, ovr, novr);
        else if (nb.kind == GNKind::Vst3Fx && nb.handle && nb.handle->processing)  // non-processing = passthrough (matches inline skip)
            render_vst3_effect(t, nb.handle, gctx, frames, oL, oR);
        else if (nb.kind == GNKind::ClapFx && nb.clap && nb.clap->processing)
            render_clap_effect(t, nb.clap, frames, oL, oR);
        else if (nb.kind == GNKind::Output) {
            // ADR-0022 P1a — the Track-Out node applies the track GAIN, so its buffer IS the track's
            // final output. (Was applied downstream in session_process's mix; relocated here so P1b's
            // master node can simply SUM the track-out buffers.) Bit-identical: x*g here == the old
            // L[i]*g in the mix. `oL/oR` already hold the summed inputs (passthrough above).
            const float g = t.gain.load(std::memory_order_relaxed);
            for (uint32_t i = 0; i < frames; ++i) { oL[i] *= g; oR[i] *= g; }
        }
    }
    // Tap each node's output (L) into its waveform-scope ring for the UI preview. Display-only:
    // fixed buffers, no alloc/lock; a few decimated samples per block advance the rolling scope.
    if (!t.node_scope.empty()) {
        for (const vivid::audio::CompiledStep& s : cg.steps) {
            if (s.out_buf < 0 || s.out_buf >= kGraphMaxNodes) continue;
            const float* nl = pool + static_cast<size_t>(s.out_buf) * 2 * stride;
            float* ring = t.node_scope.data() + static_cast<size_t>(s.out_buf) * kScopeN;
            uint32_t h = t.node_scope_head[s.out_buf];
            for (int c = 0; c < kScopePerBlock; ++c) {
                uint32_t si = static_cast<uint32_t>((2 * c + 1)) * frames / (2u * kScopePerBlock);
                ring[h % kScopeN] = nl[si < frames ? si : frames - 1];
                ++h;
            }
            t.node_scope_head[s.out_buf] = h;
        }
    }
    const float* outL = pool + static_cast<size_t>(cg.output_buf) * 2 * stride;
    std::memcpy(L, outL, frames * sizeof(float));
    std::memcpy(R, outL + stride, frames * sizeof(float));
}

static void list_vst3(const std::string& dir, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string n = e->d_name;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".vst3") == 0)
            out.push_back(dir + "/" + n);
    }
    closedir(d);
}

// --- Demo song: a glitchy IDM sketch in A minor (8-beat / 2-bar scenes so it evolves, not a
// 1-bar loop). Patterns are GENERATED with a fixed-seed xorshift RNG — rich, glitchy variation
// (velocity jitter, ghost notes, 32nd ratchets, micro-timing) that's the same every launch. Lead
// + bass paint per-note MPE expression (bend / pressure / timbre curves) to show that off. Change
// a seed below (or the DemoRng seeds) to reroll the glitch. ---
namespace demo {
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9e3779b9u) {}
    uint32_t u32() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return (u32() >> 8) * (1.0f / 16777216.0f); }         // 0..1
    float range(float a, float b) { return a + unit() * (b - a); }
    int   pick(int n) { return static_cast<int>(u32() % static_cast<uint32_t>(n)); }
    bool  chance(float p) { return unit() < p; }
};
inline void nadd(MidiClip& c, int pitch, double start, double dur, float vel) {
    ClipNote n{}; n.pitch = pitch; n.start = start; n.dur = dur;
    n.vel = vel < 0.05f ? 0.05f : (vel > 1.f ? 1.f : vel);
    c.notes.push_back(std::move(n));
}
// A ratchet/stutter: k rapid retriggers of `pitch` across [start, start+span) — the IDM glitch.
inline void nratchet(MidiClip& c, int pitch, double start, double span, int k, float vel, Rng& r) {
    const double st = span / k;
    for (int i = 0; i < k; ++i) nadd(c, pitch, start + i * st, st * 0.7, vel * r.range(0.6f, 1.f));
}
// Paint MPE curves on the most-recent note (played back as VST3 note-expression events).
inline void nbend  (MidiClip& c, float fromSemi, float toSemi) { if (!c.notes.empty()) c.notes.back().expr[AXIS_BEND].bp = { {0.f, fromSemi}, {1.f, toSemi} }; }
inline void ntimbre(MidiClip& c, float lo, float hi)           { if (!c.notes.empty()) c.notes.back().expr[AXIS_TIMBRE].bp = { {0.f, lo}, {0.45f, hi}, {1.f, lo} }; }
inline void npress (MidiClip& c, float peak)                   { if (!c.notes.empty()) c.notes.back().expr[AXIS_PRESSURE].bp = { {0.f, 0.f}, {0.35f, peak}, {1.f, 0.f} }; }

// A minor scale over two octaves (A3..A5) for the lead; low roots (A1..A2) for the bass.
constexpr int LEAD[15] = { 57,59,60,62,64,65,67, 69,71,72,74,76,77,79, 81 };
constexpr int A3 = 57, A4 = 69;
}  // namespace demo

// LEAD (played by the lead synth): glitch arps + expressive stabs. Scene A groove, B denser/higher
// stutter, C sparse MPE breakdown.
static std::vector<MidiClip> base_patterns() {
    using namespace demo;
    // A — 16th glitch arp cycling a minor shape, downbeat accents, occasional ratchet/timbre.
    MidiClip a; a.length = 8.0; { Rng r(0x1EAD01u);
        const int cell[8] = { 57,60,64,67, 60,64,69,72 };            // A C E G / C E A C
        for (int i = 0; i < 32; ++i) { const double t = i * 0.25;
            int p = cell[i % 8] + (r.chance(0.15f) ? 12 : 0);
            if (!r.chance(0.88f)) continue;
            const float v = (i % 4 == 0 ? 0.9f : 0.5f) * r.range(0.85f, 1.f);
            if (r.chance(0.12f)) nratchet(a, p, t, 0.25, 2 + r.pick(3), v, r);
            else { nadd(a, p, t, 0.18, v); if (r.chance(0.14f)) ntimbre(a, 0.2f, 0.9f); }
        }
        nadd(a, 76, 3.5, 0.5, 0.85f); nbend(a, -2.f, 0.f);           // a bent stab
        nadd(a, 69, 7.0, 1.0, 0.8f);  npress(a, 0.7f);
    }
    // B — higher, busier, more stutter + pitch-drop bends (the "build").
    MidiClip b; b.length = 8.0; { Rng r(0x1EAD02u);
        for (int i = 0; i < 32; ++i) { const double t = i * 0.25;
            if (!r.chance(0.82f)) continue;
            int p = LEAD[7 + r.pick(8)];                              // upper octave
            const float v = r.range(0.45f, 0.95f);
            if (r.chance(0.28f)) nratchet(b, p, t, 0.25, 2 + r.pick(4), v, r);
            else { nadd(b, p, t, 0.15, v); if (r.chance(0.2f)) { nbend(b, r.range(-3.f, 0.f), 0.f); } }
        }
    }
    // C — sparse MPE breakdown: long held notes with bend sweeps + pressure/timbre swells.
    MidiClip c; c.length = 8.0;
    nadd(c, 69, 0.0, 2.25, 0.78f); nbend(c, -5.f, 0.f);  npress(c, 0.85f);
    nadd(c, 72, 2.5, 1.25, 0.72f); ntimbre(c, 0.1f, 0.95f);
    nadd(c, 76, 4.0, 3.5, 0.85f);  nbend(c, 0.f, 2.f);   npress(c, 0.9f); ntimbre(c, 0.2f, 0.85f);
    return { a, b, c };
}

// BASS (played by the bass synth): syncopated sub in A minor. Its own patterns (not a transposed
// lead), so lead + bass interlock.
static std::vector<MidiClip> bass_patterns() {
    using namespace demo;
    // A — driving syncopated root/fifth with octave pops.
    MidiClip a; a.length = 8.0; { Rng r(0xBA5501u);
        const double hits[] = { 0.0, 0.75, 1.5, 2.0, 2.75, 3.5, 4.0, 4.75, 5.5, 6.0, 6.75, 7.5 };
        const int deg[]     = { 33,   33,   40,  36,  33,   45,  33,  33,   38,  40,  33,   43 };  // A A E C A A' A A D E A G
        for (int i = 0; i < 12; ++i) nadd(a, deg[i], hits[i], r.range(0.35, 0.6), r.range(0.75f, 1.f));
        for (int i = 0; i < 8; ++i) if (r.chance(0.3f)) nadd(a, 33, i * 1.0 + 0.875, 0.12, r.range(0.3f, 0.5f)); // ghost 16ths
    }
    // B — faster gated 16ths glitch, octave jumps.
    MidiClip b; b.length = 8.0; { Rng r(0xBA5502u);
        for (int i = 0; i < 32; ++i) { const double t = i * 0.25;
            if (!r.chance(0.6f)) continue;
            int p = (r.chance(0.25f) ? 45 : 33) + (r.chance(0.15f) ? r.pick(3) * 3 : 0);
            if (r.chance(0.2f)) nratchet(b, p, t, 0.25, 2 + r.pick(2), r.range(0.6f, 0.9f), r);
            else nadd(b, p, t, 0.18, r.range(0.6f, 0.95f));
        }
    }
    // C — long held sub with a slow reese-style bend (breakdown).
    MidiClip c; c.length = 8.0;
    nadd(c, 33, 0.0, 4.0, 0.9f);  nbend(c, 0.f, -0.4f);
    nadd(c, 36, 4.0, 4.0, 0.85f); nbend(c, 0.f, 0.4f);
    return { a, b, c };
}

// DRUMS: glitchy IDM kit. 36 kick, 38 snare, 39 clap, 37 rim, 42 closed hat, 46 open hat,
// 40/75/67 glitch percs. Scene A broken groove, B rolling/amen, C half-time breakdown.
static std::vector<MidiClip> drum_patterns() {
    using namespace demo;
    const int PERC[4] = { 75, 67, 40, 37 };
    // A — syncopated kick, backbeat snare+clap, ghost snares, 16th hats with random ratchets.
    MidiClip a; a.length = 8.0; { Rng r(0xD00301u);
        for (double k : { 0.0, 2.75, 3.0, 4.0, 6.5, 7.0 }) nadd(a, 36, k, 0.22, r.range(0.85f, 1.f));
        nadd(a, 38, 1.0, 0.2, 0.95f); nadd(a, 38, 5.0, 0.2, 0.95f);
        nadd(a, 39, 3.0, 0.2, 0.85f); nadd(a, 39, 7.0, 0.2, 0.85f);   // clap layer
        for (int i = 0; i < 16; ++i) if (r.chance(0.22f)) nadd(a, 38, i * 0.5 + r.range(-0.02, 0.02), 0.1, r.range(0.18f, 0.4f)); // ghosts
        for (int i = 0; i < 32; ++i) { const double t = i * 0.25; if (!r.chance(0.85f)) continue;
            if (r.chance(0.12f)) nratchet(a, 42, t, 0.25, 2 + r.pick(3), r.range(0.3f, 0.55f), r);
            else nadd(a, 42, t, 0.07, r.range(0.22f, 0.7f)); }
        for (int i = 0; i < 10; ++i) if (r.chance(0.35f)) nadd(a, PERC[r.pick(4)], r.range(0, 8), 0.1, r.range(0.3f, 0.6f)); // glitch
    }
    // B — rolling/amen: busy kicks, snare rolls (ratchets), dense hats.
    MidiClip b; b.length = 8.0; { Rng r(0xD00302u);
        for (int i = 0; i < 16; ++i) { const double t = i * 0.5;
            if (r.chance(0.55f)) nadd(b, 36, t + (r.chance(0.3f) ? 0.25 : 0.0), 0.2, r.range(0.8f, 1.f)); }
        nadd(b, 38, 1.0, 0.2, 0.95f); nadd(b, 38, 3.0, 0.2, 0.9f); nadd(b, 38, 5.0, 0.2, 0.95f); nadd(b, 38, 7.0, 0.2, 0.9f);
        for (int i = 0; i < 16; ++i) if (r.chance(0.4f)) nratchet(b, 38, i * 0.5, 0.5, 2 + r.pick(4), r.range(0.25f, 0.55f), r); // rolls
        for (int i = 0; i < 32; ++i) { const double t = i * 0.25; if (r.chance(0.9f)) nadd(b, 42, t, 0.06, r.range(0.2f, 0.65f)); }
        for (int i = 0; i < 4; ++i) nadd(b, 46, i * 2.0 + 1.5, 0.25, r.range(0.4f, 0.6f));
    }
    // C — half-time breakdown: sparse big hits, occasional glitch perc.
    MidiClip c; c.length = 8.0; { Rng r(0xD00303u);
        nadd(c, 36, 0.0, 0.3, 1.f); nadd(c, 36, 4.0, 0.3, 1.f);
        nadd(c, 38, 2.0, 0.3, 0.95f); nadd(c, 39, 6.0, 0.3, 0.9f);
        for (int i = 0; i < 8; ++i) if (r.chance(0.4f)) nadd(c, 46, i * 1.0, 0.2, r.range(0.35f, 0.55f));
        for (int i = 0; i < 6; ++i) if (r.chance(0.5f)) nratchet(c, PERC[r.pick(4)], r.range(0, 8), 0.4, 2 + r.pick(4), r.range(0.3f, 0.55f), r);
    }
    return { a, b, c };
}

enum TrackKind { kLead = 0, kBass = 1, kDrums = 2 };

static Track* make_track(Vst3Handle* h, const std::string& name, int kind) {
    auto* t = new Track();
    t->handle = h;
    t->name = name;
    t->clips.reserve(kMaxScenes);   // reserve to the scene cap so session_add_scene appends without realloc
    if (kind == kDrums)      for (auto& p : drum_patterns()) t->clips.push_back(p);
    else if (kind == kBass)  for (auto& p : bass_patterns()) t->clips.push_back(p);
    else                     for (auto& p : base_patterns()) t->clips.push_back(p);   // lead, at pitch
    t->sched.reset(&t->clips[0]);
    t->nev.reserve(64); t->scene_rel.reserve(64);
t->eev.reserve(256);
    t->edit_clips = t->clips;  // editor's mirror starts equal to the live clips
    t->edit_clips.reserve(kMaxScenes);   // operator= may shrink capacity to size — re-reserve
    t->effects.reserve(16); t->effects_edit.reserve(16);  // avoid audio-thread realloc
    t->op_effects.reserve(16); t->op_effects_edit.reserve(16);
    reserve_track_graph(t);
    return t;
}

// A dynamically-added instrument track: empty clips (the user authors them) across all
// scenes, so set_clip/launch work immediately.
static Track* make_instrument_track(Vst3Handle* h, const std::string& name, int scenes) {
    auto* t = new Track();
    t->handle = h;
    t->name = name;
    t->clips.reserve(kMaxScenes);   // reserve to the scene cap so session_add_scene appends without realloc
    for (int i = 0; i < scenes; ++i) { MidiClip c; c.length = 4.0; t->clips.push_back(c); }
    t->sched.reset(&t->clips[0]);
    t->nev.reserve(64); t->scene_rel.reserve(64);
t->eev.reserve(256);
    t->edit_clips = t->clips;
    t->edit_clips.reserve(kMaxScenes);   // operator= may shrink capacity to size — re-reserve
    t->effects.reserve(16); t->effects_edit.reserve(16);
    t->op_effects.reserve(16); t->op_effects_edit.reserve(16);
    reserve_track_graph(t);
    return t;
}

// Load the first plugin matching a role's preference list (never "atoms" — no
// license here), skipping anything that isn't an instrument with a MIDI input.
static Vst3Handle* load_role(const std::vector<std::string>& bundles,
                             const char* const* prefer, uint32_t sr,
                             Vst3HostApp* host, std::string& out_name) {
    for (int p = 0; prefer[p]; ++p) {
        for (const auto& path : bundles) {
            if (name_has(path, "atoms")) continue;
            if (!name_has(path, prefer[p])) continue;
            Vst3Handle* h = vst3_load_plugin(path.c_str(), "", sr, std::string(), host);
            if (!h) continue;
            if (!(h->component && h->component->getBusCount(kEvent, kInput) > 0)) { h->destroy(); delete h; continue; }
            if (h->processor->setProcessing(true) != kResultOk) {}
            h->processing = true;
            out_name = h->plugin_name.empty() ? path : h->plugin_name;
            return h;
        }
    }
    return nullptr;
}

namespace {
SessionLoadCb g_load_cb = nullptr;
void*         g_load_user = nullptr;
inline void load_progress(const char* status) { if (g_load_cb) g_load_cb(g_load_user, status); }
}  // namespace

void session_set_load_progress(SessionLoadCb cb, void* user) { g_load_cb = cb; g_load_user = user; }

// Two showcase tracks for the per-track audio node graph — parallel routing a linear device chain
// cannot express. Built via the public graph edit API (the exact calls a user/agent makes), then
// given a clip that makes the split audible. Native ops only (no plugin dependency). The tracks are
// graph-authoritative, so they persist as node/edge topology (and rebuild_track_graph won't wipe them).
// Called AFTER session_set_op_registry (the native ops need the registry), then republishes the
// track list. No-op without a registry.
void session_build_split_showcase(Session* s) {
    if (!s || !s->op_reg) return;
    using namespace demo;
    auto new_native_track = [&](const char* name) -> int {
        s->tracks.emplace_back(make_instrument_track(nullptr, name, s->scenes));
        s->tracks.back()->id = s->next_track_id++;
        return static_cast<int>(s->tracks.size()) - 1;
    };
    auto set_clip = [&](int ti, const MidiClip& clip) {
        Track& t = *s->tracks[ti];
        for (auto& c : t.clips) c = clip;      // the same loop in every scene slot
        t.edit_clips = t.clips;
        t.sched.reset(&t.clips[0]);
    };

    // Track A — frequency-split rack: a sine fans out to a low-pass branch and a high-pass ->
    // Bitcrush branch, merged. A wide arpeggio straddles the ~450 Hz crossover so low notes come
    // out clean and high notes come out crushed.
    {
        const int ti  = new_native_track("Freq Split");
        const int src = session_audio_graph_add_source(s, ti, "TestTone");   // creates the source + Output
        const int out = session_track_audio_graph_output_id(s, ti);
        const int lp  = session_audio_graph_add_op(s, ti, "SVFilter");
        const int hp  = session_audio_graph_add_op(s, ti, "SVFilter");
        const int cr  = session_audio_graph_add_op(s, ti, "Bitcrush");
        // add_op splices linearly (src->lp->hp->cr->out); rewire into two parallel branches.
        session_audio_graph_disconnect(s, ti, lp, hp);
        session_audio_graph_connect(s, ti, lp, out);     // low branch:  src -> lp -> out
        session_audio_graph_connect(s, ti, src, hp);     // high branch: src -> hp -> cr -> out
        session_audio_graph_node_param_set(s, ti, lp, 0, 0.f);      // LowPass
        session_audio_graph_node_param_set(s, ti, lp, 1, 450.f);
        session_audio_graph_node_param_set(s, ti, hp, 0, 1.f);      // HighPass
        session_audio_graph_node_param_set(s, ti, hp, 1, 450.f);
        session_audio_graph_node_param_set(s, ti, cr, 0, 4.f);      // 4-bit crush on the high band
        session_audio_graph_node_param_set(s, ti, cr, 2, 1.f);
        MidiClip a; a.length = 8.0;
        const int low[4] = { 36, 40, 43, 48 }, hi[4] = { 72, 76, 79, 84 };
        for (int i = 0; i < 16; ++i)
            nadd(a, (i % 2 == 0) ? low[(i / 2) % 4] : hi[(i / 2) % 4], i * 0.5, 0.45, 0.8f);
        set_clip(ti, a);
    }

    // Track B — key-range split: two sine sources split at middle C. The low source (0..59) is a
    // clean sub-bass; the high source (60..127) runs through a Bitcrush for a gritty lead — so the
    // two registers are audibly distinct instruments. One clip carries a bassline under a melody;
    // each note routes to its own source purely by pitch.
    {
        const int ti  = new_native_track("Key Split");
        const int lo  = session_audio_graph_add_source(s, ti, "TestTone");   // creates the source + Output
        const int out = session_track_audio_graph_output_id(s, ti);
        const int hi  = session_audio_graph_add_source(s, ti, "TestTone");   // 2nd source -> Output
        session_audio_graph_node_key_range_set(s, ti, lo, 0, 59);
        session_audio_graph_node_key_range_set(s, ti, hi, 60, 127);
        const int cr  = session_audio_graph_add_op(s, ti, "Bitcrush");       // splices both sources -> cr -> out
        session_audio_graph_disconnect(s, ti, lo, cr);                       // pull the low (bass) source off the crush
        session_audio_graph_connect(s, ti, lo, out);                         // clean sub-bass straight to Output
        session_audio_graph_node_param_set(s, ti, cr, 0, 5.f);               // 5-bit crush on the lead
        session_audio_graph_node_param_set(s, ti, cr, 2, 0.85f);
        MidiClip b; b.length = 8.0;
        const int bass[8] = { 36, 36, 43, 36, 41, 41, 38, 43 };
        const int mel[8]  = { 72, 74, 76, 79, 76, 74, 72, 67 };
        for (int i = 0; i < 8; ++i) { nadd(b, bass[i], i * 1.0, 0.9, 0.9f); nadd(b, mel[i], i * 1.0 + 0.25, 0.6, 0.7f); }
        set_clip(ti, b);
    }
    rebuild_track_view(s);   // publish the two new tracks to the audio thread
}

Session* session_create(uint32_t sample_rate) {
    load_progress("Scanning plug-ins...");
    std::vector<std::string> bundles;
    list_vst3("/Library/Audio/Plug-Ins/VST3", bundles);
    if (const char* home = std::getenv("HOME"))
        list_vst3(std::string(home) + "/Library/Audio/Plug-Ins/VST3", bundles);

    // Role-based assignment: lead synth, bass synth, drums. "" matches any
    // remaining instrument as a last resort; drums has no synth fallback.
    struct RoleSpec { const char* prefer[6]; int kind; };
    static const RoleSpec kRoles[] = {
        { { "pigments", "vital", "serum", "", nullptr }, kLead },
        { { "serum", "vital", "pigments", "", nullptr }, kBass },
        { { "ezdrummer", "drumcomputer", "battery", "drum", nullptr }, kDrums },
    };

    auto* s = new Session();
    s->sample_rate = sample_rate;
    s->tracks.reserve(kMaxTracks);
    s->tracks_pub.reserve(kMaxTracks);
    s->tracks_view.reserve(kMaxTracks);
    s->render_list.reserve(kMaxTracks);   // ADR-0022 P1b: master-node input list (audio thread)
    // ADR-0022 P1b.3a: the session track-output pool — one stereo kGraphMaxBlock slot per track.
    s->track_out_pool.assign(static_cast<size_t>(kMaxTracks) * 2 * kGraphMaxBlock, 0.f);
    // ADR-0022 P2a.1: the session control pool — kMaxTracks regions of kGraphMaxNodes control buffers.
    s->ctl_pool.assign(static_cast<size_t>(kMaxTracks) * kGraphMaxNodes * kGraphMaxBlock, 0.f);
    for (const auto& role : kRoles) {
        load_progress(role.kind == kDrums ? "Loading drums..." : "Loading instruments...");
        std::string name;
        Vst3Handle* h = load_role(bundles, role.prefer, sample_rate, &s->host, name);
        if (!h) { std::fprintf(stderr, "[Session] role kind %d unfilled\n", role.kind); continue; }
        s->tracks.emplace_back(make_track(h, name, role.kind));
        s->tracks.back()->id = s->next_track_id++;
        std::fprintf(stderr, "[Session] track %zu: %s\n", s->tracks.size() - 1, name.c_str());
    }

    // Auto-load one audio effect onto the lead track to prove the FX chain.
    if (!s->tracks.empty()) {
        static const char* fx_prefer[] = { "yak", "chowtape", "chow", "portal", "infiltrator", nullptr };
        for (int p = 0; fx_prefer[p]; ++p) {
            std::string fxpath;
            for (const auto& b : bundles) if (!name_has(b, "atoms") && name_has(b, fx_prefer[p])) { fxpath = b; break; }
            if (fxpath.empty()) continue;
            if (Vst3Handle* fx = load_effect(fxpath, sample_rate, &s->host)) {
                s->tracks[0]->effects_edit.push_back(fx);
                s->tracks[0]->effects = s->tracks[0]->effects_edit;  // active immediately (pre-audio)
                std::fprintf(stderr, "[Session] track 0 effect: %s\n",
                             fx->plugin_name.empty() ? fxpath.c_str() : fx->plugin_name.c_str());
                break;
            }
        }
    }

    // A built-in audio (sampler) track. Loads 3 real loops at distinct source
    // tempos (warped to the session) from the Dan Mayo library if present, else
    // falls back to the procedural demo loops.
    {
        load_progress("Loading audio loops...");
        auto at = std::make_unique<Track>();
        at->is_audio = true;
        at->name = "Audio";
        at->gain.store(0.7f, std::memory_order_relaxed);
        for (int i = 0; i < kMaxScenes; ++i) { at->aud_trim0[i].store(0.f); at->aud_trim1[i].store(1.f); }

        namespace fs = std::filesystem;
        std::vector<std::pair<std::string, double>> loops;  // (path, source bpm)
        if (const char* home = std::getenv("HOME")) {
            std::error_code ec;
            fs::path base = fs::path(home) / "Music/Ableton/User Library/Samples/Dan Mayo";
            if (fs::exists(base, ec)) {
                for (auto it = fs::recursive_directory_iterator(base, ec);
                     it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    if (ec) break;
                    if (!it->is_regular_file(ec)) continue;
                    const std::string sp = it->path().string();
                    if (sp.size() < 4 || (sp.compare(sp.size() - 4, 4, ".wav") != 0
                                          && sp.compare(sp.size() - 4, 4, ".WAV") != 0)) continue;
                    const double b = parse_bpm(sp);
                    if (b > 0) loops.emplace_back(sp, b);
                }
            }
        }
        if (!loops.empty()) {
            std::sort(loops.begin(), loops.end(), [](auto& a, auto& b) { return a.second < b.second; });
            std::vector<double> bpms;
            for (auto& l : loops) if (bpms.empty() || bpms.back() != l.second) bpms.push_back(l.second);
            const double pick[3] = { bpms.front(), bpms[bpms.size() / 2], bpms.back() };
            for (double tb : pick) {
                for (auto& l : loops) if (l.second == tb) {
                    Sampler smp;
                    if (sampler_load_wav(l.first, sample_rate, tb, smp)) at->aud_clips.push_back(std::move(smp));
                    break;
                }
            }
        }
        if (at->aud_clips.empty()) {  // no library found
            at->aud_clips.push_back(gen_sub_pulse(sample_rate, 124.0));
            at->aud_clips.push_back(gen_noise_sweep(sample_rate, 124.0));
            at->aud_clips.push_back(gen_bell_loop(sample_rate, 124.0));
        }
        pad_aud_clips(at.get(), s->scenes);
        at->active.store(-1, std::memory_order_relaxed);  // start stopped
        s->tracks.emplace_back(std::move(at));
        s->tracks.back()->id = s->next_track_id++;
        std::fprintf(stderr, "[Session] track %zu: Audio (sampler, %zu loops)\n",
                     s->tracks.size() - 1, s->tracks.back()->aud_clips.size());
    }

    if (s->tracks.empty()) { delete s; return nullptr; }
    rebuild_track_view(s);   // publish the initial set to the audio thread
    // AG-0: build each track's derived audio graph up front so gok tracks (native + VST3) run through
    // the graph from the first block — not only after a device edit. Sampler tracks stay inline until
    // Stage 4. Parity-by-construction (same source/FX helpers, same order).
    for (auto& t : s->tracks) rebuild_track_graph(t.get());
    std::fprintf(stderr, "[Session] %zu tracks, %d scenes\n", s->tracks.size(), s->scenes);
    return s;
}

int  session_track_count(Session* s) { return s ? static_cast<int>(s->tracks.size()) : 0; }
int  session_scene_count(Session* s) { return s ? s->scenes : 0; }
const char* session_track_name(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->name.c_str() : "";
}
int session_track_id(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->id : -1;
}
void session_set_track_id(Session* s, int t, int id) {   // load-time restore of a saved id
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return;
    s->tracks[t]->id = id;
    if (id >= s->next_track_id) s->next_track_id = id + 1;   // keep new ids from colliding
}

// --- Live MIDI input / record-arm (M6) -------------------------------------------------
// The armed track is stored as a stable id so it survives track reorders (like mappings).
// The UI passes track *indices*; we convert at the boundary.
static Track* armed_track_ptr(Session* s) {
    if (!s) return nullptr;
    const int id = s->armed_track.load(std::memory_order_relaxed);
    if (id < 0) return nullptr;
    for (auto& tp : s->tracks) if (tp->id == id) return tp.get();
    return nullptr;
}
void session_set_armed_track(Session* s, int track_index) {
    if (!s) return;
    if (track_index < 0 || track_index >= static_cast<int>(s->tracks.size())) {
        s->armed_track.store(-1, std::memory_order_relaxed); return;
    }
    s->armed_track.store(s->tracks[track_index]->id, std::memory_order_relaxed);
}
int session_armed_track(Session* s) {   // returns the armed track *index*, or -1
    if (!s) return -1;
    const int id = s->armed_track.load(std::memory_order_relaxed);
    if (id < 0) return -1;
    for (int i = 0; i < static_cast<int>(s->tracks.size()); ++i)
        if (s->tracks[i]->id == id) return i;
    return -1;   // armed track was deleted
}
void session_note_on(Session* s, int pitch, float vel) {
    Track* t = armed_track_ptr(s);
    if (!t || t->is_audio || pitch < 0 || pitch > 127) return;   // only monitor through an instrument track
    const double beat = s->play_beats.load(std::memory_order_relaxed);
    s->live_in.push(1, static_cast<uint8_t>(pitch), vel, beat);
    if (s->recording.load(std::memory_order_relaxed) && beat >= s->rec_capture_from) {
        std::lock_guard<std::mutex> lk(s->rec_mtx);
        s->rec_notes.push_back(RecNote{ pitch, beat, beat, vel, true });
    }
}
void session_note_off(Session* s, int pitch) {
    Track* t = armed_track_ptr(s);
    if (!t || t->is_audio || pitch < 0 || pitch > 127) return;
    const double beat = s->play_beats.load(std::memory_order_relaxed);
    s->live_in.push(0, static_cast<uint8_t>(pitch), 0.f, beat);
    if (s->recording.load(std::memory_order_relaxed)) {
        std::lock_guard<std::mutex> lk(s->rec_mtx);
        // Close the most recent still-open note of this pitch.
        for (auto it = s->rec_notes.rbegin(); it != s->rec_notes.rend(); ++it)
            if (it->open && it->pitch == pitch) { it->beat_off = beat; it->open = false; break; }
    }
}

// Editor keyboard audition (M2-followup): play a note on a specific track's instrument,
// independent of the armed track. On/off are paired by pitch inside the track's queue.
void session_preview_note(Session* s, int track, int pitch, float vel) {
    if (!s || track < 0 || track >= static_cast<int>(s->tracks.size())) return;
    if (s->tracks[track]->is_audio || pitch < 0 || pitch > 127) return;
    s->tracks[track]->preview_in.push(1, static_cast<uint8_t>(pitch), vel, 0.0);
}
void session_preview_off(Session* s, int track, int pitch) {
    if (!s || track < 0 || track >= static_cast<int>(s->tracks.size())) return;
    if (s->tracks[track]->is_audio || pitch < 0 || pitch > 127) return;
    s->tracks[track]->preview_in.push(0, static_cast<uint8_t>(pitch), 0.f, 0.0);
}

// Recording (M6.3). Start snaps the capture origin (optionally after a count-in of
// `count_in_beats`); stop closes any held notes, maps captures to clip-local beats
// (fmod by the clip length), and overdubs them into the armed track's active clip.
static void commit_recording(Session* s);
void session_set_recording(Session* s, bool on, double count_in_beats) {
    if (!s) return;
    if (on) {
        std::lock_guard<std::mutex> lk(s->rec_mtx);
        s->rec_notes.clear();
        const double now = s->play_beats.load(std::memory_order_relaxed);
        s->rec_capture_from = now + (count_in_beats > 0 ? count_in_beats : 0.0);
        s->recording.store(true, std::memory_order_relaxed);
    } else {
        if (!s->recording.exchange(false, std::memory_order_relaxed)) return;
        commit_recording(s);
    }
}
int  session_is_recording(Session* s) { return (s && s->recording.load(std::memory_order_relaxed)) ? 1 : 0; }
void session_set_metronome(Session* s, int on) { if (s) s->metronome.store(on != 0, std::memory_order_relaxed); }
int  session_get_metronome(Session* s) { return (s && s->metronome.load(std::memory_order_relaxed)) ? 1 : 0; }

static void commit_recording(Session* s) {
    std::vector<RecNote> rec;
    { std::lock_guard<std::mutex> lk(s->rec_mtx);
      const double now = s->play_beats.load(std::memory_order_relaxed);
      for (auto& r : s->rec_notes) if (r.open) { r.beat_off = now; r.open = false; }  // close held notes
      rec.swap(s->rec_notes); }
    if (rec.empty()) return;
    Track* t = armed_track_ptr(s);
    if (!t || t->is_audio) return;
    const int ti = session_armed_track(s);          // armed track *index* for session_set_clip
    if (ti < 0) return;
    const int sc = t->active.load(std::memory_order_relaxed);
    if (sc < 0 || sc >= static_cast<int>(t->clips.size())) return;
    // Start from the clip's current notes (overdub) and append the captures, mapped to
    // clip-local beats. A zero/short clip defaults to a 4-beat loop.
    const MidiClip& clip = t->clips[sc];
    const double len = clip.length > 0.0 ? clip.length : 4.0;
    std::vector<ClipNote> notes = clip.notes;
    for (const RecNote& r : rec) {
        double dur = r.beat_off - r.beat_on;
        if (dur < 1.0 / 32.0) dur = 1.0 / 32.0;   // floor very short taps to a ~1/128 note
        ClipNote n{};
        n.pitch = r.pitch;
        n.start = std::fmod(r.beat_on - s->rec_capture_from, len);
        if (n.start < 0) n.start += len;
        n.dur = std::min(dur, len);   // keep a recorded note within one loop
        n.vel = r.vel;
        notes.push_back(n);
    }
    session_set_clip(s, ti, sc, notes.data(), static_cast<int>(notes.size()), len);
}
int  session_active_clip(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->active.load(std::memory_order_relaxed) : -1;
}
int  session_queued_clip(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->queued.load(std::memory_order_relaxed) : -1;
}
void session_launch_clip(Session* s, int t, int scene) {
    if (s && t >= 0 && t < static_cast<int>(s->tracks.size()) && scene >= 0 && scene < s->scenes)
        s->tracks[t]->queued.store(scene, std::memory_order_relaxed);
}
void session_launch_scene(Session* s, int scene) {
    if (!s || scene < 0 || scene >= s->scenes) return;
    for (auto& tp : s->tracks) tp->queued.store(scene, std::memory_order_relaxed);
}
float session_track_gain(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->gain.load(std::memory_order_relaxed) : 0.f;
}
void session_set_track_gain(Session* s, int t, float g) {
    if (s && t >= 0 && t < static_cast<int>(s->tracks.size())) s->tracks[t]->gain.store(g, std::memory_order_relaxed);
}
// ADR-0022 P1b.4: recompute every track's master-sum multiplier from the mute/solo state (UI thread).
// A track is audible unless it is muted, or unless SOME track is soloed and this one is not. The audio
// thread reads the resulting `mix_scale` in the master sum; at the all-default state every scale is 1.
static void recompute_mix_scales(Session* s) {
    if (!s) return;
    bool any_solo = false;
    for (auto& tp : s->tracks) if (tp->solo.load(std::memory_order_relaxed)) { any_solo = true; break; }
    for (auto& tp : s->tracks) {
        const bool muted  = tp->mute.load(std::memory_order_relaxed);
        const bool soloed = tp->solo.load(std::memory_order_relaxed);
        const bool audible = !muted && (!any_solo || soloed);
        tp->mix_scale.store(audible ? 1.f : 0.f, std::memory_order_relaxed);
    }
}
bool session_track_mute(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) && s->tracks[t]->mute.load(std::memory_order_relaxed);
}
void session_set_track_mute(Session* s, int t, bool m) {
    if (s && t >= 0 && t < static_cast<int>(s->tracks.size())) { s->tracks[t]->mute.store(m, std::memory_order_relaxed); recompute_mix_scales(s); }
}
bool session_track_solo(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) && s->tracks[t]->solo.load(std::memory_order_relaxed);
}
void session_set_track_solo(Session* s, int t, bool so) {
    if (s && t >= 0 && t < static_cast<int>(s->tracks.size())) { s->tracks[t]->solo.store(so, std::memory_order_relaxed); recompute_mix_scales(s); }
}
// ADR-0022 P1b: the master node's gain + meters (the session's single sink).
float session_master_gain(Session* s) { return s ? s->master.gain.load(std::memory_order_relaxed) : 1.f; }
void  session_set_master_gain(Session* s, float g) { if (s) s->master.gain.store(std::max(0.f, g), std::memory_order_relaxed); }
float session_master_level(Session* s) { return s ? s->master.level.load(std::memory_order_relaxed) : 0.f; }
float session_master_transient(Session* s) { return s ? s->master.transient.load(std::memory_order_relaxed) : 0.f; }
float session_master_band(Session* s, int b) {
    if (!s) return 0.f;
    switch (b) { case 0: return s->master.band_low.load(std::memory_order_relaxed);
                 case 1: return s->master.band_mid.load(std::memory_order_relaxed);
                 case 2: return s->master.band_high.load(std::memory_order_relaxed);
                 default: return 0.f; }
}
float session_track_level(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->level.load(std::memory_order_relaxed) : 0.f;
}
float session_track_transient(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->transient.load(std::memory_order_relaxed) : 0.f;
}
float session_track_band(Session* s, int t, int band) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0.f;
    Track& tr = *s->tracks[t];
    return band == 0 ? tr.band_low.load(std::memory_order_relaxed)
         : band == 1 ? tr.band_mid.load(std::memory_order_relaxed)
                     : tr.band_high.load(std::memory_order_relaxed);
}
void* session_track_controller(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size()) && s->tracks[t]->handle) ? s->tracks[t]->handle->controller : nullptr;
}
bool session_track_is_audio(Session* s, int t) {
    return s && t >= 0 && t < static_cast<int>(s->tracks.size()) && s->tracks[t]->is_audio;
}
std::string session_get_track_state(Session* s, int t) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return {};
    if (s->tracks[t]->clap_inst) return clap_save_state(s->tracks[t]->clap_inst);   // CLAP instrument state
    if (s->tracks[t]->handle)    return vst3_save_state(s->tracks[t]->handle);
    return {};
}
void session_set_track_state(Session* s, int t, const std::string& state) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || state.empty()) return;
    if (s->tracks[t]->clap_inst) { clap_load_state(s->tracks[t]->clap_inst, state); return; }
    if (s->tracks[t]->handle)    vst3_load_state(s->tracks[t]->handle, state);
}
// CLAP instrument/effect identity + state, for project persistence (save the path + state; load
// recreates the plugin then restores its state).
const char* session_track_clap_instrument_path(Session* s, int t) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !s->tracks[t]->clap_inst) return "";
    return s->tracks[t]->clap_inst->bundle_path.c_str();
}
int session_track_clap_effect_count(Session* s, int t) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0;
    return static_cast<int>(s->tracks[t]->clap_effects.size());
}
const char* session_track_clap_effect_path(Session* s, int t, int i) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return "";
    const auto& e = s->tracks[t]->clap_effects;
    return (i >= 0 && i < static_cast<int>(e.size())) ? e[i]->bundle_path.c_str() : "";
}
std::string session_get_track_clap_effect_state(Session* s, int t, int i) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return {};
    const auto& e = s->tracks[t]->clap_effects;
    return (i >= 0 && i < static_cast<int>(e.size())) ? clap_save_state(e[i]) : std::string{};
}

// --- Preset browse / load for a track's instrument (generic; no per-plugin code). ---
// Scan the instrument's presets into the track cache. CLAP: the plugin's preset-discovery
// factory. VST3: `.vstpreset` files + native-format adapters (Serum/Pigments). Returns the count.
// Each entry carries {name, id, category, tags[], loadable} read by the accessors below.
int session_track_preset_scan(Session* s, int t, const char* filter) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0;
    Track& tr = *s->tracks[t];
    tr.preset_cache.clear();
    if (tr.clap_inst) {
        std::vector<ClapPresetInfo> pl;
        clap_list_presets(tr.clap_inst, pl, filter ? filter : "");
        tr.preset_cache.reserve(pl.size());
        for (auto& p : pl) { PresetEntry e; e.name = std::move(p.name); e.id = std::move(p.id);
                             e.source = "clap"; e.loadable = true; tr.preset_cache.push_back(std::move(e)); }
    } else if (tr.handle) {
        vst3_scan_presets(tr.handle, filter, tr.preset_cache);
    }
    return static_cast<int>(tr.preset_cache.size());
}
static const PresetEntry* preset_at(Session* s, int t, int i) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return nullptr;
    const auto& c = s->tracks[t]->preset_cache;
    return (i >= 0 && i < static_cast<int>(c.size())) ? &c[i] : nullptr;
}
int session_track_preset_count(Session* s, int t) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0;
    return static_cast<int>(s->tracks[t]->preset_cache.size());
}
const char* session_track_preset_name(Session* s, int t, int i)     { const PresetEntry* e = preset_at(s, t, i); return e ? e->name.c_str() : ""; }
const char* session_track_preset_id(Session* s, int t, int i)       { const PresetEntry* e = preset_at(s, t, i); return e ? e->id.c_str() : ""; }
const char* session_track_preset_category(Session* s, int t, int i) { const PresetEntry* e = preset_at(s, t, i); return e ? e->category.c_str() : ""; }
int         session_track_preset_loadable(Session* s, int t, int i) { const PresetEntry* e = preset_at(s, t, i); return (e && e->loadable) ? 1 : 0; }
int         session_track_preset_tag_count(Session* s, int t, int i){ const PresetEntry* e = preset_at(s, t, i); return e ? static_cast<int>(e->tags.size()) : 0; }
const char* session_track_preset_tag(Session* s, int t, int i, int k) {
    const PresetEntry* e = preset_at(s, t, i);
    return (e && k >= 0 && k < static_cast<int>(e->tags.size())) ? e->tags[k].c_str() : "";
}
// Load a preset by its id (from the scan). CLAP: preset-load ext. VST3: `.vstpreset` container or
// an adapter-owned native file -> setState. Returns true on success (browse-only presets => false).
bool session_track_preset_load(Session* s, int t, const char* id) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !id) return false;
    Track& tr = *s->tracks[t];
    if (tr.clap_inst) return clap_load_preset(tr.clap_inst, id);
    if (tr.handle)    return vst3_load_preset(tr.handle, id);
    return false;
}
int session_audio_clip_bpm(Session* s, int t, int sc) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0;
    Track& tr = *s->tracks[t];
    if (!tr.is_audio || sc < 0 || sc >= static_cast<int>(tr.aud_clips.size())) return 0;
    return static_cast<int>(std::lround(tr.aud_clips[sc].src_bpm));
}
static bool aud_valid(Session* s, int t, int sc) {
    return s && t >= 0 && t < static_cast<int>(s->tracks.size()) && s->tracks[t]->is_audio
           && sc >= 0 && sc < static_cast<int>(s->tracks[t]->aud_clips.size());
}
int session_audio_waveform(Session* s, int t, int sc, float* out, int n) {
    if (!aud_valid(s, t, sc) || !out || n <= 0) return 0;
    const Sampler& smp = s->tracks[t]->aud_clips[sc];
    if (!smp.ok()) return 0;
    const size_t N = smp.L.size();
    for (int i = 0; i < n; ++i) {
        const size_t a = N * static_cast<size_t>(i) / n, b = N * static_cast<size_t>(i + 1) / n;
        float peak = 0.f;
        for (size_t j = a; j < b && j < N; ++j) peak = std::max(peak, std::fabs(smp.L[j]));
        out[i] = peak;
    }
    return n;
}
double session_audio_loop_beats(Session* s, int t, int sc) {
    if (!aud_valid(s, t, sc)) return 4.0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    return s->tracks[t]->aud_clips[sc].loop_beats;
}
void session_get_audio_trim(Session* s, int t, int sc, float* t0, float* t1) {
    if (!aud_valid(s, t, sc)) { if (t0) *t0 = 0.f; if (t1) *t1 = 1.f; return; }
    if (t0) *t0 = s->tracks[t]->aud_trim0[sc].load(std::memory_order_relaxed);
    if (t1) *t1 = s->tracks[t]->aud_trim1[sc].load(std::memory_order_relaxed);
}
void session_set_audio_trim(Session* s, int t, int sc, float t0, float t1) {
    if (!aud_valid(s, t, sc)) return;
    s->tracks[t]->aud_trim0[sc].store(std::min(std::max(t0, 0.f), 1.f), std::memory_order_relaxed);
    s->tracks[t]->aud_trim1[sc].store(std::min(std::max(t1, 0.f), 1.f), std::memory_order_relaxed);
}

// --- audio-clip warp/shaping (A2) — UI/main thread; writes are guarded by aud_mtx so the
// audio thread reads a consistent clip. Enabling warp builds+inits the stretcher OFF the
// lock (heavy) and swaps it in under the lock (short critical section). ---
void session_set_audio_warp(Session* s, int t, int sc, int enabled, int mode) {
    if (!aud_valid(s, t, sc)) return;
    Track& tr = *s->tracks[t];
    std::unique_ptr<ClipDsp> fresh;
    if (enabled) { fresh = std::make_unique<ClipDsp>(); fresh->init(s->sample_rate > 0 ? s->sample_rate : 48000); }
    std::lock_guard<std::mutex> lk(tr.aud_mtx);
    if (tr.aud_dsp.size() < tr.aud_clips.size()) tr.aud_dsp.resize(tr.aud_clips.size());
    tr.aud_clips[sc].warp_enabled = enabled != 0;
    tr.aud_clips[sc].warp_mode = static_cast<WarpMode>(std::clamp(mode, 0, 2));
    if (enabled) tr.aud_dsp[sc] = std::move(fresh);
}
int session_get_audio_warp(Session* s, int t, int sc) {
    if (!aud_valid(s, t, sc)) return -1;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    const auto& c = s->tracks[t]->aud_clips[sc];
    return c.warp_enabled ? static_cast<int>(c.warp_mode) : -1;
}
void session_set_audio_pitch(Session* s, int t, int sc, float semitones) {
    if (!aud_valid(s, t, sc)) return;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    s->tracks[t]->aud_clips[sc].pitch_semitones = std::clamp(semitones, -48.f, 48.f);
}
float session_get_audio_pitch(Session* s, int t, int sc) {
    if (!aud_valid(s, t, sc)) return 0.f;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    return s->tracks[t]->aud_clips[sc].pitch_semitones;
}
void session_set_audio_gain(Session* s, int t, int sc, float gain) {
    if (!aud_valid(s, t, sc)) return;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    s->tracks[t]->aud_clips[sc].gain = std::clamp(gain, 0.f, 4.f);
}
float session_get_audio_gain(Session* s, int t, int sc) {
    if (!aud_valid(s, t, sc)) return 1.f;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    return s->tracks[t]->aud_clips[sc].gain;
}
// Persistence: the loop's source WAV path + tempo (empty path = a generated loop, not persisted).
// Read on the UI thread; src_path is only ever written on the UI thread (sampler_load_wav / the swap
// below), so no lock is needed for the read.
const char* session_get_audio_path(Session* s, int t, int sc) {
    return aud_valid(s, t, sc) ? s->tracks[t]->aud_clips[sc].src_path.c_str() : "";
}
double session_get_audio_src_bpm(Session* s, int t, int sc) {
    return aud_valid(s, t, sc) ? s->tracks[t]->aud_clips[sc].src_bpm : 0.0;
}
// Reload a loop from disk into (track, scene) — decode on the UI thread, then swap the clip under
// aud_mtx (the RT-safe pattern from session_pool_place_audio). Used by session load to restore loops.
bool session_load_audio_clip(Session* s, int t, int sc, const char* path, double src_bpm) {
    if (!aud_valid(s, t, sc) || !path || !*path) return false;
    Sampler smp;
    if (!sampler_load_wav(path, s->sample_rate, src_bpm > 0.0 ? src_bpm : 120.0, smp)) return false;
    Track& tr = *s->tracks[t];
    { std::lock_guard<std::mutex> lk(tr.aud_mtx); tr.aud_clips[sc] = std::move(smp); }
    return true;
}
void session_set_audio_reverse(Session* s, int t, int sc, int on) {
    if (!aud_valid(s, t, sc)) return;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    s->tracks[t]->aud_clips[sc].reverse = on != 0;
}
int session_get_audio_reverse(Session* s, int t, int sc) {
    if (!aud_valid(s, t, sc)) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    return s->tracks[t]->aud_clips[sc].reverse ? 1 : 0;
}
void session_set_audio_fades(Session* s, int t, int sc, float in_ms, float out_ms, float xfade_ms) {
    if (!aud_valid(s, t, sc)) return;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    auto& c = s->tracks[t]->aud_clips[sc];
    c.fade_in_ms = std::max(0.f, in_ms); c.fade_out_ms = std::max(0.f, out_ms); c.loop_crossfade_ms = std::max(0.f, xfade_ms);
}
void session_get_audio_fades(Session* s, int t, int sc, float* in_ms, float* out_ms, float* xfade_ms) {
    if (!aud_valid(s, t, sc)) { if (in_ms) *in_ms = 0; if (out_ms) *out_ms = 0; if (xfade_ms) *xfade_ms = 0; return; }
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    const auto& c = s->tracks[t]->aud_clips[sc];
    if (in_ms) *in_ms = c.fade_in_ms; if (out_ms) *out_ms = c.fade_out_ms; if (xfade_ms) *xfade_ms = c.loop_crossfade_ms;
}
int session_audio_auto_warp(Session* s, int t, int sc, float sensitivity) {
    if (!aud_valid(s, t, sc)) return 0;
    Track& tr = *s->tracks[t];
    auto fresh = std::make_unique<ClipDsp>();          // build the stretcher off the lock
    fresh->init(s->sample_rate > 0 ? s->sample_rate : 48000);
    std::lock_guard<std::mutex> lk(tr.aud_mtx);
    auto& c = tr.aud_clips[sc];
    if (c.L.empty()) return 0;
    const uint32_t sr = c.sr ? c.sr : 48000;
    c.transients  = audio_clip_ed::detect_transients(c.L, c.R.empty() ? c.L : c.R, sr, sensitivity);
    const double bpm = c.src_bpm > 0 ? c.src_bpm : audio_clip_ed::estimate_bpm(c.transients, sr);
    c.warp_points = audio_clip_ed::auto_warp(c.transients, static_cast<uint32_t>(c.L.size()), sr, bpm);
    c.warp_enabled = true; c.warp_mode = WarpMode::Complex;
    if (tr.aud_dsp.size() < tr.aud_clips.size()) tr.aud_dsp.resize(tr.aud_clips.size());
    tr.aud_dsp[sc] = std::move(fresh);
    return static_cast<int>(c.warp_points.size());
}
int session_audio_get_warp_pts(Session* s, int t, int sc, float* out, int cap) {
    if (!aud_valid(s, t, sc) || !out || cap <= 0) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    const auto& c = s->tracks[t]->aud_clips[sc];
    const double N = c.L.empty() ? 1.0 : static_cast<double>(c.L.size());
    const int n = std::min(cap, static_cast<int>(c.warp_points.size()));
    for (int i = 0; i < n; ++i) out[i] = static_cast<float>(c.warp_points[i].source_sample / N);
    return n;
}
int session_audio_get_transients(Session* s, int t, int sc, float* out, int cap) {
    if (!aud_valid(s, t, sc) || !out || cap <= 0) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    const auto& c = s->tracks[t]->aud_clips[sc];
    const double N = c.L.empty() ? 1.0 : static_cast<double>(c.L.size());
    const int n = std::min(cap, static_cast<int>(c.transients.size()));
    for (int i = 0; i < n; ++i) out[i] = static_cast<float>(c.transients[i].source_sample / N);
    return n;
}
int session_audio_get_warp_beats(Session* s, int t, int sc, double* out, int cap) {
    if (!aud_valid(s, t, sc) || !out || cap <= 0) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    const auto& c = s->tracks[t]->aud_clips[sc];
    const int n = std::min(cap, static_cast<int>(c.warp_points.size()));
    for (int i = 0; i < n; ++i) out[i] = c.warp_points[i].beat;
    return n;
}
void session_audio_clear_warp(Session* s, int t, int sc) {
    if (!aud_valid(s, t, sc)) return;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    auto& c = s->tracks[t]->aud_clips[sc];
    c.warp_points.clear(); c.warp_enabled = false;
}
void session_audio_set_warp_pts(Session* s, int t, int sc, const float* norm, const double* beats, int n) {
    if (!aud_valid(s, t, sc) || n < 0) return;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    auto& c = s->tracks[t]->aud_clips[sc];
    if (c.L.empty()) return;
    const double N = static_cast<double>(c.L.size());
    std::vector<audio_clip_ed::WarpPoint> pts;
    for (int i = 0; i < n; ++i)
        pts.push_back({ static_cast<uint32_t>(std::clamp(norm[i] * N, 0.0, N - 1.0)), beats[i] });
    c.warp_points = audio_clip_ed::sanitize_warp_points(std::move(pts));   // sorts by sample, monotone beats
    c.warp_enabled = c.warp_enabled || !c.warp_points.empty();
}
int session_audio_slices(Session* s, int t, int sc, int mode, float* out, int cap) {
    if (!aud_valid(s, t, sc) || !out || cap <= 0) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    const auto& c = s->tracks[t]->aud_clips[sc];
    if (c.L.empty()) return 0;
    const uint32_t N = static_cast<uint32_t>(c.L.size());
    const auto slices = audio_clip_ed::compile_slices(mode, c.transients, {}, 0, N);
    const int n = std::min(cap, static_cast<int>(slices.size()));
    for (int i = 0; i < n; ++i) out[i] = static_cast<float>(slices[i].start) / N;   // slice start positions
    return n;
}

static bool clip_valid(Session* s, int t, int sc) {
    return s && t >= 0 && t < static_cast<int>(s->tracks.size())
           && sc >= 0 && sc < static_cast<int>(s->tracks[t]->edit_clips.size());
}
int session_clip_note_count(Session* s, int t, int sc) {
    if (!clip_valid(s, t, sc)) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->edit_mtx);
    return static_cast<int>(s->tracks[t]->edit_clips[sc].notes.size());
}
int session_get_clip(Session* s, int t, int sc, ClipNote* out, int max) {
    if (!clip_valid(s, t, sc) || !out || max <= 0) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->edit_mtx);
    const auto& notes = s->tracks[t]->edit_clips[sc].notes;
    const int n = std::min(static_cast<int>(notes.size()), max);
    for (int i = 0; i < n; ++i) out[i] = notes[i];
    return n;
}
double session_clip_length(Session* s, int t, int sc) {
    if (!clip_valid(s, t, sc)) return 0.0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->edit_mtx);
    return s->tracks[t]->edit_clips[sc].length;
}
void session_set_clip(Session* s, int t, int sc, const ClipNote* notes, int n, double length) {
    if (!clip_valid(s, t, sc)) return;
    Track& tr = *s->tracks[t];
    {
        std::lock_guard<std::mutex> lk(tr.edit_mtx);
        tr.edit_clips[sc].notes.assign(notes, notes + (n > 0 ? n : 0));
        tr.edit_clips[sc].length = length > 0 ? length : tr.edit_clips[sc].length;
    }
    tr.edit_gen.fetch_add(1, std::memory_order_release);
}

// In-clip loop region (M2-followup). loop_end <= loop_start disables it (whole-clip loop).
void session_set_clip_loop(Session* s, int t, int sc, double loop_start, double loop_end) {
    if (!clip_valid(s, t, sc)) return;
    Track& tr = *s->tracks[t];
    { std::lock_guard<std::mutex> lk(tr.edit_mtx);
      tr.edit_clips[sc].loop_start = loop_start; tr.edit_clips[sc].loop_end = loop_end; }
    tr.edit_gen.fetch_add(1, std::memory_order_release);
}
void session_get_clip_loop(Session* s, int t, int sc, double* loop_start, double* loop_end) {
    if (!clip_valid(s, t, sc)) { if (loop_start) *loop_start = 0; if (loop_end) *loop_end = 0; return; }
    const MidiClip& c = s->tracks[t]->edit_clips[sc];
    if (loop_start) *loop_start = c.loop_start;
    if (loop_end)   *loop_end   = c.loop_end;
}

// --- Clip pool (loose clips outside the grid) — UI/main thread only. ---
static bool pool_valid(Session* s, int i) { return s && i >= 0 && i < static_cast<int>(s->pool.size()); }
int session_pool_count(Session* s) { return s ? static_cast<int>(s->pool.size()) : 0; }
double session_pool_length(Session* s, int i) {
    if (!pool_valid(s, i)) return 0.0;
    return s->pool[i].is_audio ? s->pool[i].audio.loop_beats : s->pool[i].clip.length;
}
const char* session_pool_name(Session* s, int i) { return pool_valid(s, i) ? s->pool[i].name.c_str() : ""; }
int session_pool_get(Session* s, int i, ClipNote* out, int max) {
    if (!pool_valid(s, i) || !out || max <= 0) return 0;
    const auto& notes = s->pool[i].clip.notes;
    const int n = std::min(static_cast<int>(notes.size()), max);
    for (int k = 0; k < n; ++k) out[k] = notes[k];
    return n;
}
int session_pool_add(Session* s, const ClipNote* notes, int n, double length, const char* name) {
    if (!s) return -1;
    PoolClip pc;
    if (notes && n > 0) pc.clip.notes.assign(notes, notes + n);
    pc.clip.length = length > 0 ? length : 4.0;
    pc.name = name ? name : "";
    s->pool.push_back(std::move(pc));
    return static_cast<int>(s->pool.size()) - 1;
}
void session_pool_remove(Session* s, int i) { if (pool_valid(s, i)) s->pool.erase(s->pool.begin() + i); }
void session_pool_clear(Session* s) { if (s) s->pool.clear(); }

// --- Audio clips in the pool (Samplers). Mirrors the MIDI pool; stash = MOVE. ---
static int sampler_waveform(const Sampler& smp, float* out, int n) {
    if (!smp.ok() || !out || n <= 0) return 0;
    const size_t N = smp.L.size();
    for (int i = 0; i < n; ++i) {
        const size_t a = N * static_cast<size_t>(i) / n, b = N * static_cast<size_t>(i + 1) / n;
        float peak = 0.f;
        for (size_t j = a; j < b && j < N; ++j) peak = std::max(peak, std::fabs(smp.L[j]));
        out[i] = peak;
    }
    return n;
}
bool session_pool_is_audio(Session* s, int i) { return pool_valid(s, i) && s->pool[i].is_audio; }
int  session_pool_audio_bpm(Session* s, int i) {
    return (pool_valid(s, i) && s->pool[i].is_audio) ? static_cast<int>(std::lround(s->pool[i].audio.src_bpm)) : 0;
}
int  session_pool_audio_waveform(Session* s, int i, float* out, int n) {
    return (pool_valid(s, i) && s->pool[i].is_audio) ? sampler_waveform(s->pool[i].audio, out, n) : 0;
}
// MOVE an audio grid clip into the pool: the source cell is cleared (under aud_mtx so the
// audio thread never sees a torn Sampler). Returns the new pool index, or -1.
int session_pool_stash_audio(Session* s, int t, int sc, const char* name) {
    if (!aud_valid(s, t, sc)) return -1;
    Track& tr = *s->tracks[t];
    if (!tr.aud_clips[sc].ok()) return -1;   // empty cell — nothing to stash
    PoolClip pc; pc.is_audio = true;
    {
        std::lock_guard<std::mutex> lk(tr.aud_mtx);
        pc.audio = std::move(tr.aud_clips[sc]);   // O(1) move out
        tr.aud_clips[sc] = Sampler{};             // leave an empty cell
    }
    pc.name = name ? name : "";
    s->pool.push_back(std::move(pc));
    return static_cast<int>(s->pool.size()) - 1;
}
// Copy a pooled audio clip into an audio grid cell (under aud_mtx). The pool keeps its copy.
bool session_pool_place_audio(Session* s, int i, int t, int sc) {
    if (!pool_valid(s, i) || !s->pool[i].is_audio || !aud_valid(s, t, sc)) return false;
    Track& tr = *s->tracks[t];
    Sampler copy = s->pool[i].audio;   // copy the PCM on the UI thread before locking
    {
        std::lock_guard<std::mutex> lk(tr.aud_mtx);
        tr.aud_clips[sc] = std::move(copy);
    }
    tr.aud_trim0[sc].store(0.f, std::memory_order_relaxed);
    tr.aud_trim1[sc].store(1.f, std::memory_order_relaxed);
    return true;
}

// Drain a device's pending UI parameter changes into its ParamChanges block.
static void drain_params(Vst3Handle* h, Vst3ParamChanges& pc) {
    ParamMsg m;
    while (h->param_q.pop(m)) {
        int32 idx = 0;
        IParamValueQueue* q = pc.addParameterData(m.id, idx);
        if (q) { int32 pt = 0; q->addPoint(0, m.value, pt); }
    }
}

// Note on/off + per-note expression. Note events are added first so a same-offset
// expression for a just-started note never precedes its note-on (VST3 wants the list
// sorted; continuing-note expression is at offset 0 with its note-on in a prior block).
// Axis mapping: bend -> kTuningTypeID (±120 semis, norm = semis/240 + 0.5), timbre ->
// kBrightnessTypeID (0..1), pressure -> per-note PolyPressureEvent (0..1).
static void emit_vst3(Vst3EventList& events, const std::vector<NoteEvent>& nev,
                      const std::vector<ExprEvent>& eev) {
    for (const NoteEvent& ne : nev) {
        Event e{};
        e.sampleOffset = static_cast<int32>(ne.sample_offset);
        e.busIndex = 0;
        if (ne.on) {
            e.type = Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<int16>(ne.pitch);
            e.noteOn.velocity = ne.vel;
            e.noteOn.noteId = ne.note_id;
            e.noteOn.channel = 0;
            e.noteOn.tuning = ne.tuning;   // semitone offset for a click-free bent start
        } else {
            e.type = Event::kNoteOffEvent;
            e.noteOff.pitch = static_cast<int16>(ne.pitch);
            e.noteOff.velocity = 0.f;
            e.noteOff.noteId = ne.note_id;
            e.noteOff.channel = 0;
        }
        events.addEvent(e);
    }
    for (const ExprEvent& xe : eev) {
        Event e{};
        e.sampleOffset = static_cast<int32>(xe.sample_offset);
        e.busIndex = 0;
        if (xe.axis == vivid::session::AXIS_PRESSURE) {
            e.type = Event::kPolyPressureEvent;
            e.polyPressure.channel = 0;
            e.polyPressure.pitch = static_cast<int16>(xe.pitch);
            e.polyPressure.pressure = std::clamp(xe.value, 0.f, 1.f);
            e.polyPressure.noteId = xe.note_id;
        } else {
            e.type = Event::kNoteExpressionValueEvent;
            e.noteExpressionValue.noteId = xe.note_id;
            if (xe.axis == vivid::session::AXIS_BEND) {
                e.noteExpressionValue.typeId = kTuningTypeID;
                e.noteExpressionValue.value = std::clamp(xe.value / 240.0 + 0.5, 0.0, 1.0);
            } else {  // AXIS_TIMBRE
                e.noteExpressionValue.typeId = kBrightnessTypeID;
                e.noteExpressionValue.value = std::clamp(static_cast<double>(xe.value), 0.0, 1.0);
            }
        }
        events.addEvent(e);
    }
}

// --- Per-source/effect render primitives (AG-0). Extracted from session_process so BOTH the inline
// path and (later) the audio-graph node dispatch call identical code — parity by construction. All
// are RT-safe: fixed-capacity VST3 scratch, no heap, the sampler try_lock skip preserved. ---

// Copy only the note/expr events whose pitch is within [lo,hi] into dst — the key-range router.
// RT-safe: dst is pre-reserved (reserve_track_graph). note-on and note-off both carry pitch, so a
// filtered-in note's off is filtered in too — on/off pairs stay balanced (no stuck notes).
static inline void filter_notes_by_range(const std::vector<NoteEvent>& src, uint8_t lo, uint8_t hi,
                                         std::vector<NoteEvent>& dst) {
    dst.clear();
    for (const NoteEvent& n : src) if (n.pitch >= lo && n.pitch <= hi) dst.push_back(n);
}
static inline void filter_expr_by_range(const std::vector<ExprEvent>& src, uint8_t lo, uint8_t hi,
                                        std::vector<ExprEvent>& dst) {
    dst.clear();
    for (const ExprEvent& x : src) if (x.pitch >= lo && x.pitch <= hi) dst.push_back(x);
}

// VST3 instrument source. Runs the processor into L/R using the caller-supplied `events` list.
// For a full-range source the caller passes t.vev (primed with scene-switch releases + this block's
// notes), keeping behavior identical; for a key-split source it passes a filtered per-source list.
static void render_vst3_instrument(Track& t, Vst3Handle* h, Vst3EventList& events,
                                   const VividAudioContext& ctx, uint32_t frames, float* L, float* R) {
    float* ch[2] = { L, R };
    AudioBusBuffers ob{}; ob.channelBuffers32 = ch; ob.numChannels = 2; ob.silenceFlags = 0;
    Vst3ParamChanges pc; pc.clear();
    drain_params(h, pc);
    ProcessContext pctx = vst3_build_process_context(&ctx, t.steady);
    ProcessData data{};
    data.processMode = kRealtime; data.symbolicSampleSize = kSample32;
    data.numSamples = static_cast<int32>(frames); data.numInputs = 0; data.numOutputs = 1;
    data.inputs = nullptr; data.outputs = &ob;
    data.inputEvents = &events; data.inputParameterChanges = &pc; data.processContext = &pctx;
    // ADR-0015 (M3): give a note-GENERATING plugin (a chord generator / arpeggiator) somewhere to
    // put the notes it makes. Before this the host never assigned data.outputEvents at all, so every
    // note such a plugin produced was silently discarded.
    if (h->has_note_out) { h->out_events.clear(); data.outputEvents = &h->out_events; }
    h->processor->process(data);
}

// Drain the notes a VST3 plugin GENERATED this block into `out` (ADR-0015 / M3). RT-safe: fixed
// capacity, no allocation. Only note-on/off are taken — the note stream is what the graph carries.
static void drain_vst3_notes(Vst3Handle* h, std::vector<NoteEvent>& out) {
    out.clear();
    if (!h || !h->has_note_out) return;
    const int32 n = h->out_events.getEventCount();
    for (int32 i = 0; i < n; ++i) {
        Event e{};
        if (h->out_events.getEvent(i, e) != kResultOk) continue;
        if (out.size() >= kGraphMaxNotes) break;   // truncate rather than allocate
        if (e.type == Event::kNoteOnEvent) {
            out.push_back(NoteEvent{ static_cast<uint32_t>(e.sampleOffset), true, e.noteOn.pitch,
                                     e.noteOn.velocity, e.noteOn.noteId, e.noteOn.tuning });
        } else if (e.type == Event::kNoteOffEvent) {
            out.push_back(NoteEvent{ static_cast<uint32_t>(e.sampleOffset), false, e.noteOff.pitch,
                                     e.noteOff.velocity, e.noteOff.noteId, 0.f });
        }
    }
}

// VST3 effect. Transforms L/R in place, using the track's fx scratch (t.fxl/t.fxr) as the plugin's
// output bus, then copies back. Caller guards `fx && fx->processing`.
static void render_vst3_effect(Track& t, Vst3Handle* fx, const VividAudioContext& ctx,
                               uint32_t frames, float* L, float* R) {
    if (t.fxl.size() < frames) { t.fxl.resize(frames); t.fxr.resize(frames); }
    float* oL = t.fxl.data(); float* oR = t.fxr.data();
    float* inCh[2] = { L, R }; float* outCh[2] = { oL, oR };
    AudioBusBuffers ib{}; ib.channelBuffers32 = inCh;  ib.numChannels = 2; ib.silenceFlags = 0;
    AudioBusBuffers fob{}; fob.channelBuffers32 = outCh; fob.numChannels = 2; fob.silenceFlags = 0;
    Vst3ParamChanges fpc; fpc.clear();
    drain_params(fx, fpc);
    ProcessContext fpctx = vst3_build_process_context(&ctx, t.steady);
    ProcessData fd{};
    fd.processMode = kRealtime; fd.symbolicSampleSize = kSample32;
    fd.numSamples = static_cast<int32>(frames);
    fd.numInputs = 1; fd.inputs = &ib;
    fd.numOutputs = 1; fd.outputs = &fob;
    fd.inputEvents = nullptr; fd.inputParameterChanges = &fpc; fd.processContext = &fpctx;
    fx->processor->process(fd);
    std::memcpy(L, oL, frames * sizeof(float));
    std::memcpy(R, oR, frames * sizeof(float));
}

// CLAP instrument. Builds this block's note + param events into the handle's scratch, then
// processes into L/R (silent input fed to any declared input port). Plays `notes` (t.nev for a
// full-range source, or a key-range-filtered list) plus this block's scene-switch note-offs.
// RT-safe (fixed scratch, no alloc/lock).
static void render_clap_instrument(Track& t, ClapHandle* h, const std::vector<NoteEvent>& notes,
                                   uint32_t frames, float* L, float* R) {
    h->events.clear();
    clap_flush_params(h);
    for (const NoteEvent& ne : t.scene_rel)   // scene-switch note-offs first, so held voices release
        h->events.add_note(ne.on, ne.pitch, ne.vel, ne.note_id, ne.sample_offset);
    for (const NoteEvent& ne : notes)         // this source's notes (full range = t.nev; key-split = filtered)
        h->events.add_note(ne.on, ne.pitch, ne.vel, ne.note_id, ne.sample_offset);
    float* out[2] = { L, R };
    float* in[2]  = { h->silence.data(), h->silence.data() + h->max_block };
    clap_run(h, static_cast<int64_t>(t.steady), frames, h->audio_in > 0 ? in : nullptr, 2, out, 2);
}

// CLAP effect. Transforms L/R in place via the track's fx scratch (t.fxl/t.fxr), like the VST3
// effect path. Caller guards `clap && clap->processing`.
static void render_clap_effect(Track& t, ClapHandle* h, uint32_t frames, float* L, float* R) {
    if (t.fxl.size() < frames) { t.fxl.resize(frames); t.fxr.resize(frames); }
    h->events.clear();
    clap_flush_params(h);
    float* in[2]  = { L, R };
    float* out[2] = { t.fxl.data(), t.fxr.data() };
    clap_run(h, static_cast<int64_t>(t.steady), frames, in, 2, out, 2);
    std::memcpy(L, out[0], frames * sizeof(float));
    std::memcpy(R, out[1], frames * sizeof(float));
}

// Sampler-loop source: render the active-scene clip into L/R (silence if not playing / no clip /
// contended). Re-reads the scene-dependent state (t.active / aud_clips / aud_dsp / aud_trim*) and
// keeps the aud_mtx try_lock skip-on-contention — the caller performs the bar-quantized scene switch
// BEFORE calling this (it mutates t.active, a transport action, not a node op).
static void render_sampler_block(Track& t, double beats, double delta, uint32_t frames,
                                 uint32_t sample_rate, bool playing, float* L, float* R) {
    const int sc = t.active.load(std::memory_order_relaxed);
    if (playing && sc >= 0 && t.aud_mtx.try_lock()) {
        if (sc < static_cast<int>(t.aud_clips.size()) && t.aud_clips[sc].ok()) {
            const float tr0 = t.aud_trim0[sc].load(std::memory_order_relaxed);
            const float tr1 = t.aud_trim1[sc].load(std::memory_order_relaxed);
            ClipDsp* d = (sc < static_cast<int>(t.aud_dsp.size())) ? t.aud_dsp[sc].get() : nullptr;
            if (d && d->ready)  // warp enabled + stretcher ready -> pitch-preserving path
                process_clip(t.aud_clips[sc], *d, beats, delta, frames, sample_rate, L, R, tr0, tr1);
            else
                t.aud_clips[sc].render(beats, delta, frames, L, R, tr0, tr1);
        }
        t.aud_mtx.unlock();
    }
}

bool session_process(Session* s, float* out, uint32_t frames, uint32_t sample_rate,
                     double bpm, double beats, uint32_t beats_per_bar,
                     bool playing, bool release_all) {
    if (!s) return false;
    std::memset(out, 0, sizeof(float) * 2 * frames);
    // ADR-0022 P1b.3a: the whole engine is sized to kGraphMaxBlock (the node pool, the control pool,
    // and now the session track-output pool), and run_track_graph bails on a larger block. Guard the
    // top-level entry too: an oversized block renders silence (already memset above) rather than
    // overflow a track slot. macOS CoreAudio never exceeds this, so normal operation is untouched.
    if (frames > kGraphMaxBlock) return true;
    s->play_beats.store(beats, std::memory_order_relaxed);   // publish the clock for live-input stamping (M6)

    // Refresh the audio-thread track view if the UI added/removed a track (cheap gen-
    // counter fast-path; the copy is into reserved capacity, so no allocation). On a
    // contended block we keep the previous view and retry next block.
    if (s->tracks_gen.load(std::memory_order_acquire) != s->tracks_gen_seen) {
        if (s->tracks_mtx.try_lock()) {
            s->tracks_view = s->tracks_pub;
            s->tracks_gen_seen = s->tracks_gen.load(std::memory_order_acquire);
            s->tracks_mtx.unlock();
        }
    }
    if (s->tracks_view.empty()) return false;
    s->render_list.clear();   // ADR-0022 P1b: rebuilt each block; the master node sums it

    const uint32_t bpb = beats_per_bar ? beats_per_bar : 4;
    const long long bar = static_cast<long long>(std::floor(beats / bpb));
    const bool new_bar = bar != s->last_bar;
    s->last_bar = bar;
    const double delta = frames * (bpm / 60.0) / (sample_rate > 0 ? sample_rate : 48000);

    bool any = false;
    for (size_t tv_i = 0; tv_i < s->tracks_view.size(); ++tv_i) {
        Track& t = *s->tracks_view[tv_i];
        // Skip a MIDI track only if it has NO source at all: no processing VST3 instrument
        // AND no native instrument operator (live or pending). A native-instrument-only track
        // (e.g. the Sampler from slice-to-MIDI) has no VST3 handle but still must run.
        if (!t.is_audio && (!t.handle || !t.handle->processing) && !t.op_instrument && !t.op_instrument_edit && !t.clap_inst) continue;
        any = true;

        // Apply pending clip edits (element-wise so &clips[sc] — and the
        // scheduler's clip pointer — stay valid). Only runs after a user edit.
        if (t.edit_gen.load(std::memory_order_acquire) != t.edit_gen_seen) {
            if (t.edit_mtx.try_lock()) {
                // A scene may have been appended to edit_clips (session_add_scene). Grow the
                // audio-owned clips to match — reserved to kMaxScenes, so this append never
                // reallocates and the scheduler's &clips[q] pointers stay valid.
                while (t.clips.size() < t.edit_clips.size()) t.clips.push_back(t.edit_clips[t.clips.size()]);
                const size_t ns = std::min(t.clips.size(), t.edit_clips.size());
                for (size_t sc = 0; sc < ns; ++sc) {
                    t.clips[sc].notes      = t.edit_clips[sc].notes;
                    t.clips[sc].length     = t.edit_clips[sc].length;
                    t.clips[sc].loop_start = t.edit_clips[sc].loop_start;   // in-clip loop region
                    t.clips[sc].loop_end   = t.edit_clips[sc].loop_end;
                }
                // notes[] was re-assigned (may have reallocated) — the scheduler's
                // active[].src pointers now dangle. Null them (note-offs still fire).
                t.sched.invalidate_active_src();
                t.edit_gen_seen = t.edit_gen.load(std::memory_order_acquire);
                t.edit_mtx.unlock();
            }
        }
        // Apply pending FX-chain edits (copy the UI's pointer list into the working
        // one; reserved capacity avoids a realloc). Only runs after an add/remove.
        if (t.fx_gen.load(std::memory_order_acquire) != t.fx_gen_seen) {
            if (t.fx_mtx.try_lock()) {
                t.effects = t.effects_edit;
                t.fx_gen_seen = t.fx_gen.load(std::memory_order_acquire);
                t.fx_mtx.unlock();
            }
        }
        // Apply pending native audio-operator edits (instrument slot + effect chain).
        if (t.op_fx_gen.load(std::memory_order_acquire) != t.op_fx_gen_seen) {
            if (t.op_fx_mtx.try_lock()) {
                t.op_effects = t.op_effects_edit;
                t.op_instrument = t.op_instrument_edit;
                t.op_fx_gen_seen = t.op_fx_gen.load(std::memory_order_acquire);
                t.op_fx_mtx.unlock();
            }
        }
        // ADR-0022 P1b.2: apply a pending audio-graph edit by pointer-SWAPPING the handoff buffer
        // into the working plan. The UI already copied the new plan into *_ho under gmtx
        // (publish_track_plan), so the callback only exchanges buffers — vector::swap is an O(1)
        // pointer swap (both sides reserved to kGraphMaxNodes: no alloc/free/copy on the audio
        // thread). The stale plan the swap leaves in *_ho is fully overwritten by the next publish
        // before it is ever swapped back. On try_lock contention we keep the current plan and retry
        // next block, exactly as before.
        if (t.ggen.load(std::memory_order_acquire) != t.ggen_seen) {
            if (t.gmtx.try_lock()) {
                t.gcg.steps.swap(t.gcg_ho.steps);
                std::swap(t.gcg.buf_count,  t.gcg_ho.buf_count);
                std::swap(t.gcg.output_buf, t.gcg_ho.output_buf);
                t.gbinds.swap(t.gbinds_ho);
                std::swap(t.gok, t.gok_ho);
                t.ggen_seen  = t.ggen.load(std::memory_order_acquire);
                t.gmtx.unlock();
            }
        }
        // ADR-0022 P1b.3a: render into this track's slice of the session output pool (slot = its
        // render order this block; == its future render_list index). run_track_graph writes L/R
        // through these pointers exactly as it did with the per-track bl/br buffers.
        const size_t slot = s->render_list.size();
        float* L = s->track_out_pool.data() + slot * 2 * kGraphMaxBlock;
        float* R = L + kGraphMaxBlock;
        std::memset(L, 0, frames * sizeof(float));
        std::memset(R, 0, frames * sizeof(float));

        if (t.is_audio) {
            // Bar-quantized scene switch (a transport action the Sampler graph node reads each block).
            if (new_bar) {
                const int q = t.queued.load(std::memory_order_relaxed);
                if (q >= 0 && q != t.active.load(std::memory_order_relaxed)) t.active.store(q, std::memory_order_relaxed);
                if (q >= 0) t.queued.store(-1, std::memory_order_relaxed);
            }
        } else {
            t.vev.clear();   // this block's VST3 event list (on the Track so the graph node can read it)
            t.scene_rel.clear();   // scene-switch note-offs for the CLAP path (parallel to t.vev)
            if (new_bar) {
                const int q = t.queued.load(std::memory_order_relaxed);
                if (q >= 0 && q != t.active.load(std::memory_order_relaxed) && q < static_cast<int>(t.clips.size())) {
                    t.nev.clear(); t.eev.clear(); t.sched.flush(t.nev); emit_vst3(t.vev, t.nev, t.eev);
                    t.scene_rel.assign(t.nev.begin(), t.nev.end());   // keep the releases for CLAP (t.nev is cleared below)
                    t.sched.reset(&t.clips[q]);
                    t.active.store(q, std::memory_order_relaxed);
                }
                if (q >= 0) t.queued.store(-1, std::memory_order_relaxed);
            }
            t.nev.clear(); t.eev.clear();
            if (release_all)    t.sched.flush(t.nev);                            // play->stop edge: release held notes
            else if (playing)   t.sched.emit(beats, delta, frames, t.nev, t.eev);  // paused: emit nothing (tails still ring)
            // Live MIDI monitoring (M6): the armed track drains the session live-input
            // queue into its own event stream so played/typed notes sound through its
            // instrument, whether or not the transport is running. note_id lives in the
            // reserved live range so offs match ons and never collide with clip notes.
            if (t.id == s->armed_track.load(std::memory_order_relaxed)) {
                LiveMidi::Ev le;
                while (s->live_in.pop(le))
                    t.nev.push_back(NoteEvent{ 0u, le.on != 0, le.pitch, le.vel,
                                              kLiveNoteIdBase + le.pitch, 0.f });
            }
            // Editor keyboard-audition: this track's own preview queue, sounded whatever
            // the arm state (a distinct note_id range so its offs never hit clip notes).
            { LiveMidi::Ev pe;
              while (t.preview_in.pop(pe))
                  t.nev.push_back(NoteEvent{ 0u, pe.on != 0, pe.pitch, pe.vel,
                                            kLiveNoteIdBase + 1000 + pe.pitch, 0.f }); }
            // Event prep only — the graph node renders the source; it reads t.vev / t.nev / t.eev.
        }

        // AG-0: the compiled per-track audio graph is the SOLE RT render path — it renders the source
        // (native / VST3 instrument / sampler) then the VST3 + native FX chain into L/R. A non-derivable
        // track (no source, or > kGraphMaxNodes devices) leaves L/R silent (gok=false).
        if (t.gok) {
            t.blk.frames = frames; t.blk.sample_rate = sample_rate;
            t.blk.bpm = static_cast<float>(bpm); t.blk.bpb = bpb; t.blk.beats = beats;
            t.blk.notes = t.nev.data(); t.blk.note_count = static_cast<uint32_t>(t.nev.size());
            t.blk.steady = t.steady; t.blk.delta = delta; t.blk.playing = playing;
            // ADR-0022 P2a.1: this track's region of the session control pool.
            t.blk.ctl_pool = s->ctl_pool.data();
            t.blk.ctl_base = tv_i * static_cast<size_t>(kGraphMaxNodes) * kGraphMaxBlock;
            run_track_graph(t, L, R, frames);
        }
        t.steady += frames;

        // ADR-0022 P1a: the track GAIN is applied inside the Track-Out (Output) node, so L/R already
        // hold the GAINED output (or silence for a gok=false / bailed track). ADR-0022 P1b: this loop
        // no longer sums into `out` — it only computes the per-track meters over that gained signal;
        // the MASTER node below sums every rendered track's output. (Per-track metering stays here —
        // it must cover the graph-bail and no-source cases too; it relocates into the track-out node
        // in P1b.3 when this per-track loop is replaced by the session-graph executor.)
        const float sr = static_cast<float>(sample_rate > 0 ? sample_rate : 48000);
        const float a_lo = 1.f - std::exp(-6.2832f * 200.f / sr);    // crossover @ ~200 Hz
        const float a_hi = 1.f - std::exp(-6.2832f * 2000.f / sr);   // crossover @ ~2 kHz
        double sum_sq = 0.0, slo = 0.0, smi = 0.0, shi = 0.0;
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = L[i];
            sum_sq += static_cast<double>(l) * l;
            t.flt_lo += (l - t.flt_lo) * a_lo;
            t.flt_hi += (l - t.flt_hi) * a_hi;
            const float lo = t.flt_lo, mi = t.flt_hi - t.flt_lo, hi = l - t.flt_hi;
            slo += static_cast<double>(lo) * lo; smi += static_cast<double>(mi) * mi; shi += static_cast<double>(hi) * hi;
        }
        const float inv = 1.f / (frames > 0 ? frames : 1);
        t.band_low.store(static_cast<float>(std::sqrt(slo * inv)), std::memory_order_relaxed);
        t.band_mid.store(static_cast<float>(std::sqrt(smi * inv)), std::memory_order_relaxed);
        t.band_high.store(static_cast<float>(std::sqrt(shi * inv)), std::memory_order_relaxed);
        const float rms = static_cast<float>(std::sqrt(sum_sq / (frames > 0 ? frames : 1)));
        t.level.store(rms, std::memory_order_relaxed);
        const float tr = std::max(0.f, (rms - t.tr_baseline) * 6.f);  // onset over baseline
        t.tr_baseline += (rms - t.tr_baseline) * 0.04f;
        t.transient.store(std::min(1.f, tr), std::memory_order_relaxed);
        s->render_list.push_back(&t);   // ADR-0022 P1b: this track feeds the master node
    }

    // ADR-0022 P1b: the MASTER node. Sum every rendered track's output into the master buffer
    // (`out`, already silent from the memset above), apply the master gain, then compute the master
    // meters over the result. At master gain 1.0 the sum is bit-identical to the pre-P1b inline
    // per-track accumulation (same values, same tracks_view order). The metronome click is mixed in
    // downstream (audio_callback), so — as before — it is neither gained here nor in these meters.
    {
        Master& m = s->master;
        for (size_t slot = 0; slot < s->render_list.size(); ++slot) {
            // ADR-0022 P1b.4: apply the track's solo/mute multiplier here (0 silences it in the mix;
            // its own meter, computed above, stays pre-mute). At the default 1.0 this is an IEEE
            // identity, so an all-audible session sums bit-identically to before.
            const float scale = s->render_list[slot]->mix_scale.load(std::memory_order_relaxed);
            const float* L = s->track_out_pool.data() + slot * 2 * kGraphMaxBlock;
            const float* R = L + kGraphMaxBlock;
            for (uint32_t i = 0; i < frames; ++i) { out[2 * i] += scale * L[i]; out[2 * i + 1] += scale * R[i]; }
        }
        const float mg = m.gain.load(std::memory_order_relaxed);
        const float sr = static_cast<float>(sample_rate > 0 ? sample_rate : 48000);
        const float a_lo = 1.f - std::exp(-6.2832f * 200.f / sr);
        const float a_hi = 1.f - std::exp(-6.2832f * 2000.f / sr);
        double sum_sq = 0.0, slo = 0.0, smi = 0.0, shi = 0.0;
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = out[2 * i] * mg, r = out[2 * i + 1] * mg;
            out[2 * i] = l; out[2 * i + 1] = r;
            sum_sq += static_cast<double>(l) * l;
            m.flt_lo += (l - m.flt_lo) * a_lo;
            m.flt_hi += (l - m.flt_hi) * a_hi;
            const float lo = m.flt_lo, mi = m.flt_hi - m.flt_lo, hi = l - m.flt_hi;
            slo += static_cast<double>(lo) * lo; smi += static_cast<double>(mi) * mi; shi += static_cast<double>(hi) * hi;
        }
        const float inv = 1.f / (frames > 0 ? frames : 1);
        m.band_low.store(static_cast<float>(std::sqrt(slo * inv)), std::memory_order_relaxed);
        m.band_mid.store(static_cast<float>(std::sqrt(smi * inv)), std::memory_order_relaxed);
        m.band_high.store(static_cast<float>(std::sqrt(shi * inv)), std::memory_order_relaxed);
        const float rms = static_cast<float>(std::sqrt(sum_sq / (frames > 0 ? frames : 1)));
        m.level.store(rms, std::memory_order_relaxed);
        const float tr = std::max(0.f, (rms - m.tr_baseline) * 6.f);
        m.tr_baseline += (rms - m.tr_baseline) * 0.04f;
        m.transient.store(std::min(1.f, tr), std::memory_order_relaxed);
    }
    return any;
}

static void destroy_handle(Vst3Handle* h) {
    if (!h) return;
    if (h->processing) h->processor->setProcessing(false);
    h->destroy(); delete h;
}
static void stop_clap_loader(Session* s);   // defined below (with the async-loader machinery)
void session_destroy(Session* s) {
    if (!s) return;
    stop_clap_loader(s);   // join the async loader + free unapplied handles before the tracks go
    auto teardown = [](Track* t) {
        for (Vst3Handle* fx : t->effects_edit) destroy_handle(fx);  // authoritative FX list
        for (Vst3Handle* fx : t->fx_retired)   destroy_handle(fx);  // removed-but-not-freed
        destroy_handle(t->handle);
        for (vivid::AudioOp* op : t->op_effects_edit) vivid::audio_op_destroy(op);   // native audio ops
        for (vivid::AudioOp* op : t->op_sources_edit) vivid::audio_op_destroy(op);   // extra graph sources
        for (vivid::AudioOp* op : t->op_retired)      vivid::audio_op_destroy(op);
        vivid::audio_op_destroy(t->op_instrument_edit);
        delete t->clap_inst;                                    // CLAP instrument + FX + retired
        for (ClapHandle* c : t->clap_effects) delete c;
        for (ClapHandle* c : t->clap_retired) delete c;
        for (Track::PluginSlot& ps : t->pslots) {               // A2: user-spawned plugin NODES
            if (ps.vst3) destroy_handle(ps.vst3);
            if (ps.clap) delete ps.clap;
        }
    };
    for (auto& tp : s->tracks)         teardown(tp.get());
    for (auto& tp : s->tracks_retired) teardown(tp.get());   // tracks removed during the run
    delete s;
}

// Effect queries read the UI-owned list (the audio thread mirrors it).
int session_effect_count(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? static_cast<int>(s->tracks[t]->effects_edit.size()) : 0;
}
const char* session_effect_name(Session* s, int t, int e) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return "";
    auto& fx = s->tracks[t]->effects_edit;
    return (e >= 0 && e < static_cast<int>(fx.size()) && fx[e]) ? fx[e]->plugin_name.c_str() : "";
}
void* session_effect_controller(Session* s, int t, int e) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return nullptr;
    auto& fx = s->tracks[t]->effects_edit;
    return (e >= 0 && e < static_cast<int>(fx.size()) && fx[e]) ? fx[e]->controller : nullptr;
}
bool session_add_effect(Session* s, int t, const char* bundle) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !bundle) return false;
    // A2 bug fix: on an AUTHORITATIVE track the linear chain is no longer the source of truth, and
    // rebuild_track_graph only re-binds handles into pre-existing nodes — so an effect pushed onto
    // the chain here never became a graph node at all. It was silently inaudible AND invisible.
    // Forward to the graph, so every caller (the browser drop, MCP, a project load) gets a node.
    if (t < static_cast<int>(s->tracks.size()) && s->tracks[t]->graph_authoritative)
        return session_audio_graph_add_plugin(s, t, bundle, kFmtVST3, /*is_source*/0, "") >= 0;
    Vst3Handle* fx = load_effect(bundle, s->sample_rate, &s->host);  // load outside the lock (slow)
    if (!fx) return false;
    Track& tr = *s->tracks[t];
    { std::lock_guard<std::mutex> lk(tr.fx_mtx); tr.effects_edit.push_back(fx); }
    tr.fx_gen.fetch_add(1, std::memory_order_release);
    rebuild_track_graph(&tr);   // AG-0: a VST3 effect disqualifies the native graph path (gok=false)
    std::fprintf(stderr, "[Session] track %d + effect: %s\n", t, fx->plugin_name.c_str());
    return true;
}
void session_remove_effect(Session* s, int t, int e) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return;
    Track& tr = *s->tracks[t];
    {
        std::lock_guard<std::mutex> lk(tr.fx_mtx);
        if (e >= 0 && e < static_cast<int>(tr.effects_edit.size())) {
            tr.fx_retired.push_back(tr.effects_edit[e]);   // freed at shutdown, not here
            tr.effects_edit.erase(tr.effects_edit.begin() + e);
        }
    }
    tr.fx_gen.fetch_add(1, std::memory_order_release);
    rebuild_track_graph(&tr);   // AG-0: a VST3 effect disqualifies the native graph path (gok=false)
}

// --- Native audio operators (AO-1). index -1 = the instrument slot; >=0 = an effect. ---
void session_set_op_registry(Session* s, vivid::OpRegistry* reg) { if (s) s->op_reg = reg; }

static vivid::AudioOp* audio_op_at(Session* s, int t, int index) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return nullptr;
    Track& tr = *s->tracks[t];
    if (index < 0) return tr.op_instrument_edit;
    return (index < static_cast<int>(tr.op_effects_edit.size())) ? tr.op_effects_edit[index] : nullptr;
}

int session_add_audio_effect(Session* s, int t, const char* op_type) {
    if (!s || !s->op_reg || t < 0 || t >= static_cast<int>(s->tracks.size())) return -1;
    vivid::AudioOp* op = vivid::audio_op_create(*s->op_reg, op_type);
    if (!op || vivid::audio_op_is_source(op)) { if (op) vivid::audio_op_destroy(op); return -1; }  // effects only
    Track& tr = *s->tracks[t];
    int idx;
    { std::lock_guard<std::mutex> lk(tr.op_fx_mtx); idx = static_cast<int>(tr.op_effects_edit.size()); tr.op_effects_edit.push_back(op); }
    tr.op_fx_gen.fetch_add(1, std::memory_order_release);
    rebuild_track_graph(&tr);   // AG-0: recompile the audio graph from the new native chain
    return idx;
}
void session_remove_audio_effect(Session* s, int t, int index) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return;
    Track& tr = *s->tracks[t];
    { std::lock_guard<std::mutex> lk(tr.op_fx_mtx);
      if (index >= 0 && index < static_cast<int>(tr.op_effects_edit.size())) {
          tr.op_retired.push_back(tr.op_effects_edit[index]);   // freed at shutdown, not on the audio thread
          tr.op_effects_edit.erase(tr.op_effects_edit.begin() + index);
      } }
    tr.op_fx_gen.fetch_add(1, std::memory_order_release);
    rebuild_track_graph(&tr);   // AG-0: recompile the audio graph from the new native chain
}
int session_audio_effect_count(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? static_cast<int>(s->tracks[t]->op_effects_edit.size()) : 0;
}
const char* session_audio_op_type(Session* s, int t, int index) {
    vivid::AudioOp* op = audio_op_at(s, t, index);
    return op ? vivid::audio_op_type(op) : "";
}
int session_set_track_audio_instrument(Session* s, int t, const char* op_type) {
    if (!s || !s->op_reg || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0;
    vivid::AudioOp* op = nullptr;
    if (op_type && *op_type) {
        op = vivid::audio_op_create(*s->op_reg, op_type);
        if (!op || !vivid::audio_op_is_source(op)) { if (op) vivid::audio_op_destroy(op); return 0; }  // sources only
    }
    Track& tr = *s->tracks[t];
    { std::lock_guard<std::mutex> lk(tr.op_fx_mtx);
      if (tr.op_instrument_edit) tr.op_retired.push_back(tr.op_instrument_edit);
      tr.op_instrument_edit = op; }
    tr.op_fx_gen.fetch_add(1, std::memory_order_release);
    rebuild_track_graph(&tr);   // AG-0: recompile the audio graph from the new native chain
    return 1;
}
// CLAP instrument/effect assignment is ASYNC — see session_request_track_clap_* below. (A slow
// plugin ctor must never run on the main thread.) The old synchronous session_set_track_clap_*
// entry points were removed once persist + the control server both moved to the async path.

// --- Async CLAP loading (see the Session members). The worker instantiates plugins off the
// main thread; the main thread applies completions via session_poll_plugin_loads(). ---
static void clap_worker_main(Session* s) {
    for (;;) {
        Session::ClapLoadReq req;
        {
            std::unique_lock<std::mutex> lk(s->clap_load_mtx);
            s->clap_load_cv.wait(lk, [s]{ return s->clap_worker_stop || !s->clap_reqs.empty(); });
            if (s->clap_worker_stop) return;                 // dying: drop any queued requests
            req = std::move(s->clap_reqs.front());
            s->clap_reqs.pop_front();
        }
        ClapHandle* h = clap_load_plugin(req.path, req.sr, kGraphMaxBlock);   // SLOW — off the main thread
        {
            std::lock_guard<std::mutex> lk(s->clap_load_mtx);
            s->clap_done.push_back({ req.track_id, req.is_instrument, req.path, h, std::move(req.state), req.slot });
        }
    }
}
static void ensure_clap_worker(Session* s) {
    if (!s->clap_worker.joinable()) s->clap_worker = std::thread(clap_worker_main, s);
}
static int enqueue_clap_load(Session* s, int t, bool is_instrument, const char* clap_path,
                            const char* state, int slot = -1) {
    ensure_clap_worker(s);
    const double sr = s->sample_rate > 0 ? s->sample_rate : 48000;
    const int tid = s->tracks[t]->id;   // capture the STABLE id (callers validated t in range)
    {
        std::lock_guard<std::mutex> lk(s->clap_load_mtx);
        s->clap_last_error.clear();
        s->clap_reqs.push_back({ tid, is_instrument, clap_path, sr, state ? state : "", slot });
    }
    s->clap_pending.fetch_add(1, std::memory_order_release);
    s->clap_load_cv.notify_one();
    return 1;
}
// Async instrument assign, optionally restoring a saved patch `state` once the (off-thread) load
// finishes. Empty path clears synchronously (fast). Poll session_plugin_loads_pending() /
// get_audio_graph to know when it's live.
int session_request_track_clap_instrument_state(Session* s, int t, const char* clap_path, const char* state) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0;
    if (!clap_path || !*clap_path) {                         // clear: no load needed, do it inline
        Track& tr = *s->tracks[t];
        if (tr.clap_inst) tr.clap_retired.push_back(tr.clap_inst);
        tr.clap_inst = nullptr; rebuild_track_graph(&tr);
        return 1;
    }
    return enqueue_clap_load(s, t, /*is_instrument*/true, clap_path, state);
}
int session_request_track_clap_effect_state(Session* s, int t, const char* clap_path, const char* state) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !clap_path || !*clap_path) return 0;
    // Same drop-loss fix as session_add_effect: on an authoritative track a chain-slot CLAP effect
    // would never become a node. Spawn it as one instead.
    if (s->tracks[t]->graph_authoritative)
        return session_audio_graph_add_plugin(s, t, clap_path, kFmtCLAP, /*is_source*/0, "") >= 0 ? 1 : 0;
    return enqueue_clap_load(s, t, /*is_instrument*/false, clap_path, state);
}
int session_request_track_clap_instrument(Session* s, int t, const char* clap_path) {
    return session_request_track_clap_instrument_state(s, t, clap_path, "");
}
int session_request_track_clap_effect(Session* s, int t, const char* clap_path) {
    return session_request_track_clap_effect_state(s, t, clap_path, "");
}
// Main thread: apply any finished async loads (swap into the Track, restore saved patch state, and
// rebuild its graph). Call once per frame from the run loop. Failed / not-an-instrument loads set
// the last-error string. For a serial worker the completions arrive in request order, so per-track
// the instrument is applied before its effects and the effect chain keeps its saved order.
void session_poll_plugin_loads(Session* s) {
    if (!s) return;
    std::deque<Session::ClapLoadDone> done;
    {
        std::lock_guard<std::mutex> lk(s->clap_load_mtx);
        if (s->clap_done.empty()) return;
        done.swap(s->clap_done);
    }
    for (auto& d : done) {
        ClapHandle* h = d.handle;
        Track* trp = nullptr;                                // resolve the STABLE id to the current slot
        for (auto& tp : s->tracks) if (tp->id == d.track_id) { trp = tp.get(); break; }
        // A2: a slot-addressed load — a CLAP the user spawned as a graph NODE. Bind it into its
        // slot; the node has existed (passing audio through / silent) since the moment it was added.
        if (d.slot >= 0) {
            bool bound = false;
            if (trp && h) {
                std::lock_guard<std::mutex> lk(trp->gmtx);
                const size_t si = static_cast<size_t>(d.slot);
                // The node may have been deleted while this was loading — the slot is then `dead`
                // and the handle has nowhere to go. (The slot itself is never recycled, so we can
                // always tell "gone" from "someone else's now".)
                if (si < trp->pslots.size() && !trp->pslots[si].dead) {
                    trp->pslots[si].clap = h;
                    trp->pslots[si].pending = false;
                    bound = true;
                    // ADR-0015 (M2): only now do we know whether this CLAP can GENERATE notes (its
                    // note-ports extension is on the handle), so declare the node's note ports here.
                    for (size_t ni = 0; ni < trp->agnodes.size(); ++ni) {
                        if (trp->agnodes[ni].pslot != d.slot) continue;
                        const auto& gn = trp->agraph.nodes();
                        if (ni < gn.size())
                            trp->agraph.set_note_ports(gn[ni].id, h->has_note_in || d.is_instrument,
                                                       h->has_note_out);
                        break;
                    }
                }
            }
            if (!bound) {
                delete h;                                    // node deleted, track gone, or load failed
                if (!h) s->clap_last_error = d.path + ": failed to load CLAP plugin";
                if (trp) {                                   // mark the slot failed so the UI stops waiting
                    std::lock_guard<std::mutex> lk(trp->gmtx);
                    const size_t si = static_cast<size_t>(d.slot);
                    if (si < trp->pslots.size()) trp->pslots[si].pending = false;
                }
            } else {
                if (!d.state.empty()) clap_load_state(h, d.state);   // restore the saved patch
                rebuild_track_graph(trp);   // takes gmtx itself — MUST be called with the lock dropped
            }
            s->clap_pending.fetch_sub(1, std::memory_order_release);
            continue;
        }
        if (trp) {
            Track& tr = *trp;
            if (d.is_instrument) {
                if (h && !h->has_note_in) { delete h; h = nullptr; s->clap_last_error = d.path + ": not a CLAP instrument"; }
                if (h) {
                    if (tr.clap_inst) tr.clap_retired.push_back(tr.clap_inst);
                    tr.clap_inst = h;
                    if (!d.state.empty()) clap_load_state(h, d.state);   // restore the saved patch (persist)
                    rebuild_track_graph(&tr);
                } else if (s->clap_last_error.empty()) s->clap_last_error = d.path + ": failed to load CLAP instrument";
            } else {
                if (h) {
                    tr.clap_effects.push_back(h);
                    if (!d.state.empty()) clap_load_state(h, d.state);
                    rebuild_track_graph(&tr);
                } else s->clap_last_error = d.path + ": failed to load CLAP effect";
            }
        } else if (h) {
            delete h;                                        // track was removed while loading
        }
        s->clap_pending.fetch_sub(1, std::memory_order_release);
    }
}
int session_plugin_loads_pending(Session* s) { return s ? s->clap_pending.load(std::memory_order_acquire) : 0; }
const char* session_last_plugin_load_error(Session* s) {
    return (s && !s->clap_last_error.empty()) ? s->clap_last_error.c_str() : "";
}
// Stop + join the loader thread and free any completions that were never applied. Call before
// tearing down the tracks (a handle in `clap_done` still owns a DSO ref).
static void stop_clap_loader(Session* s) {
    if (s->clap_worker.joinable()) {
        { std::lock_guard<std::mutex> lk(s->clap_load_mtx); s->clap_worker_stop = true; }
        s->clap_load_cv.notify_all();
        s->clap_worker.join();
    }
    for (auto& d : s->clap_done) delete d.handle;   // finished-but-unapplied handles
    s->clap_done.clear();
}
int         session_audio_op_param_count(Session* s, int t, int index) { vivid::AudioOp* op = audio_op_at(s, t, index); return op ? vivid::audio_op_param_count(op) : 0; }
const char* session_audio_op_param_name(Session* s, int t, int index, int p) { vivid::AudioOp* op = audio_op_at(s, t, index); return op ? vivid::audio_op_param_name(op, p) : ""; }
int         session_audio_op_param_hint(Session* s, int t, int index, int p) { vivid::AudioOp* op = audio_op_at(s, t, index); return op ? vivid::audio_op_param_hint(op, p) : 0; }
float       session_audio_op_param_get(Session* s, int t, int index, int p) { vivid::AudioOp* op = audio_op_at(s, t, index); return op ? vivid::audio_op_param_get(op, p) : 0.f; }
float       session_audio_op_param_min(Session* s, int t, int index, int p) { vivid::AudioOp* op = audio_op_at(s, t, index); return op ? vivid::audio_op_param_min(op, p) : 0.f; }
float       session_audio_op_param_max(Session* s, int t, int index, int p) { vivid::AudioOp* op = audio_op_at(s, t, index); return op ? vivid::audio_op_param_max(op, p) : 1.f; }
void        session_audio_op_param_set(Session* s, int t, int index, int p, float v) { vivid::AudioOp* op = audio_op_at(s, t, index); if (op) vivid::audio_op_param_set(op, p, v); }

// Enumerate registered native audio operators for the device-chain pickers.
// want_source: 1 = instruments/generators (no audio input), 0 = effects (audio input).
// The registry inspection lives in audio_op_runtime.cpp (the TU with the full operator_api).
// ADR-0015: the native NOTE EFFECTS (Arp, ...) — offered by the chooser as note ops, not
// instruments (they make no sound).
int session_available_note_op_count(Session* s) {
    return (s && s->op_reg) ? vivid::audio_note_op_count(*s->op_reg) : 0;
}
const char* session_available_note_op_name(Session* s, int idx) {
    return (s && s->op_reg) ? vivid::audio_note_op_name(*s->op_reg, idx) : "";
}
int session_available_mod_op_count(Session* s) {   // ADR-0022: native modulators (LFO / envelope)
    return (s && s->op_reg) ? vivid::audio_mod_op_count(*s->op_reg) : 0;
}
const char* session_available_mod_op_name(Session* s, int idx) {
    return (s && s->op_reg) ? vivid::audio_mod_op_name(*s->op_reg, idx) : "";
}

int session_available_audio_op_count(Session* s, int want_source) {
    return (s && s->op_reg) ? vivid::audio_op_registry_count(*s->op_reg, want_source != 0) : 0;
}
const char* session_available_audio_op_name(Session* s, int want_source, int idx) {
    return (s && s->op_reg) ? vivid::audio_op_registry_name(*s->op_reg, want_source != 0, idx) : "";
}

// AG-1 graph introspection. All read `t->agraph`/`t->agnodes` under the track's graph lock and
// bounds-check every index (safe defaults on miss). `agnodes` is parallel to `agraph.nodes()`
// (same index), so a node index maps to both its topology entry and its binding.
static Track* graph_track(Session* s, int t) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return nullptr;
    return s->tracks[t].get();
}
int session_track_audio_graph_ok(Session* s, int t) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->gok_edit ? 1 : 0;
}
int session_track_audio_graph_node_count(Session* s, int t) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return static_cast<int>(tr->agraph.nodes().size());
}
int session_track_audio_graph_node_id(Session* s, int t, int i) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const auto& n = tr->agraph.nodes();
    return (i >= 0 && i < static_cast<int>(n.size())) ? n[i].id : -1;
}
// ADR-0015: does node i take / emit NOTES? (The UI draws note ports from this; an agent needs it to
// know whether a plugin can drive another instrument.)
void session_track_audio_graph_node_note_ports(Session* s, int t, int i, int* note_in, int* note_out) {
    if (note_in) *note_in = 0;
    if (note_out) *note_out = 0;
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const auto& ns = tr->agraph.nodes();
    if (i < 0 || i >= static_cast<int>(ns.size())) return;
    if (note_in) *note_in = ns[static_cast<size_t>(i)].note_in ? 1 : 0;
    if (note_out) *note_out = ns[static_cast<size_t>(i)].note_out ? 1 : 0;
}

int session_track_audio_graph_node_kind(Session* s, int t, int i) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (i < 0 || i >= static_cast<int>(tr->agnodes.size())) return -1;
    switch (tr->agnodes[i].kind) {
        case GNKind::NativeInst: case GNKind::Vst3Inst: case GNKind::ClapInst: case GNKind::Sampler: return 0;  // source/instrument
        case GNKind::NativeFx:   case GNKind::Vst3Fx:   case GNKind::ClapFx:   return 1;              // effect
        case GNKind::Output:     return 2;
        case GNKind::MidiIn:       return 3;   // ADR-0015: the track's note stream as a node
        case GNKind::NativeNoteFx: return 4;   // ADR-0015: a note effect (Arp) — notes in, notes out
        case GNKind::NativeMod:    return 5;   // ADR-0022: a modulator (LFO) — no audio, emits control
    }
    return -1;
}
const char* session_track_audio_graph_node_type(Session* s, int t, int i) {
    Track* tr = graph_track(s, t);
    if (!tr) return "";
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (i < 0 || i >= static_cast<int>(tr->agnodes.size())) return "";
    const GNodeBind& nb = tr->agnodes[i];
    if (nb.op) return vivid::audio_op_type(nb.op);
    if (nb.clap) return nb.clap->name.c_str();          // CLAP nodes: the plugin's display name
    switch (nb.kind) {                                   // VST3/sampler nodes have no AudioOp
        case GNKind::Vst3Inst: case GNKind::Vst3Fx: return "VST3";
        case GNKind::Sampler:                       return "Sampler";
        default:                                    return "";
    }
}
// Persistence discriminator for a node's binding family (the UI-facing node_type returns the
// plugin's display name, which the loader can't map back to a plugin family). Stable codes:
// 0 = native op (createable via audio_op_create) or Output, 1 = VST3, 2 = CLAP, 3 = Sampler.
// The loader uses this to build the right placeholder agnode for a source/effect whose op isn't
// a native operator (VST3/CLAP handles are bound later; see rebind_authoritative_plugins).
int session_track_audio_graph_node_plugin_kind(Session* s, int t, int i) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (i < 0 || i >= static_cast<int>(tr->agnodes.size())) return 0;
    switch (tr->agnodes[i].kind) {
        case GNKind::Vst3Inst: case GNKind::Vst3Fx: return 1;
        case GNKind::ClapInst: case GNKind::ClapFx: return 2;
        case GNKind::Sampler:                       return 3;
        default:                                    return 0;   // native inst/fx + Output
    }
}
// Copy node i's output-waveform scope (oldest→newest) into out[n]; returns samples written (0 if
// unavailable). Display-only: node_scope is a fixed-size ring the audio thread writes lock-free, so a
// concurrent read is benign (a torn value = a one-pixel blip). node index i == the compiled out_buf.
int session_track_audio_graph_node_scope(Session* s, int t, int i, float* out, int n) {
    Track* tr = graph_track(s, t);
    if (!tr || !out || n <= 0 || i < 0 || i >= kGraphMaxNodes ||
        static_cast<size_t>(i + 1) * kScopeN > tr->node_scope.size()) return 0;
    const float* ring = tr->node_scope.data() + static_cast<size_t>(i) * kScopeN;
    const uint32_t head = tr->node_scope_head[i];   // next write position; newest is head-1
    const int cnt = std::min(n, kScopeN);
    for (int k = 0; k < cnt; ++k)
        out[k] = ring[(head + kScopeN - static_cast<uint32_t>(cnt) + static_cast<uint32_t>(k)) % kScopeN];
    return cnt;
}

// The VST3 IEditController behind a graph node (Vst3Inst / Vst3Fx), so the audio graph can open the
// plugin's native editor for it. Null for native / sampler / output nodes (which have no plugin GUI).
void* session_audio_graph_node_controller(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t);
    if (!tr) return nullptr;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return nullptr;
    const GNodeBind& nb = tr->agnodes[idx];
    return (nb.handle && (nb.kind == GNKind::Vst3Inst || nb.kind == GNKind::Vst3Fx)) ? nb.handle->controller : nullptr;
}

int session_track_audio_graph_output_id(Session* s, int t) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.output_id();
}
int session_track_audio_graph_edge_count(Session* s, int t) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return static_cast<int>(tr->agraph.edges().size());
}
// ADR-0015/0022: what signal an edge carries — 0 = audio, 1 = note, 2 = control.
int session_track_audio_graph_edge_kind(Session* s, int t, int e) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const auto& es = tr->agraph.edges();
    if (e < 0 || e >= static_cast<int>(es.size())) return 0;
    switch (es[static_cast<size_t>(e)].kind) {
        case vivid::audio::EdgeKind::Note:    return 1;
        case vivid::audio::EdgeKind::Control: return 2;
        default:                              return 0;
    }
}

int session_track_audio_graph_edge_from(Session* s, int t, int e) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const auto& es = tr->agraph.edges();
    return (e >= 0 && e < static_cast<int>(es.size())) ? es[e].from_id : -1;
}
int session_track_audio_graph_edge_to(Session* s, int t, int e) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const auto& es = tr->agraph.edges();
    return (e >= 0 && e < static_cast<int>(es.size())) ? es[e].to_id : -1;
}
// ADR-0022: a control edge's target param, or -1 (not a control edge / bad index). The UI needs it
// to draw the modulation arc on the right knob; MCP reports it so a control edge round-trips.
int session_track_audio_graph_edge_dest_param(Session* s, int t, int e) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const auto& es = tr->agraph.edges();
    if (e < 0 || e >= static_cast<int>(es.size()) || es[e].kind != vivid::audio::EdgeKind::Control) return -1;
    return es[e].dest_param;
}
// A control edge's shaper (amount/curve/invert/bipolar) — the UI evaluates control_resolve() at
// src=0 and src=1 with these to draw the arc's extent. Returns 1 on a real control edge, else 0
// (outputs left untouched). Any of the out-pointers may be null.
int session_track_audio_graph_edge_control_shape(Session* s, int t, int e, float* amount, float* curve,
                                                 int* invert, int* bipolar) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const auto& es = tr->agraph.edges();
    if (e < 0 || e >= static_cast<int>(es.size()) || es[e].kind != vivid::audio::EdgeKind::Control) return 0;
    const vivid::audio::ControlShape& sh = es[e].shape;
    if (amount)  *amount  = sh.amount;
    if (curve)   *curve   = sh.curve;
    if (invert)  *invert  = sh.invert ? 1 : 0;
    if (bipolar) *bipolar = sh.bipolar ? 1 : 0;
    return 1;
}

// AG-1 step 2 — authoritative topology edits (UI thread). Each flips the track to
// graph_authoritative (the graph, not the linear chain, is now the source of truth) and
// republishes to the audio thread via republish_track_graph. All hold t->gmtx while mutating
// agraph/agnodes; op lifetime follows the existing own/retire model (freed at shutdown).

// Create the Output sink for a bare graph, keeping the host's parallel bind array (agnodes) in
// step with the graph's node list. Passed to AudioGraph::fan_in_to_output as its make_output hook.
static int make_output_node(void* user) {
    Track* tr = static_cast<Track*>(user);
    const int out = tr->agraph.add_node(false, true, nullptr, nullptr, "out");
    tr->agnodes.push_back({ GNKind::Output, nullptr });
    return out;
}

// Add a native effect as a new node, inserted just before Output (every P->Output becomes
// P->new, then new->Output) so it lands at the end of the signal path and is immediately
// audible. Returns the new node id, or -1 (unknown op / source op / node cap / no track).
int session_audio_graph_add_op(Session* s, int t, const char* op_type) {
    Track* tr = graph_track(s, t);
    if (!tr || !s->op_reg) return -1;
    vivid::AudioOp* op = vivid::audio_op_create(*s->op_reg, op_type);
    if (!op || vivid::audio_op_is_source(op)) { if (op) vivid::audio_op_destroy(op); return -1; }  // effects only
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (static_cast<int>(tr->agraph.nodes().size()) + 1 > kGraphMaxNodes) { vivid::audio_op_destroy(op); return -1; }
    { std::lock_guard<std::mutex> olk(tr->op_fx_mtx); tr->op_effects_edit.push_back(op); }   // ownership
    tr->op_fx_gen.fetch_add(1, std::memory_order_release);
    const int nid = tr->agraph.add_node(false, false, nullptr, nullptr, op_type ? op_type : "fx");
    tr->agnodes.push_back({ GNKind::NativeFx, op });   // keep agnodes parallel to nodes()
    tr->agraph.splice_before_output(nid);              // shared wiring policy (audio_graph.cpp)
    tr->graph_authoritative = true;
    republish_track_graph(tr);
    return nid;
}

// Add a native instrument as a new *source* node, wired straight to Output (a parallel source —
// two of these with disjoint key ranges = a key-split). Sibling of add_op but sources-only and
// fan-in (no inline splice). Returns the new node id, or -1 (unknown/effect op / node cap / no track).
int session_audio_graph_add_source(Session* s, int t, const char* op_type) {
    Track* tr = graph_track(s, t);
    if (!tr || !s->op_reg) return -1;
    vivid::AudioOp* op = vivid::audio_op_create(*s->op_reg, op_type);
    if (!op || !vivid::audio_op_is_source(op)) { if (op) vivid::audio_op_destroy(op); return -1; }  // sources only
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (static_cast<int>(tr->agraph.nodes().size()) + 1 > kGraphMaxNodes) { vivid::audio_op_destroy(op); return -1; }
    { std::lock_guard<std::mutex> olk(tr->op_fx_mtx);   // ownership: primary slot if free, else an extra source
      if (!tr->op_instrument_edit) tr->op_instrument_edit = op;
      else                         tr->op_sources_edit.push_back(op); }
    tr->op_fx_gen.fetch_add(1, std::memory_order_release);
    const int nid = tr->agraph.add_node(true, false, nullptr, nullptr, op_type ? op_type : "src");
    tr->agraph.set_note_ports(nid, /*note_in*/true, /*note_out*/false);   // an instrument CONSUMES notes
    tr->agnodes.push_back({ GNKind::NativeInst, op });   // full range by default; set via key_range_set
    // Shared wiring policy (audio_graph.cpp). The Output node, if it has to be created, must also
    // get its entry in the host's parallel bind array — hence the hook.
    tr->agraph.fan_in_to_output(nid, &make_output_node, tr);
    tr->graph_authoritative = true;
    republish_track_graph(tr);
    return nid;
}

// A2: add a VST3/CLAP plugin as a first-class graph NODE — the thing that was impossible before
// (the graph could only ever *represent* plugin nodes derived from the linear chain, so no add path
// could put one anywhere). An instrument fans in to Output (parallel source → key-splits, layers);
// an effect splices in before Output.
//
// The node id comes back IMMEDIATELY, even for CLAP, whose load is async and slow (Surge XT takes
// ~90s). A not-yet-bound node is already RT-safe: run_track_graph gates on the handle being
// non-null, so it passes audio through (effect) or stays silent (instrument) until the handle
// lands. That is the existing placeholder behavior — no new audio-thread code.
int session_audio_graph_add_plugin(Session* s, int t, const char* path, int format,
                                   int is_source, const char* uid) {
    Track* tr = graph_track(s, t);
    if (!tr || !path || !*path) return -1;
    const bool clap = (format == kFmtCLAP);
    const bool src  = (is_source != 0);

    // VST3 loads synchronously (what session_add_effect already does); CLAP must not — its ctor can
    // block the main thread for a minute or more.
    Vst3Handle* vh = nullptr;
    if (!clap) {
        // `uid` (the class cid the probe recorded) makes the loader pick the EXACT class rather
        // than guessing "first instrument class, else class 0" in a multi-class bundle.
        vh = vst3_load_plugin(path, uid ? uid : "", s->sample_rate, std::string(), &s->host,
                              /*as_effect*/ !src);   // slow: outside the lock
        if (!vh) return -1;
        if (vh->processor->setProcessing(true) != kResultOk) {}
        vh->processing = true;
    }

    int slot = -1, nid = -1;
    {
        std::lock_guard<std::mutex> lk(tr->gmtx);
        if (static_cast<int>(tr->agraph.nodes().size()) + 1 > kGraphMaxNodes) {
            if (vh) destroy_handle(vh);
            return -1;
        }
        Track::PluginSlot ps;
        ps.format = format;
        ps.is_source = src;
        ps.path = path;
        ps.uid = uid ? uid : "";
        ps.vst3 = vh;
        ps.pending = clap;               // a CLAP node exists first and binds later
        tr->pslots.push_back(std::move(ps));
        slot = static_cast<int>(tr->pslots.size()) - 1;

        const GNKind kind = clap ? (src ? GNKind::ClapInst : GNKind::ClapFx)
                                 : (src ? GNKind::Vst3Inst : GNKind::Vst3Fx);
        nid = tr->agraph.add_node(src, false, nullptr, nullptr, clap ? (src ? "clap" : "cfx")
                                                                    : (src ? "vst3" : "vfx"));
        // Instruments consume notes; one that also has an event OUTPUT bus (a chord generator, an
        // arpeggiator — the Captain suite) can also PRODUCE them, so it gets a note output too and
        // can drive another instrument (ADR-0015 / M3).
        if (src) tr->agraph.set_note_ports(nid, /*note_in*/true, /*note_out*/ vh && vh->has_note_out);
        GNodeBind nb;
        nb.kind = kind;
        nb.handle = vh;
        nb.pslot = slot;
        tr->agnodes.push_back(nb);

        if (src) tr->agraph.fan_in_to_output(nid, &make_output_node, tr);   // parallel source
        else     tr->agraph.splice_before_output(nid);                      // end of the signal path
        tr->graph_authoritative = true;
        republish_track_graph(tr);
    }
    // Kick the async CLAP load AFTER the node exists, so its completion has a slot to land in.
    if (clap) enqueue_clap_load(s, t, src, path, "", slot);
    return nid;
}

// 1 = the node's plugin is loaded and bound; 0 = still loading (CLAP); -1 = no such plugin node.
int session_audio_graph_node_plugin_ready(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return -1;
    const int slot = tr->agnodes[static_cast<size_t>(idx)].pslot;
    if (slot < 0 || slot >= static_cast<int>(tr->pslots.size())) return -1;
    const Track::PluginSlot& ps = tr->pslots[static_cast<size_t>(slot)];
    if (ps.pending) return 0;
    return (ps.vst3 || ps.clap) ? 1 : -1;
}

int session_audio_graph_node_plugin_failed(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return 0;
    const int slot = tr->agnodes[static_cast<size_t>(idx)].pslot;
    if (slot < 0 || slot >= static_cast<int>(tr->pslots.size())) return 0;
    const Track::PluginSlot& ps = tr->pslots[static_cast<size_t>(slot)];
    // A plugin node (non-empty path) that finished loading (not pending) with no bound handle:
    // the load failed. A node still pending, or one that never had a plugin path, is not "failed".
    return (!ps.pending && !ps.vst3 && !ps.clap && !ps.path.empty()) ? 1 : 0;
}

// The bundle a plugin node hosts ("" if it isn't a plugin node) — for persistence + the UI label.
const char* session_audio_graph_node_plugin_path(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return "";
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return "";
    const int slot = tr->agnodes[static_cast<size_t>(idx)].pslot;
    if (slot < 0 || slot >= static_cast<int>(tr->pslots.size())) return "";
    return tr->pslots[static_cast<size_t>(slot)].path.c_str();
}

// A plugin node's patch/preset (base64), so a user-spawned plugin keeps its sound across a save +
// load. "" when the node isn't a plugin node, or its plugin hasn't finished loading.
std::string session_audio_graph_node_get_state(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return {};
    Vst3Handle* vh = nullptr; ClapHandle* ch = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);
      const int idx = tr->agraph.node_index(node_id);
      if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return {};
      const int slot = tr->agnodes[static_cast<size_t>(idx)].pslot;
      if (slot < 0 || slot >= static_cast<int>(tr->pslots.size())) return {};
      vh = tr->pslots[static_cast<size_t>(slot)].vst3;
      ch = tr->pslots[static_cast<size_t>(slot)].clap; }
    // Query the plugin OUTSIDE the graph lock: getState() can be slow, and holding gmtx would stall
    // the next republish (see the VST3 save_state stutter note in docs/thread-safety.md).
    if (ch) return clap_save_state(ch);
    if (vh) return vst3_save_state(vh);
    return {};
}
void session_audio_graph_node_set_state(Session* s, int t, int node_id, const std::string& state) {
    if (state.empty()) return;
    Track* tr = graph_track(s, t); if (!tr) return;
    Vst3Handle* vh = nullptr; ClapHandle* ch = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);
      const int idx = tr->agraph.node_index(node_id);
      if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return;
      const int slot = tr->agnodes[static_cast<size_t>(idx)].pslot;
      if (slot < 0 || slot >= static_cast<int>(tr->pslots.size())) return;
      vh = tr->pslots[static_cast<size_t>(slot)].vst3;
      ch = tr->pslots[static_cast<size_t>(slot)].clap; }
    if (ch) clap_load_state(ch, state);
    else if (vh) vst3_load_state(vh, state);
}

// Set / get a source node's MIDI key range [lo,hi] (0..127). The audio thread then hands that
// source only its in-range notes (run_track_graph). No-op on a non-source / unknown node.
void session_audio_graph_node_key_range_set(Session* s, int t, int node_id, int lo, int hi) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return;
    tr->agnodes[idx].key_lo = static_cast<uint8_t>(std::clamp(lo, 0, 127));
    tr->agnodes[idx].key_hi = static_cast<uint8_t>(std::clamp(hi, 0, 127));
    republish_track_graph(tr);   // push the updated bindings to the audio thread
}
int session_audio_graph_node_key_range_get(Session* s, int t, int node_id, int* lo, int* hi) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return 0;
    if (lo) *lo = tr->agnodes[idx].key_lo;
    if (hi) *hi = tr->agnodes[idx].key_hi;
    return 1;
}

// Remove an effect node (delete-and-bridge: its predecessors reconnect to its successors so
// signal keeps flowing). Instrument and Output nodes are not removable. Returns 1 / 0.
int session_audio_graph_remove_node(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    vivid::AudioOp* retire = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);
      const int idx = tr->agraph.node_index(node_id);
      if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return 0;
      const GNodeBind& nb = tr->agnodes[idx];
      const int pslot = nb.pslot;
      // Removable: a native effect, or ANY node the user spawned from a plugin slot (instrument or
      // effect). A chain-derived plugin node is still off limits — the linear chain owns it.
      if (nb.kind != GNKind::NativeFx && pslot < 0) return 0;
      retire = nb.op;
      if (pslot >= 0 && pslot < static_cast<int>(tr->pslots.size())) {
          Track::PluginSlot& ps = tr->pslots[static_cast<size_t>(pslot)];
          // RETIRE, never free: the audio thread may still hold this pointer in its gbinds copy for
          // up to one block after the republish below. (The house pattern — fx_retired/clap_retired
          // are drained at shutdown.) The slot is marked dead but KEPT, so an async load still in
          // flight lands on "this node is gone" rather than binding into a recycled slot.
          if (ps.vst3) { tr->fx_retired.push_back(ps.vst3);   ps.vst3 = nullptr; }
          if (ps.clap) { tr->clap_retired.push_back(ps.clap); ps.clap = nullptr; }
          ps.dead = true;
          ps.pending = false;
      }
      tr->agraph.remove_node_bridged(node_id);
      tr->agnodes.erase(tr->agnodes.begin() + idx);               // mirror the node erase (parallel)
      tr->graph_authoritative = true;
      republish_track_graph(tr); }
    if (retire) {   // move ownership op_effects_edit -> op_retired (freed at shutdown, not on audio thread)
        std::lock_guard<std::mutex> olk(tr->op_fx_mtx);
        auto& v = tr->op_effects_edit;
        auto it = std::find(v.begin(), v.end(), retire);
        if (it != v.end()) v.erase(it);
        tr->op_retired.push_back(retire);
        tr->op_fx_gen.fetch_add(1, std::memory_order_release);
    }
    return 1;
}

// Add an edge from_id -> to_id. Rejected (returns 0) on a bad/duplicate/self edge, or if the
// edge would create a cycle (the graph is reverted and the last good plan keeps playing).
int session_audio_graph_connect_kind(Session* s, int t, int from_id, int to_id, int kind) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    const auto ek = (kind == 1) ? vivid::audio::EdgeKind::Note : vivid::audio::EdgeKind::Audio;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (!tr->agraph.connect(from_id, to_id, ek)) return 0;             // dup / self-loop / bad id
    if (!republish_track_graph(tr)) { tr->agraph.disconnect(from_id, to_id, ek); return 0; }  // cycle: revert
    // A note edge changes the graph's DEPTH (the instrument it feeds moves a column downstream), so
    // re-seed the layout — otherwise the note chain lands on top of the nodes it now precedes.
    if (ek == vivid::audio::EdgeKind::Note) tr->agraph.clear_positions();
    tr->graph_authoritative = true;
    return 1;
}
int session_audio_graph_connect(Session* s, int t, int from_id, int to_id) {
    return session_audio_graph_connect_kind(s, t, from_id, to_id, 0);   // audio (the default signal)
}

// ADR-0015: add a native NOTE EFFECT (Arp / chord / transpose) as a node. It is wired with NOTE
// edges only — it makes no sound, so it gets no audio wiring at all (an audio edge to Output would
// just add silence). Returns the new node id, or -1 (unknown op / cap / no track).
int session_audio_graph_add_note_op(Session* s, int t, const char* op_type) {
    Track* tr = graph_track(s, t);
    if (!tr || !s->op_reg) return -1;
    vivid::AudioOp* op = vivid::audio_op_create(*s->op_reg, op_type);
    if (!op) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (static_cast<int>(tr->agraph.nodes().size()) + 1 > kGraphMaxNodes) { vivid::audio_op_destroy(op); return -1; }
    { std::lock_guard<std::mutex> olk(tr->op_fx_mtx); tr->op_effects_edit.push_back(op); }   // ownership
    tr->op_fx_gen.fetch_add(1, std::memory_order_release);
    const int nid = tr->agraph.add_node(/*is_source*/true, false, nullptr, nullptr, op_type ? op_type : "note");
    tr->agraph.set_note_ports(nid, /*note_in*/true, /*note_out*/true);   // notes in -> notes out
    GNodeBind nb;
    nb.kind = GNKind::NativeNoteFx;
    nb.op = op;
    tr->agnodes.push_back(nb);
    tr->agraph.clear_positions();   // the note chain adds depth: re-seed so it lays out left->right
    tr->graph_authoritative = true;
    republish_track_graph(tr);
    return nid;
}

// ADR-0022: add a MODULATOR (LFO / envelope) as a node. Like a note effect it makes no sound and
// gets no audio wiring; it emits a 0..1 control signal that a CONTROL edge carries to a param.
// Returns the new node id, or -1 (unknown op / not a modulator / cap / no track).
int session_audio_graph_add_mod_op(Session* s, int t, const char* op_type) {
    Track* tr = graph_track(s, t);
    if (!tr || !s->op_reg) return -1;
    if (!op_type || !vivid::audio_op_is_mod_op(op_type)) return -1;   // only a registered modulator
    vivid::AudioOp* op = vivid::audio_op_create(*s->op_reg, op_type);
    if (!op) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (static_cast<int>(tr->agraph.nodes().size()) + 1 > kGraphMaxNodes) { vivid::audio_op_destroy(op); return -1; }
    { std::lock_guard<std::mutex> olk(tr->op_fx_mtx); tr->op_effects_edit.push_back(op); }   // ownership
    tr->op_fx_gen.fetch_add(1, std::memory_order_release);
    const int nid = tr->agraph.add_node(/*is_source*/true, false, nullptr, nullptr, op_type);
    tr->agraph.set_control_ports(nid, /*control_in*/false, /*control_out*/true);   // emits control
    GNodeBind nb;
    nb.kind = GNKind::NativeMod;
    nb.op = op;
    tr->agnodes.push_back(nb);
    tr->agraph.clear_positions();
    tr->graph_authoritative = true;
    republish_track_graph(tr);
    return nid;
}

// ADR-0022: wire a modulator's control output to ONE param of `to_id`, shaped by amount/curve/
// invert/bipolar (see ControlShape). Returns 1 / 0 (dup of that exact param / self-loop / bad id /
// cycle). The dest param is addressed the same way the param accessors are: by param index.
int session_audio_graph_connect_control(Session* s, int t, int from_id, int to_id, int dest_param,
                                        float amount, float curve, int invert, int bipolar) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    vivid::audio::ControlShape sh;
    sh.amount = amount; sh.curve = curve; sh.invert = invert != 0; sh.bipolar = bipolar != 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (!tr->agraph.connect_control(from_id, to_id, dest_param, sh)) return 0;
    if (!republish_track_graph(tr)) { tr->agraph.disconnect_control(from_id, to_id, dest_param); return 0; }  // cycle: revert
    tr->agraph.clear_positions();   // a modulator adds an upstream column: re-seed the layout
    tr->graph_authoritative = true;
    return 1;
}
int session_audio_graph_disconnect_control(Session* s, int t, int from_id, int to_id, int dest_param) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.disconnect_control(from_id, to_id, dest_param);
    republish_track_graph(tr);
    return 1;
}
// ADR-0022: re-shape an existing control edge (amount/curve/invert/bipolar) without rewiring.
// Recompiles so the audio thread picks up the new shape. 1 on success, 0 if no such edge.
int session_audio_graph_set_control_shape(Session* s, int t, int from_id, int to_id, int dest_param,
                                          float amount, float curve, int invert, int bipolar) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    vivid::audio::ControlShape sh;
    sh.amount = amount; sh.curve = curve; sh.invert = invert != 0; sh.bipolar = bipolar != 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (!tr->agraph.set_control_shape(from_id, to_id, dest_param, sh)) return 0;
    republish_track_graph(tr);
    return 1;
}

// ADR-0015: add the track's note stream AS A NODE — clips + live MIDI + typing + MCP + preview.
// It emits notes on a note edge; wire it to an instrument (or to a note effect) to route them.
// Returns the new node id, or -1.
int session_audio_graph_add_midi_in(Session* s, int t) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (static_cast<int>(tr->agraph.nodes().size()) + 1 > kGraphMaxNodes) return -1;
    const int nid = tr->agraph.add_node(/*is_source*/true, false, nullptr, nullptr, "midi");
    tr->agraph.set_note_ports(nid, /*note_in*/false, /*note_out*/true);
    GNodeBind nb;
    nb.kind = GNKind::MidiIn;
    tr->agnodes.push_back(nb);
    tr->agraph.clear_positions();   // notes add a column upstream: re-seed so the chain reads L->R
    tr->graph_authoritative = true;
    republish_track_graph(tr);
    return nid;
}

// Remove an edge (no-op if absent). Disconnecting can never create a cycle, so it always
// compiles. Returns 1.
int session_audio_graph_disconnect(Session* s, int t, int from_id, int to_id) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.disconnect(from_id, to_id);
    tr->graph_authoritative = true;
    republish_track_graph(tr);
    return 1;
}

// Node-id-keyed param access. The chain-index model (audio_op_at, -1/0+) can't address a node
// in a non-linear graph, so params are addressed by stable node id (from the introspection API).
// Works for both derived (linear) and authoritative graphs — agnodes holds each node's bound op.
static vivid::AudioOp* graph_node_op(Track* tr, int node_id) {   // caller holds tr->gmtx
    const int idx = tr->agraph.node_index(node_id);
    return (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) ? tr->agnodes[idx].op : nullptr;
}
// A VST3 graph node → its handle (whose `params`/`controller`/`param_q` back the dock param band,
// exactly as the old linear device view did). Null for native/sampler/output nodes.
static Vst3Handle* graph_node_handle(Track* tr, int node_id) {   // caller holds tr->gmtx
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return nullptr;
    const GNodeBind& nb = tr->agnodes[idx];
    return (nb.handle && (nb.kind == GNKind::Vst3Inst || nb.kind == GNKind::Vst3Fx)) ? nb.handle : nullptr;
}
// A CLAP graph node → its handle (params in PLAIN units; edits go via its SPSC param_q). Null otherwise.
static ClapHandle* graph_node_clap(Track* tr, int node_id) {   // caller holds tr->gmtx
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return nullptr;
    const GNodeBind& nb = tr->agnodes[idx];
    return (nb.clap && (nb.kind == GNKind::ClapInst || nb.kind == GNKind::ClapFx)) ? nb.clap : nullptr;
}
// The dock param band edits a graph node's params — native op params OR a VST3 node's exposed
// (automatable) params, in NORMALIZED 0..1 space like the old linear knob grid.
int session_audio_graph_node_param_count(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_count(op);
    if (Vst3Handle* h = graph_node_handle(tr, node_id)) return static_cast<int>(h->params.size());
    if (ClapHandle* c = graph_node_clap(tr, node_id)) return static_cast<int>(c->params.size());
    return 0;
}
const char* session_audio_graph_node_param_name(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return "";
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_name(op, p);
    if (Vst3Handle* h = graph_node_handle(tr, node_id))
        return (p >= 0 && p < static_cast<int>(h->params.size())) ? h->params[p].name.c_str() : "";
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size())) ? c->params[p].name.c_str() : "";
    return "";
}
int session_audio_graph_node_param_hint(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_hint(op, p);
    return 0;   // VST3 params → DEFAULT (a knob)
}
// Base/min/max WITHOUT taking gmtx — for callers already holding it (the resolved accessor below).
// Base for a native op is its `pvals` (never written by the audio thread); for a plugin it is the
// plugin's own current value (there is no host-side base for plugins yet — ADR-0022, a P2 gap).
static float graph_param_base_nolock(Track* tr, int node_id, int p) {
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_get(op, p);
    if (Vst3Handle* h = graph_node_handle(tr, node_id))
        return (h->controller && p >= 0 && p < static_cast<int>(h->params.size()))
                   ? static_cast<float>(h->controller->getParamNormalized(h->params[p].id)) : 0.f;
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size()))
                   ? static_cast<float>(clap_param_value(c, c->params[p].id)) : 0.f;
    return 0.f;
}
static float graph_param_min_nolock(Track* tr, int node_id, int p) {
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_min(op, p);
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size())) ? static_cast<float>(c->params[p].min) : 0.f;
    return 0.f;   // VST3 params are normalized
}
static float graph_param_max_nolock(Track* tr, int node_id, int p) {
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_max(op, p);
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size())) ? static_cast<float>(c->params[p].max) : 1.f;
    return 1.f;
}

float session_audio_graph_node_param_get(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return graph_param_base_nolock(tr, node_id, p);   // the BASE (ADR-0022): the user's value
}
float session_audio_graph_node_param_min(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return graph_param_min_nolock(tr, node_id, p);
}
float session_audio_graph_node_param_max(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 1.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return graph_param_max_nolock(tr, node_id, p);
}

// ADR-0022: the RESOLVED value — base + every control edge driving this param, right now. The UI
// draws the live dot with this; MCP reports it as "value". It runs the same control_resolve() the
// audio thread runs, reading each modulator's published output (ctl_pub), so the dot cannot drift
// from what you hear. For an unmodulated param it is exactly the base.
float session_audio_graph_node_param_resolved(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    float v = graph_param_base_nolock(tr, node_id, p);
    const float lo = graph_param_min_nolock(tr, node_id, p);
    const float hi = graph_param_max_nolock(tr, node_id, p);
    for (const vivid::audio::AudioGraphEdge& e : tr->agraph.edges()) {
        if (e.kind != vivid::audio::EdgeKind::Control || e.to_id != node_id || e.dest_param != p) continue;
        const int mi = tr->agraph.node_index(e.from_id);   // modulator node index == ctl_pub index
        if (mi < 0 || mi >= kGraphMaxNodes) continue;
        const float src = tr->ctl_pub[mi].load(std::memory_order_relaxed);
        v = vivid::audio::control_resolve(v, src, e.shape, lo, hi);   // stacks, like the audio thread
    }
    return v;
}
// 1 if any control edge drives this param (the UI's "modulated" affordance — the teal ring).
int session_audio_graph_node_param_wired(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    for (const vivid::audio::AudioGraphEdge& e : tr->agraph.edges())
        if (e.kind == vivid::audio::EdgeKind::Control && e.to_id == node_id && e.dest_param == p) return 1;
    return 0;
}
void session_audio_graph_node_param_set(Session* s, int t, int node_id, int p, float v) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) { vivid::audio_op_param_set(op, p, v); return; }
    if (Vst3Handle* h = graph_node_handle(tr, node_id); h && p >= 0 && p < static_cast<int>(h->params.size())) {
        const ParamID id = h->params[p].id;
        v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        h->param_q.push(id, v);                                            // → audio thread (RT-safe SPSC)
        if (h->controller) h->controller->setParamNormalized(id, v);       // → plugin GUI reflection
        return;
    }
    if (ClapHandle* c = graph_node_clap(tr, node_id); c && p >= 0 && p < static_cast<int>(c->params.size())) {
        double val = std::clamp(static_cast<double>(v), c->params[p].min, c->params[p].max);
        c->param_q.push(c->params[p].id, val);   // plain value → audio thread emits a CLAP param event
    }
}
// --- Richer param metadata (widget-by-type + curated inspector). VST3 + CLAP. ---
// A param's discreteness drives the widget: 0 discrete steps = continuous (→ slider/knob), a
// 2-state switch (→ bool/toggle), or a small named list (→ enum/dropdown). Enum labels + the human
// display value come from the plugin's own formatter (VST3 getParamStringByValue / CLAP value_to_text).
// VST3 encodes discreteness as step_count; CLAP as the IS_STEPPED flag over a plain [min,max] range.
static constexpr int kMaxEnumChoices = 24;   // beyond this a discrete param is a slider, not a giant dropdown

int session_audio_graph_node_param_type(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0 /*VIVID_PARAM_FLOAT*/;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (Vst3Handle* h = graph_node_handle(tr, node_id); h && p >= 0 && p < static_cast<int>(h->params.size())) {
        const int sc = h->params[p].step_count;
        if (sc == 1) return 2 /*VIVID_PARAM_BOOL*/;
        if (sc  > 1 && sc + 1 <= kMaxEnumChoices) return 1 /*VIVID_PARAM_INT (enum)*/;
    }
    if (ClapHandle* c = graph_node_clap(tr, node_id); c && p >= 0 && p < static_cast<int>(c->params.size())) {
        if (c->params[p].flags & CLAP_PARAM_IS_STEPPED) {
            const int span = static_cast<int>(std::lround(c->params[p].max - c->params[p].min));   // discrete values = span+1
            if (span == 1) return 2 /*VIVID_PARAM_BOOL*/;
            if (span > 1 && span + 1 <= kMaxEnumChoices) return 1 /*VIVID_PARAM_INT (enum)*/;
        }
    }
    return 0 /*VIVID_PARAM_FLOAT*/;   // native ops / continuous / too-many-steps
}
int session_audio_graph_node_param_choice_count(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (Vst3Handle* h = graph_node_handle(tr, node_id); h && p >= 0 && p < static_cast<int>(h->params.size())) {
        const int sc = h->params[p].step_count;
        return (sc > 1 && sc + 1 <= kMaxEnumChoices) ? sc + 1 : 0;   // a discrete list of sc+1 named values
    }
    if (ClapHandle* c = graph_node_clap(tr, node_id); c && p >= 0 && p < static_cast<int>(c->params.size())) {
        if (c->params[p].flags & CLAP_PARAM_IS_STEPPED) {
            const int span = static_cast<int>(std::lround(c->params[p].max - c->params[p].min));
            if (span > 1 && span + 1 <= kMaxEnumChoices) return span + 1;
        }
    }
    return 0;
}
const char* session_audio_graph_node_param_choice_label(Session* s, int t, int node_id, int p, int choice) {
    static thread_local std::string buf; buf.clear();
    Track* tr = graph_track(s, t); if (!tr) return "";
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (Vst3Handle* h = graph_node_handle(tr, node_id); h && h->controller && p >= 0 && p < static_cast<int>(h->params.size())) {
        const int sc = h->params[p].step_count;
        if (sc > 1 && choice >= 0 && choice <= sc) {
            String128 str{};
            const double norm = static_cast<double>(choice) / static_cast<double>(sc);   // VST3: normalized position of the choice
            if (h->controller->getParamStringByValue(h->params[p].id, norm, str) == kResultOk)
                buf = vst3_tchar_to_utf8(str);
        }
    }
    if (ClapHandle* c = graph_node_clap(tr, node_id); c && c->ext_params && c->ext_params->value_to_text
        && p >= 0 && p < static_cast<int>(c->params.size())) {
        const auto& pe = c->params[p];
        const double val = pe.min + static_cast<double>(choice);   // CLAP: the choice's plain value
        if (choice >= 0 && val <= pe.max) {
            char tmp[128];
            if (c->ext_params->value_to_text(c->plugin, pe.id, val, tmp, sizeof tmp)) buf = tmp;
        }
    }
    return buf.c_str();
}
const char* session_audio_graph_node_param_display(Session* s, int t, int node_id, int p) {
    static thread_local std::string buf; buf.clear();
    Track* tr = graph_track(s, t); if (!tr) return "";
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (Vst3Handle* h = graph_node_handle(tr, node_id); h && h->controller && p >= 0 && p < static_cast<int>(h->params.size())) {
        const ParamID id = h->params[p].id;
        String128 str{};
        if (h->controller->getParamStringByValue(id, h->controller->getParamNormalized(id), str) == kResultOk) {
            buf = vst3_tchar_to_utf8(str);
            if (!h->params[p].units.empty()) { buf += ' '; buf += h->params[p].units; }
        }
    }
    if (ClapHandle* c = graph_node_clap(tr, node_id); c && c->ext_params && c->ext_params->value_to_text
        && p >= 0 && p < static_cast<int>(c->params.size())) {
        char tmp[128];
        if (c->ext_params->value_to_text(c->plugin, c->params[p].id, clap_param_value(c, c->params[p].id), tmp, sizeof tmp))
            buf = tmp;
    }
    return buf.c_str();   // "" for native ops → caller keeps its numeric fallback
}
// Is this graph node a plugin (VST3 or CLAP)? Drives the curated inspector (vs the native knob strip).
int session_audio_graph_node_is_plugin(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return (graph_node_handle(tr, node_id) != nullptr || graph_node_clap(tr, node_id) != nullptr) ? 1 : 0;
}
// Curated inspector param set (pure curation). Stored on the AudioGraphNode (UI thread; persisted).
void session_audio_graph_node_param_pin(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.pin_param(node_id, p);
}
void session_audio_graph_node_param_unpin(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.unpin_param(node_id, p);
}
int session_audio_graph_node_param_is_pinned(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.is_param_pinned(node_id, p) ? 1 : 0;
}
int session_audio_graph_node_param_pinned_count(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const std::vector<int>* v = tr->agraph.node_pinned(node_id);
    return v ? static_cast<int>(v->size()) : 0;
}
int session_audio_graph_node_param_pinned_at(Session* s, int t, int node_id, int i) {
    Track* tr = graph_track(s, t); if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const std::vector<int>* v = tr->agraph.node_pinned(node_id);
    return (v && i >= 0 && i < static_cast<int>(v->size())) ? (*v)[i] : -1;
}

// Editor node position (UI thread; persisted). set is keyed by stable node id (drag / load);
// get is by node INDEX for save/introspection iteration. Position is UI-only (not in the compiled
// plan), so setting it needs no republish. get returns 0 when the node has never been placed.
void session_audio_graph_node_set_pos(Session* s, int t, int node_id, float x, float y) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.set_node_pos(node_id, x, y);
}
int session_track_audio_graph_node_pos(Session* s, int t, int i, float* x, float* y) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (i < 0 || i >= static_cast<int>(tr->agraph.nodes().size())) return 0;
    float gx = 0.f, gy = 0.f;
    if (!tr->agraph.node_pos(tr->agraph.nodes()[i].id, gx, gy)) return 0;
    if (x) *x = gx; if (y) *y = gy;
    return 1;
}

int session_track_audio_graph_authoritative(Session* s, int t) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->graph_authoritative ? 1 : 0;
}

// AG-1 step 2 — graph load (persistence). Rebuilds an authoritative graph node-by-node; the host
// assigns FRESH node ids (returned) so the caller remaps saved-id -> new-id and replays edges by
// the new ids. Sequence: clear -> load_node* -> (set node params) -> load_edge* -> finish_load.
// Nothing is RT-published until finish_load.
void session_audio_graph_clear(Session* s, int t) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    { std::lock_guard<std::mutex> olk(tr->op_fx_mtx);   // retire any existing native ops (freed at shutdown)
      for (vivid::AudioOp* op : tr->op_effects_edit) tr->op_retired.push_back(op);
      tr->op_effects_edit.clear();
      for (vivid::AudioOp* op : tr->op_sources_edit) tr->op_retired.push_back(op);
      tr->op_sources_edit.clear();
      if (tr->op_instrument_edit) { tr->op_retired.push_back(tr->op_instrument_edit); tr->op_instrument_edit = nullptr; }
      tr->op_fx_gen.fetch_add(1, std::memory_order_release); }
    tr->agraph.reset(); tr->agnodes.clear(); tr->graph_authoritative = false;
}
int session_audio_graph_load_node(Session* s, int t, int kind, int plugin_kind, const char* op_type) {
    Track* tr = graph_track(s, t); if (!tr || !s->op_reg) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (static_cast<int>(tr->agraph.nodes().size()) + 1 > kGraphMaxNodes) return -1;
    vivid::AudioOp* op = nullptr;
    GNKind gk = GNKind::Output; bool is_src = false, is_out = false;
    if (kind == 4) {   // ADR-0015: a native NOTE EFFECT (notes in -> notes out)
        vivid::AudioOp* nop = vivid::audio_op_create(*s->op_reg, op_type);
        if (!nop) return -1;
        { std::lock_guard<std::mutex> olk(tr->op_fx_mtx); tr->op_effects_edit.push_back(nop); }
        tr->op_fx_gen.fetch_add(1, std::memory_order_release);
        const int nid_nf = tr->agraph.add_node(true, false, nullptr, nullptr, op_type ? op_type : "note");
        tr->agraph.set_note_ports(nid_nf, true, true);
        GNodeBind nbn; nbn.kind = GNKind::NativeNoteFx; nbn.op = nop;
        tr->agnodes.push_back(nbn);
        return nid_nf;
    }
    if (kind == 3) {   // ADR-0015: the MidiIn node (the track's note stream)
        const int nid_mi = tr->agraph.add_node(true, false, nullptr, nullptr, "midi");
        tr->agraph.set_note_ports(nid_mi, false, true);
        GNodeBind nbm; nbm.kind = GNKind::MidiIn;
        tr->agnodes.push_back(nbm);
        return nid_mi;
    }
    if (kind == 5) {   // ADR-0022: a native MODULATOR (LFO) — no audio, emits control
        vivid::AudioOp* mop = vivid::audio_op_create(*s->op_reg, op_type);
        if (!mop) return -1;
        { std::lock_guard<std::mutex> olk(tr->op_fx_mtx); tr->op_effects_edit.push_back(mop); }
        tr->op_fx_gen.fetch_add(1, std::memory_order_release);
        const int nid_md = tr->agraph.add_node(true, false, nullptr, nullptr, op_type ? op_type : "mod");
        tr->agraph.set_control_ports(nid_md, false, true);
        GNodeBind nbd; nbd.kind = GNKind::NativeMod; nbd.op = mop;
        tr->agnodes.push_back(nbd);
        return nid_md;
    }
    if (kind == 2) { is_out = true; }   // output sink: no op
    else if (plugin_kind != 0) {
        // Plugin source/effect (VST3/CLAP) or the audio-loop Sampler: no native op. Create the
        // placeholder binding — its handle is filled once the (async) plugin load lands, via
        // rebind_authoritative_plugins on the next rebuild/finish_load. This keeps the topology
        // node (and its edges) intact instead of dropping it because audio_op_create can't make it.
        is_src = (kind == 0);
        switch (plugin_kind) {
            case 1: gk = is_src ? GNKind::Vst3Inst : GNKind::Vst3Fx; break;   // VST3
            case 2: gk = is_src ? GNKind::ClapInst : GNKind::ClapFx; break;   // CLAP
            case 3: gk = GNKind::Sampler; is_src = true; break;              // audio-loop scene source
            default: return -1;
        }
    } else {
        op = vivid::audio_op_create(*s->op_reg, op_type);
        if (!op) return -1;
        if (kind == 0) {   // instrument (source)
            if (!vivid::audio_op_is_source(op)) { vivid::audio_op_destroy(op); return -1; }
            gk = GNKind::NativeInst; is_src = true;
            std::lock_guard<std::mutex> olk(tr->op_fx_mtx);
            // First source → the primary slot (back-compat); additional sources (a key-split) →
            // op_sources_edit. Both are just ownership; the node references the op via agnodes.
            if (!tr->op_instrument_edit) tr->op_instrument_edit = op;
            else                         tr->op_sources_edit.push_back(op);
        } else {           // effect
            if (vivid::audio_op_is_source(op)) { vivid::audio_op_destroy(op); return -1; }
            gk = GNKind::NativeFx;
            std::lock_guard<std::mutex> olk(tr->op_fx_mtx);
            tr->op_effects_edit.push_back(op);
        }
        tr->op_fx_gen.fetch_add(1, std::memory_order_release);
    }
    const int nid = tr->agraph.add_node(is_src, is_out, nullptr, nullptr, op_type ? op_type : "node");
    tr->agnodes.push_back({ gk, op });
    return nid;
}
// A2: restore a plugin node the user spawned (it carries its own bundle path, so it does NOT come
// from the track's linear chain). Mirrors session_audio_graph_load_node: NO auto-wiring — the edges
// are replayed from the file. The node exists immediately; a CLAP binds when its async load lands,
// and its saved patch is applied at that point (that's what the state arg on the request is for).
int session_audio_graph_load_plugin_node(Session* s, int t, int node_id, const char* path,
                                         int format, int is_source, const char* uid,
                                         const char* state) {
    Track* tr = graph_track(s, t);
    if (!tr || !path || !*path) return -1;
    (void)node_id;   // ids are re-issued in load order, exactly as session_audio_graph_load_node does
    const bool clap = (format == kFmtCLAP);
    const bool src  = (is_source != 0);

    Vst3Handle* vh = nullptr;
    if (!clap) {
        vh = vst3_load_plugin(path, uid ? uid : "", s->sample_rate, std::string(), &s->host, !src);
        if (vh) {
            if (vh->processor->setProcessing(true) != kResultOk) {}
            vh->processing = true;
            if (state && *state) vst3_load_state(vh, state);   // restore the saved patch
        }
        // A missing/failed plugin still gets its NODE (with a null handle): the topology and the
        // user's wiring survive, and the node is an audible no-op rather than silently vanishing.
    }

    int slot = -1, nid = -1;
    {
        std::lock_guard<std::mutex> lk(tr->gmtx);
        if (static_cast<int>(tr->agraph.nodes().size()) + 1 > kGraphMaxNodes) {
            if (vh) destroy_handle(vh);
            return -1;
        }
        Track::PluginSlot ps;
        ps.format = format;
        ps.is_source = src;
        ps.path = path;
        ps.uid = uid ? uid : "";
        ps.vst3 = vh;
        ps.pending = clap;
        tr->pslots.push_back(std::move(ps));
        slot = static_cast<int>(tr->pslots.size()) - 1;

        const GNKind gk = clap ? (src ? GNKind::ClapInst : GNKind::ClapFx)
                               : (src ? GNKind::Vst3Inst : GNKind::Vst3Fx);
        nid = tr->agraph.add_node(src, false, nullptr, nullptr, clap ? (src ? "clap" : "cfx")
                                                                    : (src ? "vst3" : "vfx"));
        if (src) tr->agraph.set_note_ports(nid, /*note_in*/true, /*note_out*/ vh && vh->has_note_out);
        GNodeBind nb;
        nb.kind = gk;
        nb.handle = vh;
        nb.pslot = slot;
        tr->agnodes.push_back(nb);
        tr->graph_authoritative = true;
    }
    if (clap) enqueue_clap_load(s, t, src, path, state ? state : "", slot);
    return nid;
}

const char* session_audio_graph_node_plugin_uid(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return "";
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return "";
    const int slot = tr->agnodes[static_cast<size_t>(idx)].pslot;
    if (slot < 0 || slot >= static_cast<int>(tr->pslots.size())) return "";
    return tr->pslots[static_cast<size_t>(slot)].uid.c_str();
}

void session_audio_graph_load_edge_kind(Session* s, int t, int from_id, int to_id, int kind) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.connect(from_id, to_id,
                       kind == 1 ? vivid::audio::EdgeKind::Note : vivid::audio::EdgeKind::Audio);
}
// ADR-0022: load a control edge (into agraph only; finish_load compiles + publishes, like the
// other load_edge* calls). Shape carried straight from the saved fields.
void session_audio_graph_load_edge_control(Session* s, int t, int from_id, int to_id, int dest_param,
                                           float amount, float curve, int invert, int bipolar) {
    Track* tr = graph_track(s, t); if (!tr) return;
    vivid::audio::ControlShape sh;
    sh.amount = amount; sh.curve = curve; sh.invert = invert != 0; sh.bipolar = bipolar != 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.connect_control(from_id, to_id, dest_param, sh);
}
void session_audio_graph_load_edge(Session* s, int t, int from_id, int to_id) {
    session_audio_graph_load_edge_kind(s, t, from_id, to_id, 0);   // audio (pre-ADR-0015 default)
}
void session_audio_graph_finish_load(Session* s, int t, int output_id) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.set_output_id(output_id);
    tr->graph_authoritative = true;
    rebind_authoritative_plugins(tr);   // bind any plugin handle that already landed (else no-op)
    republish_track_graph(tr);
}

// A6: slice an audio clip into a new MIDI track driven by a native Sampler. Computes the
// clip's slices (`slice_mode`: 1=transients, 3=16-grid), creates a paired instrument track
// whose native instrument is a Sampler loaded with the clip's PCM + those slices, and writes
// a MIDI clip mapping ascending pitches (base C1=36) → slices at their beat positions. The
// Sampler is loaded BEFORE it is published to the audio thread (no RT race). Returns the new
// track index, or -1. Runs on the UI thread (control-server drain / editor req).
int session_slice_to_midi(Session* s, int src_track, int src_scene, int slice_mode) {
    if (!aud_valid(s, src_track, src_scene) || !s->op_reg) return -1;
    if (static_cast<int>(s->tracks.size()) >= kMaxTracks) return -1;

    // 1. Snapshot the source clip's PCM + slice regions (UI-thread copy under aud_mtx).
    std::vector<float> L, R;
    std::vector<uint32_t> ss, se;
    double loop_beats = 4.0; uint32_t sr = 0, N = 0;
    {
        std::lock_guard<std::mutex> lk(s->tracks[src_track]->aud_mtx);
        const Sampler& c = s->tracks[src_track]->aud_clips[src_scene];
        if (c.L.empty()) return -1;
        L = c.L; R = c.R; N = static_cast<uint32_t>(c.L.size()); sr = c.sr;
        loop_beats = c.loop_beats > 0 ? c.loop_beats : 4.0;
        const int m = (slice_mode == 3) ? 3 : 1;   // transients unless 16-grid asked
        for (const auto& rg : audio_clip_ed::compile_slices(m, c.transients, {}, 0, N)) {
            ss.push_back(rg.start); se.push_back(rg.end);
        }
    }
    if (ss.empty()) return -1;
    const int base_note = 36;   // C1 → slice 0

    // 2. Create the paired instrument (MIDI) track — no VST3, empty clips.
    Track* nt = make_instrument_track(nullptr, s->tracks[src_track]->name + " Slices", s->scenes);
    s->tracks.emplace_back(nt);
    s->tracks.back()->id = s->next_track_id++;
    const int new_track = static_cast<int>(s->tracks.size()) - 1;
    Track& tr = *s->tracks[new_track];

    // 3. Set its native instrument to a Sampler + inject PCM/slices, then publish it.
    if (vivid::AudioOp* op = vivid::audio_op_create(*s->op_reg, "Sampler")) {
        if (vivid::audio_op_is_source(op) &&
            vivid::audio_op_load_sampler(op, L.data(), R.empty() ? nullptr : R.data(), N, sr,
                                         ss.data(), se.data(), static_cast<int>(ss.size()), base_note)) {
            std::lock_guard<std::mutex> lk(tr.op_fx_mtx);
            tr.op_instrument_edit = op;
        } else {
            vivid::audio_op_destroy(op);
        }
    }
    tr.op_fx_gen.fetch_add(1, std::memory_order_release);
    rebuild_track_graph(&tr);   // AG-0: recompile the audio graph from the new native chain

    // 4. Write the MIDI clip: one ascending-pitch note per slice at its beat position.
    {
        std::lock_guard<std::mutex> lk(tr.edit_mtx);
        MidiClip& clip = tr.edit_clips[src_scene];
        clip.notes.clear();
        for (size_t i = 0; i < ss.size(); ++i) {
            const double start = static_cast<double>(ss[i]) / N * loop_beats;
            const double end   = static_cast<double>(se[i]) / N * loop_beats;
            ClipNote nn{};
            nn.pitch = base_note + static_cast<int>(i);
            nn.start = start;
            nn.dur   = std::max(0.05, end - start);
            nn.vel   = 0.9f;
            clip.notes.push_back(nn);
        }
        clip.length = loop_beats;
    }
    tr.edit_gen.fetch_add(1, std::memory_order_release);

    rebuild_track_view(s);   // publish the fully-formed track to the audio thread
    return new_track;
}

// Resolve a plugin by DISPLAY NAME against the whole installed catalog (audio/plugin_catalog.h) and
// add it as an effect. This replaces a hard-coded five-item list ("Yak Delay"/"CHOWTape"/...): the
// catalog knows every VST3 and CLAP on the machine, so any of them resolves. Used when loading an
// OLD project, whose per-track `fx` entries were saved by name rather than by path.
//
// Matching is exact-then-prefix on the bundle name, case-insensitively — a saved "CHOWTape" must
// still find "CHOWTapeModel.vst3".
bool session_add_effect_by_name(Session* s, int t, const char* name) {
    if (!s || !name || !*name) return false;
    const auto lower = [](std::string x) {
        for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return x;
    };
    const std::string want = lower(name);
    int best = -1;
    for (int i = 0; i < plugin_count(); ++i) {
        const std::string have = lower(plugin_at(i).name);
        if (have == want) { best = i; break; }                          // exact wins
        if (best < 0 && have.rfind(want, 0) == 0) best = i;             // else the first prefix match
    }
    if (best < 0) {
        std::fprintf(stderr, "[Session] no installed plugin named '%s' (project effect dropped)\n", name);
        return false;
    }
    const PluginInfo& p = plugin_at(best);
    if (p.format == kFmtCLAP) return session_request_track_clap_effect(s, t, p.path.c_str()) != 0;
    return session_add_effect(s, t, p.path.c_str());
}

// --- Dynamic tracks (create/delete) ---

// Resolve `spec` (a .vst3 path, or a plugin name) to a loaded instrument with a MIDI input.
// Returns nullptr if nothing matched/loaded.
//
// The name path resolves against the WHOLE installed catalog (audio/plugin_catalog.h), replacing a
// hard-coded five-item label->substring table ("Serum 2" -> "serum", ...). It stays a *substring*
// match at the end so an old project's saved name (which may be a display name, not a bundle name)
// still finds its plugin.
static Vst3Handle* load_instrument_spec(Session* s, const char* spec, std::string& out_name) {
    const std::string sp = spec ? spec : "";
    if (sp.size() > 5 && sp.compare(sp.size() - 5, 5, ".vst3") == 0 && std::filesystem::exists(sp)) {
        Vst3Handle* h = vst3_load_plugin(sp.c_str(), "", s->sample_rate, std::string(), &s->host);
        if (h && h->component && h->component->getBusCount(kEvent, kInput) > 0) {
            if (h->processor->setProcessing(true) != kResultOk) {}
            h->processing = true;
            out_name = h->plugin_name.empty() ? sp : h->plugin_name;
            return h;
        }
        if (h) { h->destroy(); delete h; }
        return nullptr;
    }
    // By name: try the installed catalog first (exact, then prefix, case-insensitive).
    const auto lower = [](std::string x) {
        for (char& c : x) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return x;
    };
    const std::string want = lower(sp);
    for (int pass = 0; pass < 2 && !want.empty(); ++pass) {
        for (int i = 0; i < plugin_count(); ++i) {
            const PluginInfo& p = plugin_at(i);
            if (p.format != kFmtVST3) continue;                 // this path loads a VST3 handle
            const std::string have = lower(p.name);
            const bool m = (pass == 0) ? (have == want) : (have.rfind(want, 0) == 0);
            if (!m) continue;
            Vst3Handle* h = vst3_load_plugin(p.path.c_str(), "", s->sample_rate, std::string(), &s->host);
            if (h && h->component && h->component->getBusCount(kEvent, kInput) > 0) {
                if (h->processor->setProcessing(true) != kResultOk) {}
                h->processing = true;
                out_name = h->plugin_name.empty() ? p.name : h->plugin_name;
                return h;
            }
            if (h) { h->destroy(); delete h; }
        }
    }
    // Last resort: the old substring scan (a saved display name that isn't the bundle name).
    std::vector<std::string> bundles;
    list_vst3("/Library/Audio/Plug-Ins/VST3", bundles);
    if (const char* home = std::getenv("HOME"))
        list_vst3(std::string(home) + "/Library/Audio/Plug-Ins/VST3", bundles);
    const char* prefer[2] = { spec, nullptr };
    return load_role(bundles, prefer, s->sample_rate, &s->host, out_name);
}

int session_add_instrument_track(Session* s, const char* instrument) {
    if (!s || !instrument || !*instrument) return -1;
    if (static_cast<int>(s->tracks.size()) >= kMaxTracks) return -1;
    std::string name;
    Vst3Handle* h = load_instrument_spec(s, instrument, name);
    if (!h) { std::fprintf(stderr, "[Session] add track: no instrument matched '%s'\n", instrument); return -1; }
    s->tracks.emplace_back(make_instrument_track(h, name, s->scenes));
    s->tracks.back()->id = s->next_track_id++;
    rebuild_track_view(s);
    const int idx = static_cast<int>(s->tracks.size()) - 1;
    std::fprintf(stderr, "[Session] + track %d: %s\n", idx, name.c_str());
    return idx;
}

int session_add_audio_track(Session* s) {
    if (!s || static_cast<int>(s->tracks.size()) >= kMaxTracks) return -1;
    auto at = std::make_unique<Track>();
    at->is_audio = true;
    at->name = "Audio";
    at->gain.store(0.7f, std::memory_order_relaxed);
    for (int i = 0; i < kMaxScenes; ++i) { at->aud_trim0[i].store(0.f); at->aud_trim1[i].store(1.f); }
    at->aud_clips.reserve(kMaxScenes);   // reserve to the scene cap so session_add_scene appends without realloc
    at->aud_clips.push_back(gen_sub_pulse(s->sample_rate, 124.0));
    at->aud_clips.push_back(gen_noise_sweep(s->sample_rate, 124.0));
    at->aud_clips.push_back(gen_bell_loop(s->sample_rate, 124.0));
    pad_aud_clips(at.get(), s->scenes);
    at->active.store(-1, std::memory_order_relaxed);
    s->tracks.emplace_back(std::move(at));
    s->tracks.back()->id = s->next_track_id++;
    rebuild_track_view(s);
    const int idx = static_cast<int>(s->tracks.size()) - 1;
    std::fprintf(stderr, "[Session] + audio track %d\n", idx);
    return idx;
}

// Append a scene (grid row) to every track. Growth is append-only within the reserved
// kMaxScenes capacity, so nothing reallocates:
//   - audio tracks grow aud_clips under aud_mtx (the audio thread try_locks it around render);
//   - MIDI tracks grow edit_clips under edit_mtx + bump edit_gen — the audio thread grows the
//     audio-owned `clips` to match in its mirror-apply, keeping sched's &clips[q] valid.
// Bump s->scenes LAST (after every track has the slot), so a launch of the new scene is gated
// off until the row exists everywhere. UI/main thread only.
int session_add_scene(Session* s) {
    if (!s || s->scenes >= kMaxScenes) return -1;
    const int ns = s->scenes + 1;
    for (auto& tp : s->tracks) {
        Track* t = tp.get();
        if (t->is_audio) {
            std::lock_guard<std::mutex> lk(t->aud_mtx);
            pad_aud_clips(t, ns);
            t->aud_trim0[ns - 1].store(0.f, std::memory_order_relaxed);   // full-clip loop window for the new slot
            t->aud_trim1[ns - 1].store(1.f, std::memory_order_relaxed);
        } else {
            {
                std::lock_guard<std::mutex> lk(t->edit_mtx);
                MidiClip c; c.length = 4.0;
                t->edit_clips.push_back(c);   // reserved to kMaxScenes → no realloc
            }
            t->edit_gen.fetch_add(1, std::memory_order_release);
        }
    }
    s->scenes = ns;
    rebuild_track_view(s);
    std::fprintf(stderr, "[Session] + scene %d (now %d scenes)\n", ns - 1, ns);
    return ns - 1;
}

// Load-time only: set the scene count BEFORE tracks are recreated (rebuild_tracks_from_doc),
// so each track is born with the right number of clip slots. Clamped to [1, kMaxScenes].
void session_set_scene_count(Session* s, int scenes) {
    if (!s) return;
    s->scenes = std::min(std::max(scenes, 1), kMaxScenes);
}

// A bare native-instrument track (no VST3 handle) whose instrument + effects come from an
// authoritative audio graph loaded onto it — the home for a persisted rewired graph. Same shape
// as the track slice_to_midi builds; reserve_track_graph runs inside make_instrument_track.
int session_add_graph_track(Session* s, const char* name) {
    if (!s || static_cast<int>(s->tracks.size()) >= kMaxTracks) return -1;
    s->tracks.emplace_back(make_instrument_track(nullptr, (name && *name) ? name : "Graph", s->scenes));
    s->tracks.back()->id = s->next_track_id++;
    rebuild_track_view(s);
    const int idx = static_cast<int>(s->tracks.size()) - 1;
    std::fprintf(stderr, "[Session] + graph track %d: %s\n", idx, s->tracks.back()->name.c_str());
    return idx;
}

bool session_remove_track(Session* s, int t) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return false;
    // Move (don't free) the track to the retired list: an in-flight audio block may still
    // hold it in tracks_view until the next sync, so it must outlive this call. Freed at
    // session_destroy (no plugin teardown on the audio thread).
    s->tracks_retired.push_back(std::move(s->tracks[t]));
    s->tracks.erase(s->tracks.begin() + t);
    rebuild_track_view(s);
    std::fprintf(stderr, "[Session] - track %d (retired)\n", t);
    return true;
}

// --- Device parameters (P24). device: 0 = instrument, 1+ = effects. ---
static Vst3Handle* device_handle(Session* s, int t, int dev) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return nullptr;
    Track& tr = *s->tracks[t];
    if (dev == 0) return tr.handle;
    const int e = dev - 1;
    return (e >= 0 && e < static_cast<int>(tr.effects_edit.size())) ? tr.effects_edit[e] : nullptr;
}
int session_param_count(Session* s, int t, int dev) {
    Vst3Handle* h = device_handle(s, t, dev);
    return h ? static_cast<int>(h->params.size()) : 0;
}
const char* session_param_name(Session* s, int t, int dev, int i) {
    Vst3Handle* h = device_handle(s, t, dev);
    return (h && i >= 0 && i < static_cast<int>(h->params.size())) ? h->params[i].name.c_str() : "";
}
uint32_t session_param_id(Session* s, int t, int dev, int i) {
    Vst3Handle* h = device_handle(s, t, dev);
    return (h && i >= 0 && i < static_cast<int>(h->params.size())) ? static_cast<uint32_t>(h->params[i].id) : 0u;
}
float session_param_value(Session* s, int t, int dev, int i) {
    Vst3Handle* h = device_handle(s, t, dev);
    if (!h || !h->controller || i < 0 || i >= static_cast<int>(h->params.size())) return 0.f;
    return static_cast<float>(h->controller->getParamNormalized(h->params[i].id));
}
void session_set_param(Session* s, int t, int dev, uint32_t id, float value) {
    Vst3Handle* h = device_handle(s, t, dev);
    if (!h) return;
    value = value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
    h->param_q.push(id, value);                                        // -> audio thread (process)
    if (h->controller) h->controller->setParamNormalized(id, value);   // -> plugin GUI reflection
}

}  // namespace vivid::session
