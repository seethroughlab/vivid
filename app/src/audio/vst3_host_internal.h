#pragma once
// ADR-0025: the shared session/track/graph internals of the vst3 host, extracted from
// vst3_host.cpp so cohesive groups can move into their own TUs. Private impl header — the
// public C API is vst3_host.h. All these types are RT-reachable: read docs/thread-safety.md.
#include "vst3_host_common.h"
#include "vst3_host.h"
#include "audio/analysis_ring.h"   // ADR-0029: atomic-slot spectrum ring (MeterState::an_ring)
#include "audio/node_ring_bank.h"  // ADR-0029: atomic-slot per-node capture rings (node_scope, node_an)
#include "audio/held_note_set.h"   // ADR-0029: atomic-slot polyphonic held-note set (Track::held)
#include "audio/audio_budgets.h"   // ADR-0031 §6: RT audio budgets (kDefaultMaxBlockFrames coupling)
#include "audio/pdc.h"             // ADR-0032 E1: PDC ring constants + delay primitive
#include "audio/note_event_ring.h" // discrete note on/off events (Track::note_events) for one-shot visuals
#include "midi/midi_clip.h"
#include "audio/audio_clip.h"
#include "audio/clip_dsp.h"
#include "audio/audio_op_runtime.h"
#include "audio/audio_graph.h"
#include "audio/clap_host.h"
#include "audio/plugin_catalog.h"
#include "audio/vst3_presets.h"   // PresetEntry (Track::preset_cache stores it by value) — keeps this header self-contained
#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#if defined(__APPLE__)
#include <dispatch/dispatch.h>   // dispatch_semaphore_t for the track-parallel audio worker pool
#endif
#include <deque>

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
// The per-node FFT-capture gate (Track::node_analyze_mask) packs one bit per node into a uint64_t, so a
// node index must fit in 64 bits. Raising kGraphMaxNodes past 64 needs a wider mask.
static_assert(kGraphMaxNodes <= 64, "node_analyze_mask is a uint64_t bitset over node indices");
constexpr uint32_t kGraphMaxBlock = 4096;
// ADR-0031 §6: audio_budgets().max_block_frames defaults to kDefaultMaxBlockFrames and is what the RT
// health counters treat as "oversized". Keep it pinned to this pool-stride authority at compile time.
static_assert(kGraphMaxBlock == vivid::audio::kDefaultMaxBlockFrames,
              "audio_budgets max block default must track kGraphMaxBlock (pool stride)");
// ADR-0032 E1: the PDC ring reserves one max block so the oldest read never aliases the newest write
// (delay + frames <= kPdcRingCap for any legal block). If kGraphMaxBlock grows past the reserve, bump
// kPdcRingCap.
static_assert(kGraphMaxBlock <= vivid::audio::kPdcRingCap - vivid::audio::kPdcMaxComp,
              "PDC ring reserve (kPdcRingCap - kPdcMaxComp) must cover one max audio block");
constexpr double   kTrackCaptureSeconds = 30.0;
// ADR-0015: capacity of ONE note buffer. Matches audio_op_runtime's kMaxNotes — a block that
// somehow carried more notes than this would be truncated rather than allocate on the RT thread.
constexpr size_t   kGraphMaxNotes = 512;
constexpr int      kScopeN        = 128;   // per-node output-waveform ring length (UI preview)
constexpr int      kScopePerBlock = 8;     // decimated samples pushed into the ring each block
// kAnalysisN / kFftBands live in the public vst3_host.h (shared with the frame-side FFT publisher).

// ADR-0025: the per-signal meter + spectral-analysis state, shared by a Track (its rendered output) and
// the Master (the summed mix) — one type instead of two identical field clusters. The audio thread writes
// it each block (see analyze_sample / publish_meters in vst3_host.cpp); the frame thread reads the atomics
// for meters + audio-reactive params, and snapshots `an_ring` for the FFT (mini_fft.h). `flt_lo`/`flt_hi`/
// `tr_baseline` are audio-thread-only running state (the crossover one-poles + the onset baseline). A torn
// read of an_ring is a harmless 1-frame spectral blip.
struct MeterState {
    std::atomic<float> level{0.f}, transient{0.f};
    std::atomic<float> band_low{0.f}, band_mid{0.f}, band_high{0.f};   // 3-band energy
    vivid::audio::AnalysisRing<kAnalysisN> an_ring;   // mono sample ring (frame-side FFT); atomic slots (ADR-0029)
    float              flt_lo = 0.f, flt_hi = 0.f, tr_baseline = 0.f;   // audio-thread running state
};

// MidiIn (ADR-0015): the track's note stream as an explicit NODE — clips + live MIDI + musical
// typing + MCP + preview, i.e. exactly what fills t.nev. It emits notes on a note edge instead of
// the old invisible per-track broadcast.
// MidiClip (ADR-0022 P3.1b): the clip scheduler + play-stop release flush AS A NODE — the MIDI
// mirror of the AudioClip, reading t.active. Split out of MidiIn so clips and live/preview are
// distinct note SOURCES; both feed the instrument's note-in (merged, time-sorted). Appended to
// keep every prior enumerator's value stable.
// Selector (ADR-0022 P3.2): a per-track-out note MUX — merges its note inputs (the scene clip
// nodes) into one note-out feeding the instrument, so instrument note fan-in stays low regardless
// of scene count. Selection is by SOURCE gating: only the active scene's clip node emits, so the
// merge is exactly the active clip. Appended to keep prior enumerator values stable.
// NativeGen (ADR-0022 P3.3): a note GENERATOR (Euclid/Chord/RandMelody) placed in a scene cell —
// an algorithmic note SOURCE gated by scene exactly like MidiClip, emitting its own notes from the
// transport. Reuses GNodeBind.op (the generator instance) + .scene. Appended to keep values stable.
enum class GNKind : uint8_t { NativeInst, NativeFx, Vst3Inst, Vst3Fx, ClapInst, ClapFx, Sampler, Output, MidiIn, NativeNoteFx, NativeMod, MidiClip, Selector, NativeGen };
// POD; trivially copyable (required for the try_lock swap of gbinds). `op` = native inst/fx;
// `handle` = VST3 inst/fx; `clap` = CLAP inst/fx (all raw, non-owning — Track owns them).
// AudioClip carries no pointer (its active clip is scene-dependent, re-read from Track& at process).
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
    // ADR-0022 P2b.3c: this node's SESSION-GLOBAL id (unique across every track + the master node),
    // -1 until assigned. Assigned on the AUTHORITATIVE publish path only (a derived linear-chain
    // track regenerates its local ids every rebuild and can't be cross-addressed, so it stays -1);
    // once a track is authoritative its nodes keep a stable gnid across republishes. The audio thread
    // never reads it — it is host/UI addressing (the substrate cross-track AUDIO edges name nodes by,
    // P2b.4) that rides along in the POD bind harmlessly.
    int32_t gnid = -1;
    // ADR-0022 P3.2b: for a MidiClip node, WHICH scene slot it represents (-1 = not a per-scene clip
    // node). The derived graph builds one MidiClip node per scene; only the node whose scene == the
    // track's active scene emits (the others gate to silence), so the shared scheduler still drives
    // exactly one live stream. Set on the UI thread, read on the audio thread (a plain int, published
    // via the gbinds swap like every other field).
    int32_t scene = -1;
};
// The audio thread swaps gbinds under a try_lock as a plain copy (reserved capacity, no move) —
// that is only RT-safe while the bind is trivially copyable. Adding a non-POD member here would
// silently break the real-time contract, so pin it.
static_assert(std::is_trivially_copyable<GNodeBind>::value, "GNodeBind must stay POD for the RT gbinds swap");

// ADR-0022 P2a.2: a SESSION-LEVEL cross-track control edge — a modulator on one track driving a param
// on ANOTHER track. `XCtlEdge` is the UI-thread authoritative record (stable ids, survives reorders).
// `XCtlApply` is its resolved form for the audio thread: read the source modulator's value at
// `src_pool_index` (absolute sample-0 index into Session::ctl_pool) and drive the dst node's param.
// The dst is addressed by (track id, compiled out_buf) so run_track_graph can match its current node.
struct XCtlEdge {
    int src_track_id = -1, src_node_id = -1;
    int dst_track_id = -1, dst_node_id = -1, dst_param = -1;
    vivid::audio::ControlShape shape;
};
struct XCtlApply {
    int    dst_track_id = -1, dst_out_buf = -1, dst_param = -1;
    size_t src_pool_index = 0;   // absolute sample-0 index into Session::ctl_pool (source track's region)
    vivid::audio::ControlShape shape;
};
static_assert(std::is_trivially_copyable<XCtlApply>::value, "XCtlApply must stay POD for the RT xctl swap");

// ADR-0022 P2b.4: a SESSION-LEVEL cross-track AUDIO edge — the output of a node on one track summed
// into a node on ANOTHER track. `XAudioEdge` is the UI-thread authoritative record (stable ids,
// survives reorders). `XAudioApply` is its resolved form for the audio thread: the source node's
// stereo output lives at `node_pool[src_pool_base + src_out_buf·2·frames]` (its track's region of the
// one session node pool — the same layout the source track renders into), and it is added into the
// dst node's summed input, matched by (dst_track_id, dst_out_buf). Unlike control (one block-rate
// number, fixed kGraphMaxBlock stride, sample 0), audio is a full buffer whose per-buffer stride is
// the block's `frames`, so only the region BASE is precomputed; the intra-region offset is applied
// live. `src_track_id` is carried so the block's render-order topo-sort can put the source first.
struct XAudioEdge {
    int src_track_id = -1, src_node_id = -1;
    int dst_track_id = -1, dst_node_id = -1;
};
struct XAudioApply {
    int    src_track_id = -1, dst_track_id = -1, dst_out_buf = -1;
    int    src_out_buf = -1;
    size_t src_pool_base = 0;   // source track's region base into Session::node_pool (kGraphMaxBlock-strided)
};
static_assert(std::is_trivially_copyable<XAudioApply>::value, "XAudioApply must stay POD for the RT xaudio swap");

// ADR-0022 P2b.5: a SESSION-LEVEL cross-track NOTE edge — a note-emitting node on one track (a
// MidiIn / note-effect / note-generating plugin) feeding a note-consuming node (an instrument / note
// effect) on ANOTHER track. `XNoteEdge` is the UI-thread authoritative record (stable ids).
// `XNoteApply` is its resolved form: `src_notes` points at the source node's note-out buffer (a stable
// slot in the source track's `npool` — the outer vector is sized once, so the inner-vector address is
// stable across blocks), which the consumer MERGES into its note input. Notes live in per-track `npool`
// (not a session pool like audio), so the resolved form carries the buffer pointer directly rather than
// a pool offset; it is re-resolved (like xaudio) whenever a track's plan or membership changes. The
// source track renders before the consumer (the block render-order topo-sort covers note edges too), so
// its buffer is current.
struct XNoteEdge {
    int src_track_id = -1, src_node_id = -1;
    int dst_track_id = -1, dst_node_id = -1;
};
struct XNoteApply {
    int src_track_id = -1, dst_track_id = -1, dst_out_buf = -1;
    std::vector<NoteEvent>* src_notes = nullptr;   // -> source node's npool note-out buffer (cleared if src idle)
};
static_assert(std::is_trivially_copyable<XNoteApply>::value, "XNoteApply must stay POD for the RT xnote swap");

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
    // ADR-0022 P2b.1: the session NODE-buffer pool + this track's region base (in floats). Each track
    // owns (kGraphMaxNodes+1) stereo buffers at node_pool[node_base ..]. Set per track in
    // session_process (== tracks_view index · the per-track stride).
    float* node_pool = nullptr; size_t node_base = 0;
    // ADR-0022 P2a.2: the published cross-track control edges (all of them; each node matches those
    // targeting it by dst_track_id + dst_out_buf). Empty ⇒ zero cost.
    const XCtlApply* xctl = nullptr; uint32_t xctl_count = 0;
    // ADR-0022 P2b.4: the published cross-track AUDIO edges (all of them; each node matches those
    // targeting it by dst_track_id + dst_out_buf). Empty ⇒ zero cost. The source track renders before
    // this one (block render-order topo-sort), so its region holds the rendered output.
    const XAudioApply* xaudio = nullptr; uint32_t xaudio_count = 0;
    // ADR-0022 P2b.5: the published cross-track NOTE edges (all of them; a note consumer matches those
    // targeting it by dst_track_id + dst_out_buf and merges the source's notes). Empty ⇒ zero cost.
    const XNoteApply* xnote = nullptr; uint32_t xnote_count = 0;
};

struct Session;   // ADR-0022 P2a.2 (Track back-pointer, for re-resolving cross-track edges on recompile)

struct Track {
    Session*              session = nullptr;   // set by rebuild_track_view; UI-thread only
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
    // ADR-0032 E1: playback plugin-delay compensation. A per-track stereo delay ring (planar: L block
    // then R block of kPdcRingCap each), applied at the master_mix seam so a track lagging behind a
    // higher-latency track is pulled back into alignment. `pdc_ring` stays empty (PDC inert) until a
    // recompute allocates it on the MAIN thread while pdc_delay is still 0 — then it is never resized, so
    // the audio thread never touches storage in flight. `pdc_w` is the audio-thread-only write cursor;
    // `pdc_delay` is the read-behind (main writes, audio reads relaxed; 0 = passthrough).
    std::vector<float>    pdc_ring;
    uint32_t              pdc_w = 0;
    std::atomic<int>      pdc_delay{0};
    // ADR-0033 P4: node solo / audition. `soloed_node_ids` is the UI-set state (stable node ids the
    // user is auditioning; guarded by gmtx like every agraph edit). `node_audible_mask` is the derived
    // per-node multiplier (bit i, i == node index == out_buf, 1 = audible): the union of each soloed
    // node's signal path (ancestors + node + descendants); ~0 when nothing is soloed. Recomputed on the
    // UI thread whenever the solo set or the graph topology changes, read by the audio thread in the
    // executor. Performance state — never persisted or undone (unlike track solo above).
    std::vector<int>      soloed_node_ids;
    std::atomic<uint64_t> node_audible_mask{~0ull};
    MeterState            meter;   // ADR-0025: level/transient/3-band/spectrum ring (shared with Master)
    // Note-derived bridge sources (the note peer of level/transient): the audio thread scans this
    // block's t.nev note stream and publishes the most-recent note-on's pitch/velocity + a note-on
    // flag, so MIDI notes can drive visual params DIRECTLY (pitch->colour etc.), not just via the
    // rendered signal. pitch/vel are HELD (a sustained note keeps its value); gate is a per-block
    // note-on pulse (the frame side decays it into a flash). Written audio-thread, read UI-thread.
    std::atomic<float>    note_pitch{0.f};   // last note-on pitch / 127 (0..1)
    std::atomic<float>    note_vel{0.f};     // last note-on velocity (0..1)
    std::atomic<float>    note_gate{0.f};    // 1.0 in a block containing a note-on, else 0.0
    // Polyphonic active-notes channel (the note instancer): the persistent set of currently-HELD notes,
    // maintained incrementally from t.nev's on/off events (audio thread), snapshotted by the frame thread.
    // Atomic-slot set (ADR-0029): a torn snapshot mixes whole notes, a benign 1-frame glitch, never UB.
    vivid::audio::HeldNoteSet<kMaxHeld> held;
    // Discrete note on/off EVENTS this block (the one-shot counterpart to `held`): a lock-free SPSC ring
    // pushed by the audio thread, drained by the frame thread → the note-event bus. Carries note_id, so a
    // re-struck held pitch fires again (membership can't). Sized well above notes-per-block.
    vivid::audio::NoteEventRing<128> note_events;
    // (spectrum ring + crossover state now live in `meter` above — MeterState, shared with Master.)
    std::vector<float>    bl, br;          // planar scratch
    std::mutex            capture_mtx;      // audio thread uses try_lock; UI snapshots may block
    std::vector<float>    capture_l, capture_r;
    uint32_t              capture_sample_rate = 0;
    size_t                capture_write_pos = 0;
    size_t                capture_filled = 0;
    std::vector<NoteEvent> nev;            // full per-block note stream = nev_clip ++ nev_live (broadcast fallback + blk.notes)
    std::vector<NoteEvent> nev_clip;       // ADR-0022 P3.1b: clip scheduler + release flush (feeds the MidiClip node)
    std::vector<NoteEvent> nev_live;       // ADR-0022 P3.1b: live MIDI + editor preview (feeds the MidiIn node)
    std::vector<NoteEvent> scene_rel;      // scene-switch note-offs for the CLAP path (VST3 gets them via vev)
    std::vector<ExprEvent> eev;            // per-note expression scratch (M3), pre-reserved
    // P4: clip-level controller events for this block. A CC is a CHANNEL message, so unlike notes
    // it is NOT key-range filtered per source node — every instrument on the track sees it.
    std::vector<CcEvent>   cev;            // this block's controllers = cev_clip ++ cev_live
    std::vector<CcEvent>   cev_clip;       // from the active clip's automation lanes
    std::vector<CcEvent>   cev_live;       // reserved for live hardware input (Phase D)
    Vst3EventList          vev;            // VST3 event list for this block (scene-switch releases +
                                           // notes); on the Track so both the inline path AND the
                                           // audio-graph Vst3Inst node dispatch share the same list.
    // Key-range routing scratch (a key-split track has >1 source, each voicing one pitch range).
    // Source nodes run sequentially in run_track_graph, so ONE filtered buffer per track is reused
    // across sources (like t.vev). Reserved off the audio thread; src_vev is fixed-capacity (256).
    std::vector<NoteEvent> src_nev;
    std::vector<ExprEvent> src_eev;
    std::vector<NoteEvent> ni_nev;         // native-instrument note scratch: [scene-switch releases ++ this block's notes]
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
    // ADR-0022 P2b.1: the node-buffer pool moved to the Session (`Session::node_pool`, per-track
    // regions) — the substrate a unified session executor + cross-track audio route through. A track's
    // nodes render into its region via `blk.node_pool + blk.node_base`.
    // ADR-0022 P2a.1: the CONTROL pool moved to the Session (`Session::ctl_pool`, per-track regions),
    // so cross-track modulation can read across track boundaries. A modulator writes its 0..1 signal
    // into this track's region; a consumer reads it via `blk.ctl_pool[blk.ctl_base + src_buf·...]`.
    // Per-node output-waveform scope (UI preview): the audio thread pushes kScopePerBlock decimated
    // samples of each node's output into a fixed ring (indexed by out_buf); the UI reads a snapshot to
    // draw a live waveform. Display-only; atomic-slot ring (ADR-0029), allocated eagerly at track init.
    vivid::audio::NodeRingBank        node_scope;       // kGraphMaxNodes rings of kScopeN
    // Gated per-node FFT: the UI sets a bit per node whose fft source is CONSUMED (wired/spawned); the
    // audio thread then captures that node's CONTIGUOUS block samples into node_an for the frame-side FFT.
    // Allocated ONCE on the UI thread when the mask first goes non-zero (the mask store is the release
    // barrier), so the RT thread only ever reads a stable, fully-allocated buffer. Unwatched nodes cost 0.
    std::atomic<uint64_t>            node_analyze_mask{0};   // bit i (== out_buf == node index) → capture node i
    vivid::audio::NodeRingBank        node_an;                // kGraphMaxNodes rings of kAnalysisN (lazy)
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
    // ADR-0022 P4.1: stable session-global ids for DERIVED-track nodes. The derived rebuild
    // regenerates 0-based local ids each time, so gnids are keyed by node ROLE (inst / sel /
    // cell:<scene> / midi / vfx:i / nfx:i / cfx:i / out) and cached here — a node keeps its gnid
    // across rebuilds. UI-thread only; the audio thread never reads gnid.
    std::unordered_map<std::string, int32_t> derived_gnid_by_role;
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
    // ADR-0022 P3.3: per-scene GENERATOR cells (a scene cell holds a clip OR a note generator).
    // `op == null` => the cell is a clip (the default; `clips[sc]` is its content). `op != null`
    // => a generator; the op instance is owned here and reaches the audio thread through the
    // NativeGen node's gbind (rebuild_track_graph republishes on place/remove). Sized to `scenes`,
    // reserved to kMaxScenes (append is RT-safe). UI/main-thread only. Retired ops go to op_retired.
    struct GenCell { vivid::AudioOp* op = nullptr; std::string type; };
    std::vector<GenCell>  gen_cells;
    // Audio track: no plugin; per-scene samples played transport-locked. `aud_clips` is
    // sized to `scenes` (an empty AudioClip = empty cell). Content edits (stash/place a
    // clip) happen on the UI thread under aud_mtx; the audio thread try_locks it around
    // render (skips a block on contention) — the UI critical section is an O(1) move.
    bool                  is_audio = false;
    std::vector<AudioClip>  aud_clips;
    std::vector<std::unique_ptr<ClipDsp>> aud_dsp;   // A2: per-slot warp stretcher (null until warp on)
    std::mutex            aud_mtx;
    std::atomic<float>    aud_trim0[kMaxScenes];   // per-scene loop window (fractions)
    std::atomic<float>    aud_trim1[kMaxScenes];
};

// A loose clip in the session-level pool (lives outside the track grid). Holds either a
// MIDI clip or an audio clip (AudioClip). UI-thread-only storage: the audio thread never
// reads `Session::pool`, so no edit-mirror is needed.
struct PoolClip { bool is_audio = false; MidiClip clip; AudioClip audio; std::string name; };

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
// keeps the mix bit-identical with the pre-P1b inline sum. The meters are `MeterState` — the same type
// a Track uses (ADR-0025) — since the master's analysis is identical to a track's.
struct Master {
    std::atomic<float> gain{1.f};
    MeterState         meter;   // ADR-0025: level/transient/3-band/spectrum ring (shared with Track)
    // ADR-0022 P2b.3c: the master's SESSION-GLOBAL node id — the sink of the one session graph, and
    // the first citizen of the global id space (assigned 0 at session init). `is_master` marks it as
    // the single session sink, as distinct from a track's `is_track_out` node (AudioGraph::is_output).
    int  gnid = -1;
    bool is_master = true;
};

// ADR-0022 P2b.3b: one entry in the FLAT session execution plan. The prior two per-track render
// loops (render each track → meter → then sum the master) are replaced by a single ordered list of
// these, walked by one executor. A `Node` step runs one compiled node of its track through
// process_step; a `Finalize` step copies that track's output into its track-out slot, taps the node
// scope, and computes its meters; the single trailing `Master` step sums the slots. For per-track
// islands (no cross-track audio edges yet) the list is exactly track0's nodes, track0 finalize,
// track1's nodes, … , master — bit-identical to the loops it replaces. P2b.4 topo-sorts this list so
// cross-track edges can interleave tracks; because every Node step is self-describing (its Track +
// node-pool region), the executor never special-cases a loop boundary.
struct FlatStep {
    enum Kind : uint8_t { Node, Finalize, Master } kind;
    Track*                             t;      // Node, Finalize
    const vivid::audio::CompiledStep*  node;   // Node
    uint32_t                           slot;   // Finalize (its track-out-pool slot)
    bool                               valid;  // Finalize: did the track's plan pass the RT bail-net
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
    // ADR-0022 P2b.3b: the flat session execution plan — one ordered list of steps across all render
    // tracks (see FlatStep). Audio-thread-only scratch, rebuilt each block; reserved so the per-block
    // clear + push_back never allocate (kMaxTracks tracks × up to kGraphMaxNodes node steps + one
    // finalize each, plus one master step).
    std::vector<FlatStep> session_plan;
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
    // ADR-0022 P2b.1: ONE session-owned NODE-buffer pool (was per-track Track::gpool), per-track
    // regions — track `i` owns region `i` (base = i·(kGraphMaxNodes+1)·2·kGraphMaxBlock floats; each
    // region holds (kGraphMaxNodes+1) stereo buffers, node outputs + 1 scratch). run_track_graph
    // renders into its region; a session executor + cross-track audio route through this one pool.
    std::vector<float>    node_pool;
    // ADR-0022 P2a.1b: scratch for the modulator pre-pass's discarded silent audio output (stereo,
    // kGraphMaxBlock). Modulators run one at a time, so one shared scratch suffices.
    std::vector<float>    mod_scratch;
    // ADR-0022 P2a.2: session-level cross-track control edges. `xctl_edges` is UI-thread
    // authoritative; `republish_xctl` resolves it into `xctl_ho` and the audio thread swaps that into
    // `xctl_view` (the P1b.2 handoff pattern) — the callback reads the resolved list, never the edges.
    std::vector<XCtlEdge>  xctl_edges;
    std::vector<XCtlApply> xctl_view, xctl_ho;
    std::mutex             xctl_mtx;
    std::atomic<uint64_t>  xctl_gen{0};
    uint64_t               xctl_gen_seen = 0;
    // ADR-0022 P2b.4: session-level cross-track AUDIO edges — the exact same handoff shape as the
    // control edges above. `xaudio_edges` is UI-thread authoritative; `republish_xaudio` resolves it
    // into `xaudio_ho` and the audio thread swaps that into `xaudio_view`. The render-order topo-sort
    // (session_process) reads xaudio_view to order source tracks before their consumers.
    std::vector<XAudioEdge>  xaudio_edges;
    std::vector<XAudioApply> xaudio_view, xaudio_ho;
    std::mutex               xaudio_mtx;
    std::atomic<uint64_t>    xaudio_gen{0};
    uint64_t                 xaudio_gen_seen = 0;
    // ADR-0022 P2b.5: session-level cross-track NOTE edges — same handoff shape as the audio/control
    // edges above. `xnote_edges` is UI-thread authoritative; `republish_xnote` resolves it into
    // `xnote_ho` and the audio thread swaps that into `xnote_view`.
    std::vector<XNoteEdge>   xnote_edges;
    std::vector<XNoteApply>  xnote_view, xnote_ho;
    std::mutex               xnote_mtx;
    std::atomic<uint64_t>    xnote_gen{0};
    uint64_t                 xnote_gen_seen = 0;
    std::atomic<uint64_t> tracks_gen{0};
    uint64_t              tracks_gen_seen = 0;
    int       next_track_id = 0;   // monotonic source of stable per-track IDs
    // ADR-0022 P2b.3c: monotonic source of SESSION-GLOBAL node ids (unique across every track's nodes
    // + the master). The master claims id 0 at init; authoritative-track nodes draw the rest. UI-thread
    // only. Unbounded/never reused (like a track's local id), so an edge saved against a gnid can never
    // silently re-bind to a different node.
    int       next_gnid = 0;
    int       scenes = 3;
    // ADR-0022 P3.3: per-scene display names (the "named" in "a scene is a NAMED set of bindings").
    // UI-thread only; NEVER read on the audio thread. Sized lazily to `scenes`; default "A","B",…
    std::vector<std::string> scene_names;
    // Scene-launch quantization: a queued scene switch takes effect at the next `launch_quantum_bars`-
    // bar boundary (1 = the next bar; typically 4 = let the phrase finish). `last_launch_q` tracks the
    // last quantum index seen on the audio thread so the boundary is detected once.
    std::atomic<int> launch_quantum_bars{1};
    long long        last_launch_q = -1;
    // ADR-0032 E1: playback plugin-delay compensation, off by default (a live instrument stays low-latency
    // unless the user opts in). Project-persisted like launch_quantum_bars — it changes the musical result.
    // When true, master_mix delays each compensable track by (L_max - L_track); see Track::pdc_ring.
    std::atomic<bool> pdc_enabled{false};
    // Published by pdc_recompute (main thread) for the diagnostics/get_health surface: L_max applied (the
    // whole compensated mix's added latency, samples), how many tracks are exactly compensated, how many
    // are left live (unknown-latency / live-input / cross-track), and whether any track's latency was
    // clamped to kPdcMaxComp. Read on the health thread (relaxed, display-only) like the Phase B numbers.
    std::atomic<int>  pdc_applied_delay{0};
    std::atomic<int>  pdc_tracks_comp{0};
    std::atomic<int>  pdc_tracks_live{0};
    std::atomic<bool> pdc_clamped{false};
    // Session music-theory context: root note + scale NAME (e.g. "C" + "minor"). The theory
    // vocabulary + validation live in the Python bridge (mcp/theory.py, ADR-0046); the core just
    // stores the two strings so the key/scale round-trips with the project. UI/main thread only.
    std::string      music_root  = "C";
    std::string      music_scale = "major";
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
    std::deque<ClapLoadReq>  clap_reqs;      // incoming loads; the poll ROUTES them (first -> main, rest -> bg)
    std::deque<ClapLoadReq>  clap_bg_reqs;   // routed to the async worker (only after JUCE is pinned to main)
    std::deque<ClapLoadDone> clap_done;
    std::atomic<bool>        clap_juce_pinned{false};   // true once the 1st CLAP was built on the MAIN thread
                                                        // (JUCE's message thread is bound to main — editors work)
    std::atomic<int>         clap_pending{0};    // requested-but-not-yet-applied loads
    bool                     clap_worker_stop = false;
    std::string              clap_last_error;    // main-thread only (last failed async load)
    std::vector<std::string> unresolved_instruments;  // main-thread only: display-names of instruments that
                                                       // failed to resolve on load (UX Ph6 F2); drained to a toast

    // --- Track-parallel audio executor (ADR-0052): a persistent RT worker pool so per-track DSP fans
    // out across cores instead of serializing on the one CoreAudio thread (2 heavy synths were tanking
    // the render framerate via preemption). Created once by session_set_audio_workgroup after the
    // device exists; joined in session_destroy. RT-safe: the per-block path only stores scalars, does
    // atomic fetch_add/fetch_sub, and posts/waits dispatch_semaphores — no alloc, no lock. ---
#if defined(__APPLE__)
    std::vector<std::thread> audio_workers;
    dispatch_semaphore_t     aw_go   = nullptr;   // master posts W times to wake W workers
    dispatch_semaphore_t     aw_done = nullptr;   // last participant posts once; master waits once
    std::atomic<uint32_t>    aw_next_slot{0};     // work-stealing task index into render_list
    std::atomic<int>         aw_remaining{0};     // participant barrier countdown
    std::atomic<bool>        aw_running{false};   // pool-alive flag (shutdown)
    int                      aw_n_workers = 0;    // persistent worker count (0 => always serial)
    bool                     aw_enabled   = true; // VIVID_AUDIO_WORKERS kill-switch (read once at start)
    void*                    aw_workgroup = nullptr;  // retained os_workgroup_t (or null → no RT join)
    uint32_t                 aw_frames = 0, aw_sr = 0, aw_n = 0;   // per-block params (published before go)
#endif
};

// Resolve a (session, track index) to the Track (or null). Defined in vst3_host.cpp; declared here so
// the extracted param TU (vst3_host_params.cpp) can reach it (ADR-0025 split). Caller-facing helper.
Track* graph_track(Session* s, int t);

// (Re)build a track's audio graph from its native chain + republish to the audio thread. Defined in
// vst3_host.cpp; declared here so the extracted CLAP-loader TU can rebuild a track when a plugin binds.
void rebuild_track_graph(Track* t);
// Async CLAP loader (defined in vst3_host_clap_loader.cpp; ADR-0025 split). enqueue_clap_load is
// called from the plugin-node add paths in vst3_host.cpp (the default slot marks a non-slot legacy
// load); stop_clap_loader joins the worker at session teardown.
int  enqueue_clap_load(Session* s, int t, bool is_instrument, const char* clap_path, const char* state, int slot = -1);
void stop_clap_loader(Session* s);

// --- Per-source/effect RENDER PRIMITIVES (defined in vst3_host_render.cpp; ADR-0025 split PR-B). Each
// runs one VST3/CLAP plugin for a block (or drains the notes it generated) — pure DSP over a handle.
// process_step + session_process (in vst3_host.cpp) call these, so the inline path and the audio-graph
// node dispatch share identical code (parity by construction). RT-safe: fixed scratch, no heap/lock.
void emit_vst3(Vst3EventList& events, const std::vector<NoteEvent>& nev, const std::vector<ExprEvent>& eev);
void filter_notes_by_range(const std::vector<NoteEvent>& src, uint8_t lo, uint8_t hi, std::vector<NoteEvent>& dst);
void filter_expr_by_range(const std::vector<ExprEvent>& src, uint8_t lo, uint8_t hi, std::vector<ExprEvent>& dst);
// `mod`/`mod_n` (ADR-0034): control-edge modulation resolved for this block — injected as param points
// after the UI param drain (so a wired param's modulation wins). nullptr/0 for an unmodulated node.
void render_vst3_instrument(Track& t, Vst3Handle* h, Vst3EventList& events, const VividAudioContext& ctx, uint32_t frames, float* L, float* R,
                            const ParamMsg* mod = nullptr, uint32_t mod_n = 0);
void render_vst3_effect(Track& t, Vst3Handle* fx, const VividAudioContext& ctx, uint32_t frames, float* L, float* R,
                        const ParamMsg* mod = nullptr, uint32_t mod_n = 0);
// `mod`/`mod_n` (ADR-0034): control-edge modulation resolved for this block — injected as param events
// after the param_q drain (so a wired param's modulation wins). nullptr/0 for an unmodulated node.
void render_clap_instrument(Track& t, ClapHandle* h, const std::vector<NoteEvent>& notes, uint32_t frames, float* L, float* R,
                            const ClapParamMsg* mod = nullptr, uint32_t mod_n = 0);
void render_clap_effect(Track& t, ClapHandle* h, uint32_t frames, float* L, float* R,
                        const ClapParamMsg* mod = nullptr, uint32_t mod_n = 0);
void drain_vst3_notes(Vst3Handle* h, std::vector<NoteEvent>& out);
void drain_clap_notes(ClapHandle* h, std::vector<NoteEvent>& out);

}  // namespace vivid::session
