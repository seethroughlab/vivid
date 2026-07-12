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
constexpr int      kScopeN        = 128;   // per-node output-waveform ring length (UI preview)
constexpr int      kScopePerBlock = 8;     // decimated samples pushed into the ring each block

enum class GNKind : uint8_t { NativeInst, NativeFx, Vst3Inst, Vst3Fx, ClapInst, ClapFx, Sampler, Output };
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
    std::atomic<float>    level{0.f};
    std::atomic<float>    transient{0.f};
    float                 tr_baseline = 0.f;  // onset detector baseline (audio thread)
    std::atomic<float>    band_low{0.f}, band_mid{0.f}, band_high{0.f};  // 3-band energy
    float                 flt_lo = 0.f, flt_hi = 0.f;  // one-pole crossover states
    std::vector<float>    bl, br;          // planar scratch
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
    // Preset browse cache (UI thread only): the track instrument's presets (name/id + discovery
    // metadata), filled by session_track_preset_scan and read by the count/name/id/... accessors.
    std::vector<PresetEntry> preset_cache;
    // AG-0 audio graph (ADR-0012). Working copies (audio thread) + edit copies (UI thread),
    // published via ggen/gmtx like the FX chain. `gok` gates the graph path per track (false
    // => inline). Working buffers are reserved to capacity so the audio-thread copy from the
    // edit copies never reallocates. `blk` is transient (filled each block).
    vivid::audio::CompiledAudioGraph gcg, gcg_edit;   // topology plan (POD steps)
    std::vector<GNodeBind>           gbinds, gbinds_edit;
    std::vector<float>               gpool;            // node-buffer pool
    // Per-node output-waveform scope (UI preview): the audio thread pushes kScopePerBlock decimated
    // samples of each node's output into a fixed ring (indexed by out_buf); the UI reads a snapshot to
    // draw a live waveform. Display-only — no alloc/lock; a torn head read is a harmless visual blip.
    std::vector<float>               node_scope;       // kGraphMaxNodes * kScopeN
    std::vector<uint32_t>            node_scope_head;  // kGraphMaxNodes write positions
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
    std::atomic<float>    aud_trim0[8];   // per-scene loop window (fractions)
    std::atomic<float>    aud_trim1[8];
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
    struct ClapLoadReq  { int track_id; bool is_instrument; std::string path; double sr; std::string state; };
    struct ClapLoadDone { int track_id; bool is_instrument; std::string path; ClapHandle* handle; std::string state; };
    std::thread              clap_worker;
    std::mutex               clap_load_mtx;      // guards clap_reqs / clap_done / clap_worker_stop
    std::condition_variable  clap_load_cv;
    std::deque<ClapLoadReq>  clap_reqs;
    std::deque<ClapLoadDone> clap_done;
    std::atomic<int>         clap_pending{0};    // requested-but-not-yet-applied loads
    bool                     clap_worker_stop = false;
    std::string              clap_last_error;    // main-thread only (last failed async load)
};

// Republish the current track membership for the audio thread (UI/main thread only).
// Call after any add/remove; the audio thread picks it up on its next block.
static void rebuild_track_view(Session* s) {
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
    if (static_cast<int>(t->aud_clips.size()) > scenes) t->aud_clips.resize(scenes);
    while (static_cast<int>(t->aud_clips.size()) < scenes) t->aud_clips.emplace_back();
}

// AG-0: reserve a track's audio-graph working buffers to capacity so the audio-thread copy
// from the edit copies never reallocates (RT-safe). Call once at track creation.
static void reserve_track_graph(Track* t) {
    t->gbinds.reserve(kGraphMaxNodes);       t->gbinds_edit.reserve(kGraphMaxNodes);
    t->gcg.steps.reserve(kGraphMaxNodes);    t->gcg_edit.steps.reserve(kGraphMaxNodes);
    t->gpool.assign(static_cast<size_t>(kGraphMaxNodes + 1) * 2 * kGraphMaxBlock, 0.f);
    t->node_scope.assign(static_cast<size_t>(kGraphMaxNodes) * kScopeN, 0.f);
    t->node_scope_head.assign(kGraphMaxNodes, 0u);
    t->src_nev.reserve(256);   t->src_eev.reserve(256);   // key-range filter scratch (>= any block's note count)
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
static bool republish_track_graph(Track* t) {
    if (!t->agraph.compile(t->gcg_edit)) return false;   // cycle → published plan unchanged
    t->gbinds_edit = t->agnodes;                          // parallel to nodes(): index == out_buf
    t->gok_edit    = true;
    t->ggen.fetch_add(1, std::memory_order_release);
    return true;
}

static void rebuild_track_graph(Track* t) {
    std::lock_guard<std::mutex> lk(t->gmtx);
    // Once the graph is authoritative, a legacy device-chain edit must not wipe the user's
    // topology — just recompile what's there (e.g. after a param change that calls rebuild).
    if (t->graph_authoritative) { republish_track_graph(t); return; }
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
        t->ggen.fetch_add(1, std::memory_order_release);
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
    t->ggen.fetch_add(1, std::memory_order_release);
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
        if (s.n_in == 0) {   // source node
            const bool full_range = (nb.key_lo == 0 && nb.key_hi == 127);
            if (nb.kind == GNKind::NativeInst && nb.op) {
                const NoteEvent* nn = b.notes; uint32_t nc = b.note_count;
                if (!full_range) {   // key-split: hand this source only its in-range notes
                    filter_notes_by_range(t.nev, nb.key_lo, nb.key_hi, t.src_nev);
                    nn = t.src_nev.data(); nc = static_cast<uint32_t>(t.src_nev.size());
                }
                vivid::audio_op_process(nb.op, oL, oR, frames, b.sample_rate, b.bpm, b.bpb, b.beats, nn, nc);
            }
            else if (nb.kind == GNKind::Vst3Inst && nb.handle) {
                std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));  // silent input, matches inline
                if (full_range) {   // t.vev is primed with scene-switch releases (identical to today)
                    emit_vst3(t.vev, t.nev, t.eev);
                    render_vst3_instrument(t, nb.handle, t.vev, gctx, frames, oL, oR);
                } else {            // key-split: independent filtered event list for this source
                    filter_notes_by_range(t.nev, nb.key_lo, nb.key_hi, t.src_nev);
                    filter_expr_by_range(t.eev, nb.key_lo, nb.key_hi, t.src_eev);
                    t.src_vev.clear();
                    emit_vst3(t.src_vev, t.src_nev, t.src_eev);
                    render_vst3_instrument(t, nb.handle, t.src_vev, gctx, frames, oL, oR);
                }
            }
            else if (nb.kind == GNKind::ClapInst && nb.clap) {
                std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
                if (full_range) render_clap_instrument(t, nb.clap, t.nev, frames, oL, oR);
                else { filter_notes_by_range(t.nev, nb.key_lo, nb.key_hi, t.src_nev);   // key-split
                       render_clap_instrument(t, nb.clap, t.src_nev, frames, oL, oR); }
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
        if (nb.kind == GNKind::NativeFx && nb.op)       // effect: transform in place (Output = passthrough)
            vivid::audio_op_process(nb.op, oL, oR, frames, b.sample_rate, b.bpm, b.bpb, b.beats, nullptr, 0);
        else if (nb.kind == GNKind::Vst3Fx && nb.handle && nb.handle->processing)  // non-processing = passthrough (matches inline skip)
            render_vst3_effect(t, nb.handle, gctx, frames, oL, oR);
        else if (nb.kind == GNKind::ClapFx && nb.clap && nb.clap->processing)
            render_clap_effect(t, nb.clap, frames, oL, oR);
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
    if (kind == kDrums)      for (auto& p : drum_patterns()) t->clips.push_back(p);
    else if (kind == kBass)  for (auto& p : bass_patterns()) t->clips.push_back(p);
    else                     for (auto& p : base_patterns()) t->clips.push_back(p);   // lead, at pitch
    t->sched.reset(&t->clips[0]);
    t->nev.reserve(64); t->scene_rel.reserve(64);
t->eev.reserve(256);
    t->edit_clips = t->clips;  // editor's mirror starts equal to the live clips
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
    for (int i = 0; i < scenes; ++i) { MidiClip c; c.length = 4.0; t->clips.push_back(c); }
    t->sched.reset(&t->clips[0]);
    t->nev.reserve(64); t->scene_rel.reserve(64);
t->eev.reserve(256);
    t->edit_clips = t->clips;
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
        for (int i = 0; i < 8; ++i) { at->aud_trim0[i].store(0.f); at->aud_trim1[i].store(1.f); }

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
    h->processor->process(data);
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

    const uint32_t bpb = beats_per_bar ? beats_per_bar : 4;
    const long long bar = static_cast<long long>(std::floor(beats / bpb));
    const bool new_bar = bar != s->last_bar;
    s->last_bar = bar;
    const double delta = frames * (bpm / 60.0) / (sample_rate > 0 ? sample_rate : 48000);

    bool any = false;
    for (Track* tp : s->tracks_view) {
        Track& t = *tp;
        // Skip a MIDI track only if it has NO source at all: no processing VST3 instrument
        // AND no native instrument operator (live or pending). A native-instrument-only track
        // (e.g. the Sampler from slice-to-MIDI) has no VST3 handle but still must run.
        if (!t.is_audio && (!t.handle || !t.handle->processing) && !t.op_instrument && !t.op_instrument_edit && !t.clap_inst) continue;
        any = true;

        // Apply pending clip edits (element-wise so &clips[sc] — and the
        // scheduler's clip pointer — stay valid). Only runs after a user edit.
        if (t.edit_gen.load(std::memory_order_acquire) != t.edit_gen_seen) {
            if (t.edit_mtx.try_lock()) {
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
        // AG-0: apply a pending audio-graph edit (topology plan + node bindings). Reserved
        // working buffers => these copies never reallocate (RT-safe); no free on this thread.
        if (t.ggen.load(std::memory_order_acquire) != t.ggen_seen) {
            if (t.gmtx.try_lock()) {
                t.gcg.steps  = t.gcg_edit.steps;   // POD steps; capacity reserved to kGraphMaxNodes
                t.gcg.buf_count  = t.gcg_edit.buf_count;
                t.gcg.output_buf = t.gcg_edit.output_buf;
                t.gbinds     = t.gbinds_edit;      // POD binds; capacity reserved
                t.gok        = t.gok_edit;
                t.ggen_seen  = t.ggen.load(std::memory_order_acquire);
                t.gmtx.unlock();
            }
        }
        if (t.bl.size() < frames) { t.bl.resize(frames); t.br.resize(frames); }
        float* L = t.bl.data(); float* R = t.br.data();
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
            run_track_graph(t, L, R, frames);
        }
        t.steady += frames;

        const float g = t.gain.load(std::memory_order_relaxed);
        const float sr = static_cast<float>(sample_rate > 0 ? sample_rate : 48000);
        const float a_lo = 1.f - std::exp(-6.2832f * 200.f / sr);    // crossover @ ~200 Hz
        const float a_hi = 1.f - std::exp(-6.2832f * 2000.f / sr);   // crossover @ ~2 kHz
        double sum_sq = 0.0, slo = 0.0, smi = 0.0, shi = 0.0;
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = L[i] * g, r = R[i] * g;
            out[2 * i] += l; out[2 * i + 1] += r;
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
            s->clap_done.push_back({ req.track_id, req.is_instrument, req.path, h, std::move(req.state) });
        }
    }
}
static void ensure_clap_worker(Session* s) {
    if (!s->clap_worker.joinable()) s->clap_worker = std::thread(clap_worker_main, s);
}
static int enqueue_clap_load(Session* s, int t, bool is_instrument, const char* clap_path, const char* state) {
    ensure_clap_worker(s);
    const double sr = s->sample_rate > 0 ? s->sample_rate : 48000;
    const int tid = s->tracks[t]->id;   // capture the STABLE id (callers validated t in range)
    {
        std::lock_guard<std::mutex> lk(s->clap_load_mtx);
        s->clap_last_error.clear();
        s->clap_reqs.push_back({ tid, is_instrument, clap_path, sr, state ? state : "" });
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
int session_track_audio_graph_node_kind(Session* s, int t, int i) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (i < 0 || i >= static_cast<int>(tr->agnodes.size())) return -1;
    switch (tr->agnodes[i].kind) {
        case GNKind::NativeInst: case GNKind::Vst3Inst: case GNKind::ClapInst: case GNKind::Sampler: return 0;  // source/instrument
        case GNKind::NativeFx:   case GNKind::Vst3Fx:   case GNKind::ClapFx:   return 1;              // effect
        case GNKind::Output:     return 2;
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

// AG-1 step 2 — authoritative topology edits (UI thread). Each flips the track to
// graph_authoritative (the graph, not the linear chain, is now the source of truth) and
// republishes to the audio thread via republish_track_graph. All hold t->gmtx while mutating
// agraph/agnodes; op lifetime follows the existing own/retire model (freed at shutdown).

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
    const int out = tr->agraph.output_id();
    if (out >= 0) {
        std::vector<int> preds;
        for (const vivid::audio::AudioGraphEdge& e : tr->agraph.edges())
            if (e.to_id == out) preds.push_back(e.from_id);
        for (int p : preds) { tr->agraph.disconnect(p, out); tr->agraph.connect(p, nid); }
        tr->agraph.connect(nid, out);
    }
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
    tr->agnodes.push_back({ GNKind::NativeInst, op });   // full range by default; set via key_range_set
    int out = tr->agraph.output_id();
    if (out < 0) {   // bare graph (no source yet): materialize the Output sink so the source is heard
        out = tr->agraph.add_node(false, true, nullptr, nullptr, "out");
        tr->agnodes.push_back({ GNKind::Output, nullptr });
        tr->agraph.set_output_id(out);
    }
    tr->agraph.connect(nid, out);                        // fan-in to Output (parallel with existing sources)
    tr->graph_authoritative = true;
    republish_track_graph(tr);
    return nid;
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
      if (tr->agnodes[idx].kind != GNKind::NativeFx) return 0;    // only effects removable
      retire = tr->agnodes[idx].op;
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
int session_audio_graph_connect(Session* s, int t, int from_id, int to_id) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (!tr->agraph.connect(from_id, to_id)) return 0;                 // dup / self-loop / bad id
    if (!republish_track_graph(tr)) { tr->agraph.disconnect(from_id, to_id); return 0; }  // cycle: revert
    tr->graph_authoritative = true;
    return 1;
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
float session_audio_graph_node_param_get(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_get(op, p);
    if (Vst3Handle* h = graph_node_handle(tr, node_id))
        return (h->controller && p >= 0 && p < static_cast<int>(h->params.size()))
                   ? static_cast<float>(h->controller->getParamNormalized(h->params[p].id)) : 0.f;
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size()))
                   ? static_cast<float>(clap_param_value(c, c->params[p].id)) : 0.f;
    return 0.f;
}
float session_audio_graph_node_param_min(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_min(op, p);
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size())) ? static_cast<float>(c->params[p].min) : 0.f;
    return 0.f;   // VST3 params are normalized
}
float session_audio_graph_node_param_max(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 1.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_max(op, p);
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size())) ? static_cast<float>(c->params[p].max) : 1.f;
    return 1.f;
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
int session_audio_graph_load_node(Session* s, int t, int kind, const char* op_type) {
    Track* tr = graph_track(s, t); if (!tr || !s->op_reg) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (static_cast<int>(tr->agraph.nodes().size()) + 1 > kGraphMaxNodes) return -1;
    vivid::AudioOp* op = nullptr;
    GNKind gk = GNKind::Output; bool is_src = false, is_out = false;
    if (kind == 2) { is_out = true; }   // output sink: no op
    else {
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
void session_audio_graph_load_edge(Session* s, int t, int from_id, int to_id) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.connect(from_id, to_id);
}
void session_audio_graph_finish_load(Session* s, int t, int output_id) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.set_output_id(output_id);
    tr->graph_authoritative = true;
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

// A small catalog of effects offered in the device-chain "+ FX" menu.
static const struct { const char* label; const char* match; } kEffectCatalog[] = {
    { "Yak Delay", "yak" }, { "CHOWTape", "chowtape" }, { "Portal", "portal" },
    { "Infiltrator", "infiltrator" }, { "Airwindows", "airwindows" },
};
int session_available_effect_count() { return static_cast<int>(sizeof(kEffectCatalog) / sizeof(kEffectCatalog[0])); }
const char* session_available_effect_name(int i) {
    return (i >= 0 && i < session_available_effect_count()) ? kEffectCatalog[i].label : "";
}
bool session_add_effect_by_index(Session* s, int t, int i) {
    if (!s || i < 0 || i >= session_available_effect_count()) return false;
    std::vector<std::string> bundles;
    list_vst3("/Library/Audio/Plug-Ins/VST3", bundles);
    if (const char* home = std::getenv("HOME"))
        list_vst3(std::string(home) + "/Library/Audio/Plug-Ins/VST3", bundles);
    for (const auto& b : bundles)
        if (name_has(b, kEffectCatalog[i].match)) return session_add_effect(s, t, b.c_str());
    return false;
}

// --- Dynamic tracks (create/delete) ---

// A small catalog of instruments offered in the "+ Track" menu.
static const struct { const char* label; const char* match; } kInstrumentCatalog[] = {
    { "Pigments", "pigments" }, { "Serum 2", "serum" }, { "Vital", "vital" },
    { "EZdrummer 3", "ezdrummer" }, { "Battery", "battery" },
};
int session_available_instrument_count() {
    return static_cast<int>(sizeof(kInstrumentCatalog) / sizeof(kInstrumentCatalog[0]));
}
const char* session_available_instrument_name(int i) {
    return (i >= 0 && i < session_available_instrument_count()) ? kInstrumentCatalog[i].label : "";
}

// Resolve `spec` (a catalog label, a plugin-name substring, or a .vst3 path) to a loaded
// instrument with a MIDI input. Returns nullptr if nothing matched/loaded.
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
    const char* match = spec;   // catalog label -> its match substring; else spec is the substring
    for (int i = 0; i < session_available_instrument_count(); ++i)
        if (sp == kInstrumentCatalog[i].label) { match = kInstrumentCatalog[i].match; break; }
    std::vector<std::string> bundles;
    list_vst3("/Library/Audio/Plug-Ins/VST3", bundles);
    if (const char* home = std::getenv("HOME"))
        list_vst3(std::string(home) + "/Library/Audio/Plug-Ins/VST3", bundles);
    const char* prefer[2] = { match, nullptr };
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
    for (int i = 0; i < 8; ++i) { at->aud_trim0[i].store(0.f); at->aud_trim1[i].store(1.f); }
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
