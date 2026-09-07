// Multi-track session — N tracks, each a hosted VST3 instrument + per-scene MIDI
// clips, mixed (per-track gain) to the master output with bar-quantized launch.
// Built on classic's extracted host (vst3_host_common.h, anonymous namespace).
#include "vst3_host_common.h"
#include "vst3_host.h"
#include "midi/midi_clip.h"
#include "midi/note_record.h"   // P4 Phase E: apply_sustain / decimate_cc (pure, headless-tested)
#include "audio/audio_clip.h"
#include "audio/sample_engine/sample_decode.h"        // decode_audio_native (direct WAV→AudioClip-node load)
#include "audio/clip_dsp.h"                           // A2: per-clip warp stretcher (ClipDsp + process_clip)
#include "audio/audio_op_runtime.h"                   // AO-1: native audio operators (opaque; no operator_api leak)
#include "audio/sampler_op.h"                          // ADR-0049: SamplerInfo/SamplerSlice (read API)
#include "audio/audio_graph.h"                        // AG-0: per-track audio signal graph (ADR-0012)
#include "audio/plugin_hang_monitor.h"                 // ADR-0045 Tier 2a: the in-flight beacon (PluginInFlight)
#include "audio/audio_health.h"                        // ADR-0031 §3: RT health counters (note_handoff_skip)
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
#if defined(__APPLE__)
#include <os/workgroup.h>   // ADR-0052: aux RT audio worker threads join the CoreAudio device workgroup
#endif
#include <cctype>
#include <algorithm>
#include <utility>
#include <filesystem>
#include <dirent.h>

#include "audio/vst3_host_internal.h"   // ADR-0025: shared session/track/graph internals (extracted)
using namespace Steinberg;
using namespace Steinberg::Vst;

// ADR-0045 Tier 2a: the process-global plugin-fault ring (RT/monitor push → frame drain) and the
// in-flight beacon the hang monitor watches. One audio engine per process, like the other RT singletons.
namespace vivid::audio {
PluginFaultRing<64>& plugin_fault_ring() {
    static PluginFaultRing<64> ring;
    return ring;
}
PluginInFlight& plugin_inflight() {
    static PluginInFlight beacon;
    return beacon;
}
}  // namespace vivid::audio

namespace vivid::session {


static void recompute_mix_scales(Session* s);   // ADR-0022 P1b.4 (defined below)
static void recompute_node_audible(Track* t);   // ADR-0033 P4 node solo (defined below)
static void republish_xctl(Session* s);         // ADR-0022 P2a.2 (defined below)
static void republish_xaudio(Session* s);        // ADR-0022 P2b.4 (defined below)
static void republish_xnote(Session* s);         // ADR-0022 P2b.5 (defined below)

// ADR-0032 E1.1: (re)classify tracks and publish per-track PDC delays. Defined below (needs
// track_plugin_latency_sum); forward-declared so the membership/graph-edit republish sites can call it.
static void pdc_recompute(Session* s);

// Republish the current track membership for the audio thread (UI/main thread only).
// Call after any add/remove; the audio thread picks it up on its next block.
static void rebuild_track_view(Session* s) {
    // ADR-0022 P1b.4: membership changed — a new track must respect an active solo, and dropping a
    // soloed track must un-silence the rest. Recompute before publishing the new view.
    recompute_mix_scales(s);
    // ADR-0022 P2a.2/P2b.4/P2b.5: track POSITIONS moved → cross-track source regions/buffers changed → re-resolve.
    republish_xctl(s);
    republish_xaudio(s);
    republish_xnote(s);
    {
        std::lock_guard<std::mutex> lk(s->tracks_mtx);
        s->tracks_pub.clear();
        for (auto& tp : s->tracks) { tp->session = s; s->tracks_pub.push_back(tp.get()); }   // P2a.2 back-pointer
        s->tracks_gen.fetch_add(1, std::memory_order_release);
    }
    pdc_recompute(s);   // ADR-0032 E1.1: membership changed → reclassify + republish PDC delays
}

static Vst3Handle* load_effect(const std::string& path, uint32_t sr, Vst3HostApp* host) {
    Vst3Handle* h = vst3_load_plugin(path.c_str(), "", sr, std::string(), host, /*as_effect*/true);
    if (!h) return nullptr;
    if (h->processor->setProcessing(true) != kResultOk) {}
    h->processing = true;
    return h;
}

static bool name_has(const std::string& path, const char* lower_needle) {
    std::string p = path;
    for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return p.find(lower_needle) != std::string::npos;
}

// An audio track keeps exactly `scenes` clip slots (an empty AudioClip = empty cell), so
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
    // ADR-0022 P2b.1: the node-buffer pool is now session-owned (Session::node_pool, per-track
    // regions), sized once at session init — no per-track allocation here.
    t->node_scope.allocate(kGraphMaxNodes, kScopeN);   // ADR-0029: atomic-slot per-node scope rings
    t->src_nev.reserve(256);   t->src_eev.reserve(256);   // key-range filter scratch (>= any block's note count)
    // P4: controller events per block — bounded by the lane cap plus whatever live input adds.
    t->cev.reserve(kMaxCcLanes + 64);  t->cev_clip.reserve(kMaxCcLanes + 8);  t->cev_live.reserve(64);
    t->ni_nev.reserve(kGraphMaxNotes);   // native-instrument scene-release prepend scratch (RT: no alloc)
    // ADR-0015: the note pool — one note list per possible note-emitting node, each reserved to
    // kGraphMaxNotes. Preallocated to the same worst case as the audio pool, so the audio thread
    // only ever clear()s and push_back()s within capacity: no allocation in the callback.
    t->npool.resize(kGraphMaxNodes);
    for (auto& nb : t->npool) nb.reserve(kGraphMaxNotes);
    t->nmerge.reserve(kGraphMaxNotes);
    // Ph2 P3-02: pre-size the VST3/CLAP effect I/O scratch to the worst-case block up front, so the
    // per-block effect render never resizes (allocates) on the audio hot path. session_process bails
    // on frames > kGraphMaxBlock, so this is always large enough.
    t->fxl.assign(kGraphMaxBlock, 0.f);   t->fxr.assign(kGraphMaxBlock, 0.f);
    // ADR-0022 P2a.1: the control pool is now session-owned (Session::ctl_pool, per-track regions),
    // sized once at session init — no per-track allocation here. `ctl_pub` (the UI live-dot atomics)
    // stays per-track.
    t->ctl_pub.reset(new std::atomic<float>[kGraphMaxNodes]);
    for (int i = 0; i < kGraphMaxNodes; ++i) t->ctl_pub[i].store(0.f, std::memory_order_relaxed);
}

static void configure_track_capture(Track* t, uint32_t sample_rate) {
    if (!t || sample_rate == 0) return;
    const size_t frames = static_cast<size_t>(kTrackCaptureSeconds * static_cast<double>(sample_rate));
    std::lock_guard<std::mutex> lk(t->capture_mtx);
    t->capture_l.assign(frames, 0.f);
    t->capture_r.assign(frames, 0.f);
    t->capture_sample_rate = sample_rate;
    t->capture_write_pos = 0;
    t->capture_filled = 0;
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

// ADR-0022 P2b.3c: give every node on this (authoritative) track a session-global id. Assigns to any
// agnode still at -1 (a newly added node), leaving already-assigned ids stable — so a node keeps its
// gnid across every republish of an authoritative track. Only the authoritative path calls this: a
// derived linear-chain track regenerates its local ids each rebuild and can't be cross-addressed, so
// its nodes stay at -1. UI-thread only (draws from Session::next_gnid). Behaviour-inert — the audio
// thread never reads gnid.
static void assign_node_gnids(Track* t) {
    if (!t->session) return;   // pre-construction: no cross-track edge can exist yet; assigned later
    for (GNodeBind& nb : t->agnodes)
        if (nb.gnid < 0) nb.gnid = t->session->next_gnid++;
}

// ADR-0022 P4.1: give a DERIVED track's nodes STABLE session-global ids. The derived rebuild
// regenerates local ids from scratch, so a monotonic assign would reshuffle gnids every rebuild.
// Instead each node's gnid is keyed by its ROLE (a stable identity for the derived graph): the
// instrument, the per-track-out selector, each scene cell (by scene index), MidiIn, each FX (by
// family + position), and the output. A role keeps its gnid across rebuilds via the per-track
// cache; a new role (e.g. an added scene cell) draws a fresh gnid. Behaviour-inert — gnid is
// host/UI addressing only; the audio thread never reads it.
static void assign_derived_gnids(Track* t) {
    if (!t->session) return;
    int vfx = 0, nfx = 0, cfx = 0, note = 0, mod = 0;
    for (GNodeBind& nb : t->agnodes) {
        std::string role;
        switch (nb.kind) {
            case GNKind::NativeInst: case GNKind::Vst3Inst:
            case GNKind::ClapInst:   case GNKind::Sampler:   role = "inst"; break;
            case GNKind::Selector:                           role = "sel";  break;
            case GNKind::MidiClip:   case GNKind::NativeGen:  role = "cell:" + std::to_string(nb.scene); break;
            case GNKind::MidiIn:                             role = "midi"; break;
            case GNKind::Vst3Fx:                             role = "vfx:" + std::to_string(vfx++); break;
            case GNKind::NativeFx:                           role = "nfx:" + std::to_string(nfx++); break;
            case GNKind::ClapFx:                             role = "cfx:" + std::to_string(cfx++); break;
            case GNKind::NativeNoteFx:                       role = "note:" + std::to_string(note++); break;
            case GNKind::NativeMod:                          role = "mod:" + std::to_string(mod++); break;
            case GNKind::Output:                             role = "out";  break;
        }
        auto it = t->derived_gnid_by_role.find(role);
        nb.gnid = (it != t->derived_gnid_by_role.end()) ? it->second
                                                        : (t->derived_gnid_by_role[role] = t->session->next_gnid++);
    }
}

// ADR-0022 P3.x uniformity: the note sub-graph (per-scene Clip/Gen source → Selector, live MidiIn) is
// ENGINE-MANAGED infrastructure, exactly like the Output node — reconciled to match current state on
// every graph publish, so note sources appear and route identically on derived AND authoritative
// graphs (a bare `add_source` instrument no longer routes notes through the invisible t.nev fallback).
// Pure agraph/agnodes mutation; caller holds t->gmtx; no compile/publish. Idempotent.
//
// Infra note fan-out is engine-OWNED (mirrors Output's fan-in): a user cannot hand-wire a Selector/
// MidiIn source edge and have it persist — reconcile recomputes those two nodes' fan-out each publish,
// wiring them to every note consumer that has no OTHER (user) note source, and dropping them from any
// consumer a user note-fx (Arp/chord) now feeds. Returns false and changes NOTHING if the full
// sub-graph won't fit kGraphMaxNodes (a partial one would route through a broken Selector instead of
// falling back to t.nev).
static bool reconcile_note_subgraph(Track* t) {
    using EK = vivid::audio::EdgeKind;
    auto& g = t->agraph;
    auto& an = t->agnodes;
    auto id_at   = [&](int idx) { return g.nodes()[idx].id; };
    auto is_infra = [](GNKind k) { return k == GNKind::Selector || k == GNKind::MidiIn ||
                                          k == GNKind::MidiClip || k == GNKind::NativeGen; };
    auto is_inst  = [](GNKind k) { return k == GNKind::NativeInst || k == GNKind::Sampler ||
                                          k == GNKind::Vst3Inst   || k == GNKind::ClapInst; };
    auto find_kind = [&](GNKind k) -> int {
        for (size_t i = 0; i < an.size(); ++i) if (an[i].kind == k) return static_cast<int>(i);
        return -1;
    };
    auto find_scene = [&](int sc) -> int {
        for (size_t i = 0; i < an.size(); ++i)
            if ((an[i].kind == GNKind::MidiClip || an[i].kind == GNKind::NativeGen) && an[i].scene == sc)
                return static_cast<int>(i);
        return -1;
    };
    auto has_user_note_src = [&](int id, int sel, int mid) -> bool {   // a note edge into id NOT from infra
        for (const auto& e : g.edges())
            if (e.to_id == id && e.kind == EK::Note && e.from_id != sel && e.from_id != mid) return true;
        return false;
    };
    auto remove_at = [&](int idx) { g.remove_node(id_at(idx)); an.erase(an.begin() + idx); };
    auto scene_gen = [&](int sc) -> vivid::AudioOp* {
        return sc < static_cast<int>(t->gen_cells.size()) ? t->gen_cells[sc].op : nullptr;
    };
    // A scene "slot" is POPULATED iff it holds a clip with notes or a placed generator. An empty slot
    // has no Clip node in the graph — that is the first-class "empty slot".
    auto scene_populated = [&](int sc) -> bool {
        if (scene_gen(sc)) return true;
        return sc < static_cast<int>(t->edit_clips.size()) && !t->edit_clips[sc].notes.empty();
    };

    // Note-consuming instruments present in the graph (by kind).
    std::vector<int> insts;
    if (!t->is_audio)
        for (size_t i = 0; i < an.size(); ++i) if (is_inst(an[i].kind)) insts.push_back(id_at(static_cast<int>(i)));

    // Audio track, or no instrument yet → tear down any infra note nodes and return.
    if (t->is_audio || insts.empty()) {
        for (int i = static_cast<int>(an.size()) - 1; i >= 0; --i) if (is_infra(an[i].kind)) remove_at(i);
        return true;
    }

    const int nscenes = std::clamp(static_cast<int>(t->edit_clips.size()), 1, kMaxScenes);

    // All-or-nothing budget preflight: count infra nodes we'd ADD (flips/edges cost no nodes).
    int need = (find_kind(GNKind::Selector) < 0 ? 1 : 0) + (find_kind(GNKind::MidiIn) < 0 ? 1 : 0);
    for (int sc = 0; sc < nscenes; ++sc) if (scene_populated(sc) && find_scene(sc) < 0) ++need;
    if (static_cast<int>(g.nodes().size()) + need > kGraphMaxNodes) return false;

    // Ensure the instruments declare a note-in port (so the fan-out below recognizes them).
    for (int id : insts) g.set_note_ports(id, /*note_in*/true, /*note_out*/false);

    // Ensure at least one Selector + one MidiIn (adopt an existing/manual one; never delete extras).
    int sel = find_kind(GNKind::Selector);
    if (sel < 0) { an.push_back({ GNKind::Selector, nullptr, nullptr });
                   const int id = g.add_node(true, false, nullptr, nullptr, "sel"); g.set_note_ports(id, true, true);
                   sel = static_cast<int>(an.size()) - 1; }
    int mid = find_kind(GNKind::MidiIn);
    if (mid < 0) { an.push_back({ GNKind::MidiIn, nullptr, nullptr });
                   const int id = g.add_node(true, false, nullptr, nullptr, "midi"); g.set_note_ports(id, false, true);
                   mid = static_cast<int>(an.size()) - 1; }
    const int sel_id = id_at(sel), mid_id = id_at(mid);

    // One scene node per POPULATED scene in 0..nscenes-1 (empty slots get none): NativeGen if a
    // generator is placed, else MidiClip. Flip the kind IN PLACE (same AudioGraph node → id +
    // scene→Selector edge survive). Wire → Selector.
    for (int sc = 0; sc < nscenes; ++sc) {
        if (!scene_populated(sc)) continue;
        vivid::AudioOp* gop = scene_gen(sc);
        int idx = find_scene(sc);
        if (idx < 0) {
            GNodeBind cb; cb.scene = sc; cb.kind = gop ? GNKind::NativeGen : GNKind::MidiClip; cb.op = gop;
            an.push_back(cb);
            const int id = g.add_node(true, false, nullptr, nullptr, gop ? "gen" : "clip");
            g.set_note_ports(id, false, true);
            idx = static_cast<int>(an.size()) - 1;
        } else {
            an[idx].kind = gop ? GNKind::NativeGen : GNKind::MidiClip; an[idx].op = gop;   // in-place flip
        }
        g.connect(id_at(idx), sel_id, EK::Note);   // idempotent
    }
    // Drop scene nodes for EMPTY or out-of-range scenes (empty-slot + orphan cleanup).
    for (int i = static_cast<int>(an.size()) - 1; i >= 0; --i) {
        if (an[i].kind != GNKind::MidiClip && an[i].kind != GNKind::NativeGen) continue;
        const int sc = an[i].scene;
        if (sc < 0 || sc >= nscenes || !scene_populated(sc)) remove_at(i);
    }

    // Fan-out (per-node, minimal-touch): wire Selector+MidiIn into every note consumer with no OTHER
    // note source; drop them from any that a user note-fx now feeds. Only ever touches the two infra
    // edges — never a user edge.
    for (size_t i = 0; i < an.size(); ++i) {
        if (is_infra(an[i].kind)) continue;
        if (!g.nodes()[i].note_in) continue;           // instruments + note-fx (Arp/chord/transpose)
        const int nid = id_at(static_cast<int>(i));
        if (has_user_note_src(nid, sel_id, mid_id)) { g.disconnect(sel_id, nid, EK::Note); g.disconnect(mid_id, nid, EK::Note); }
        else                                        { g.connect(sel_id, nid, EK::Note);    g.connect(mid_id, nid, EK::Note); }
    }
    return true;
}

static bool republish_track_graph(Track* t) {
    reconcile_note_subgraph(t);                          // engine-managed note sources (uniform on every publish)
    if (!t->agraph.compile(t->gcg_edit)) return false;   // cycle → published plan unchanged
    assign_node_gnids(t);                                 // ADR-0022 P2b.3c: session-global ids
    t->gbinds_edit = t->agnodes;                          // parallel to nodes(): index == out_buf
    t->gok_edit    = true;
    recompute_node_audible(t);                            // ADR-0033 P4: node indices moved → rebuild the solo mask
    publish_track_plan(t);
    // ADR-0022 P2a.2/P2b.4: this track's node indices / buffers may have moved — re-resolve any
    // cross-track edges that reference it. (t->session is null only during initial track construction,
    // before any cross-track edge can exist.)
    republish_xctl(t->session);
    republish_xaudio(t->session);
    republish_xnote(t->session);
    // ADR-0032 E1.1: a plugin add/remove/bind or a routing edit can change a track's latency or its
    // linear/live classification → recompute PDC. (t->session is null only during initial construction.)
    if (t->session) pdc_recompute(t->session);
    return true;
}

// Bind the track's loaded plugin handles into its authoritative-graph placeholder nodes. A saved
// authoritative graph reloads its topology as bare Vst3Inst/ClapInst/Vst3Fx/ClapFx placeholders
// (session_audio_graph_load_node) because the plugins load asynchronously; when they land
// (session_poll_plugin_loads) their handles must be threaded back into those nodes before the plan
// republishes, or the graph keeps a null source/effect and stays silent. Matches placeholders to
// handles by kind + node order (the seed built them in the same order the plugins are saved/loaded).
// Caller MUST hold t->gmtx. Non-plugin nodes (native ops / AudioClip / Output) are left untouched.
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

void rebuild_track_graph(Track* t) {   // ADR-0025: external (declared in vst3_host_internal.h) so the extracted CLAP-loader TU can rebuild on bind
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
    // ADR-0022 P3.2b: one MidiClip node PER SCENE (edit_clips is sized to the scene count on the UI
    // thread — where this runs — and grown by session_add_scene, so it is the reliable count here).
    const int  nscenes = t->is_audio ? 0 : std::clamp(static_cast<int>(t->edit_clips.size()), 1, kMaxScenes);
    const int  node_count = 1 + static_cast<int>(t->effects_edit.size())
                              + static_cast<int>(t->op_effects_edit.size())
                              + static_cast<int>(t->clap_effects.size()) + 1   // source + VST3 FX + native FX + CLAP FX + output
                              + (t->is_audio ? 0 : (2 + nscenes));   // ADR-0022 P3.1a/b/P3.2a/b: + MidiIn + Selector + one MidiClip per scene
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
    // ADR-0022 P2b.5: declare the instrument's NOTE-IN port (it consumes notes) so a cross-track note
    // edge can target it. Metadata only — bit-identical: graph_note_input resolves note inputs from note
    // EDGES (n_note_in), not this flag, so a derived instrument with no note edge still reads its own
    // stream. A AudioClip (is_audio) ignores notes, so it is left without a note-in port.
    //
    // ADR-0022 P3.1a/b + P3.2a — note production becomes graph nodes. Build the derived note
    // sub-graph feeding the instrument through NOTE edges, replacing the invisible per-track t.nev
    // broadcast fallback for derived instrument tracks:
    //   MidiClip (t.nev_clip = clip scheduler + release flush; the MIDI mirror of the AudioClip)
    //       → Selector (per-track-out note MUX) → instrument
    //   MidiIn   (t.nev_live = live MIDI + typing + MCP + preview) → instrument  (live is NOT
    //                                                                scene-gated, so it bypasses
    //                                                                the Selector)
    // The Selector collapses the clip fan-in to one edge into the instrument (P3.2b fans MidiClip
    // into N per-scene nodes behind it); today it passes the single clip stream through. Together
    // Selector(clip)+MidiIn(live) == the pre-split t.nev, so the instrument receives the same notes
    // — every event keeps its exact pitch/vel/offset/id. All edge kinds constrain topo order, so
    // each node renders before its consumer; the instrument reads via graph_note_input's merge path,
    // which TIME-SORTS by sample offset (what plugins and scanning native ops require). The broadcast
    // fallback is untouched — just no longer reached. A AudioClip (is_audio) gets none of this.
    // Note sources are ENGINE-MANAGED infrastructure — build them via the shared reconcile so the
    // derived and authoritative paths are identical: a Selector fed by one Clip/Gen node per POPULATED
    // scene (an empty slot gets no node), plus a live MidiIn, wired into the instrument. Runs here —
    // right after the instrument, before the FX loops — so agnodes/node order is unchanged. reconcile
    // no-ops for an audio track (is_audio) and when there is no note-consuming instrument.
    reconcile_note_subgraph(t);
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
    assign_derived_gnids(t);                      // ADR-0022 P4.1: stable session-global ids (by role)
    t->gbinds_edit = t->agnodes;                 // parallel to nodes(): index == out_buf
    t->gok_edit = t->agraph.compile(t->gcg_edit);
    publish_track_plan(t);
    // ADR-0022 P2b.4/P2b.5 FIX: this DERIVED track's graph was just (re)built, so its node indices /
    // note buffers may have moved — re-resolve any cross-track edges that reference it, exactly as the
    // authoritative republish_track_graph does. Without this, a cross-track edge restored while this
    // track had no instrument yet (empty agraph → node_index=-1 → unresolved) is never re-resolved once
    // the derived graph lands, so it silently fails to route after a session reload.
    republish_xctl(t->session);
    republish_xaudio(t->session);
    republish_xnote(t->session);
}

// The per-source/effect render primitives (emit_vst3 / filter_* / render_vst3_* / render_clap_* /
// drain_*_notes) are declared in vst3_host_internal.h and defined in vst3_host_render.cpp (ADR-0025
// split). The sampler source render stays here (it reaches into the Track's clip/warp state).
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
static const std::vector<NoteEvent>& graph_note_input(Track& t, const vivid::audio::CompiledStep& s,
                                                      const GraphBlockCtx& b) {
    // ADR-0022 P2b.5: cross-track NOTE sources targeting this node (merged in ADDITION to local notes).
    bool has_x = false;
    for (uint32_t x = 0; x < b.xnote_count; ++x)
        if (b.xnote[x].dst_track_id == t.id && b.xnote[x].dst_out_buf == s.out_buf) { has_x = true; break; }
    const int n = static_cast<int>(t.npool.size());
    // Fast path — no cross-track note edge: exactly the pre-P2b.5 behavior (return a buffer by
    // reference, no copy) for the common single-source cases; multi-input falls through to the merge.
    if (!has_x) {
        if (s.n_note_in <= 0) return t.nev;
        if (s.n_note_in == 1) {
            const int b0 = s.note_in_buf[0];
            return (b0 >= 0 && b0 < n) ? t.npool[static_cast<size_t>(b0)] : t.nev;
        }
    }
    // Merge path: the node's local note inputs + any cross-track sources, sorted by sample offset.
    t.nmerge.clear();
    auto append = [&](const std::vector<NoteEvent>& src) {
        for (const NoteEvent& e : src) {
            if (t.nmerge.size() >= kGraphMaxNotes) break;   // truncate rather than allocate
            t.nmerge.push_back(e);
        }
    };
    if (s.n_note_in <= 0) append(t.nev);   // no local note edge, but a cross-track one exists → its own stream + cross-track
    else for (int k = 0; k < s.n_note_in; ++k) {
        const int bk = s.note_in_buf[k];
        if (bk >= 0 && bk < n) append(t.npool[static_cast<size_t>(bk)]);
    }
    for (uint32_t x = 0; x < b.xnote_count; ++x) {
        const XNoteApply& xa = b.xnote[x];
        if (xa.dst_track_id == t.id && xa.dst_out_buf == s.out_buf && xa.src_notes) append(*xa.src_notes);
    }
    std::sort(t.nmerge.begin(), t.nmerge.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.sample_offset < b.sample_offset; });
    return t.nmerge;
}

// ADR-0022: resolve a step's control inputs into param OVERRIDES before the node runs. Reads each
// driving modulator's 0..1 value from the session control pool region (`b.ctl_pool + b.ctl_base`),
// combines it with the LIVE BASE (`audio_op_param_get` reads pvals, never written on this thread),
// and stacks (two modulators on one param each add their swing). Shared by the modulator pre-pass and
// the audio render loop. Returns the override count. Empty (returns 0) for any unmodulated node.
static uint32_t resolve_control_inputs(const vivid::audio::CompiledStep& s, const GNodeBind& nb,
                                       const GraphBlockCtx& b, vivid::AudioOpParamOverride* ovr) {
    uint32_t novr = 0;
    if (s.n_control_in > 0 && nb.op) {
        for (int k = 0; k < s.n_control_in; ++k) {
            const vivid::audio::ControlIn& ci = s.control_in[k];
            if (ci.src_buf < 0 || ci.param < 0) continue;
            const float src = b.ctl_pool[b.ctl_base + static_cast<size_t>(ci.src_buf) * kGraphMaxBlock];   // sample 0
            const float lo  = vivid::audio_op_param_min(nb.op, ci.param);
            const float hi  = vivid::audio_op_param_max(nb.op, ci.param);
            int slot = -1;
            for (uint32_t j = 0; j < novr; ++j) if (ovr[j].param == ci.param) { slot = static_cast<int>(j); break; }
            // ADR-0030 Phase 2: resolve on top of the EFFECTIVE base — a frame-bridge override when
            // active, else the authored base — so a mapped param the user also modulates composes.
            const float base = slot >= 0 ? ovr[slot].value : vivid::audio_op_param_effective(nb.op, ci.param);
            const float v = vivid::audio::control_resolve(base, src, ci.shape, lo, hi);
            if (slot >= 0) ovr[slot].value = v;
            else if (novr < vivid::audio::kMaxControlInputs) ovr[novr++] = { ci.param, v };
        }
    }
    return novr;
}

// ADR-0034: resolve this block's control inputs for a CLAP plugin node into param events (plain
// units, keyed by clap_id), reading the EFFECTIVE base (`aeff_load` — bridge value if active, else authored) instead of the native
// op's pvals. Same combine as native — in-track edges + cross-track edges, stacking on one param —
// but the output is delivered through the plugin's event list (render_clap_*), not the native
// override array. Returns the count; 0 for an unmodulated node or one with no captured base yet.
static uint32_t resolve_clap_control(const vivid::audio::CompiledStep& s, const GNodeBind& nb,
                                     const GraphBlockCtx& b, int dst_track_id, ClapParamMsg* out) {
    if (!nb.clap) return 0;
    uint32_t n = 0;
    auto stack = [&](int pidx, float src, const vivid::audio::ControlShape& sh) {
        if (pidx < 0 || pidx >= static_cast<int>(nb.clap->params.size())) return;
        double base; if (!nb.clap->aeff_load(pidx, base)) return;   // effective base (bridge if active, else authored)
        const clap_id id = nb.clap->params[pidx].id;
        const float lo = static_cast<float>(nb.clap->params[pidx].min), hi = static_cast<float>(nb.clap->params[pidx].max);
        int slot = -1;
        for (uint32_t j = 0; j < n; ++j) if (out[j].id == id) { slot = static_cast<int>(j); break; }
        const float cur = slot >= 0 ? static_cast<float>(out[slot].value) : static_cast<float>(base);
        const float v = vivid::audio::control_resolve(cur, src, sh, lo, hi);
        if (slot >= 0) out[slot].value = v;
        else if (n < vivid::audio::kMaxControlInputs) out[n++] = { id, static_cast<double>(v) };
    };
    for (int k = 0; k < s.n_control_in; ++k) {   // in-track control edges
        const vivid::audio::ControlIn& ci = s.control_in[k];
        if (ci.src_buf < 0 || ci.param < 0) continue;
        stack(ci.param, b.ctl_pool[b.ctl_base + static_cast<size_t>(ci.src_buf) * kGraphMaxBlock], ci.shape);
    }
    for (uint32_t x = 0; x < b.xctl_count; ++x) {   // cross-track control edges targeting this node
        const XCtlApply& xa = b.xctl[x];
        if (xa.dst_track_id != dst_track_id || xa.dst_out_buf != s.out_buf || xa.dst_param < 0) continue;
        stack(xa.dst_param, b.ctl_pool[xa.src_pool_index], xa.shape);
    }
    return n;
}

// ADR-0034: the VST3 twin of resolve_clap_control — resolve a VST3 node's control edges into
// normalized param points (VST3 params are 0..1), keyed by ParamID, injected into the plugin's
// Vst3ParamChanges by render_vst3_*. Reads the EFFECTIVE base (`aeff_load`). Empty (0) for an
// unmodulated node or one without a captured base.
static uint32_t resolve_vst3_control(const vivid::audio::CompiledStep& s, const GNodeBind& nb,
                                     const GraphBlockCtx& b, int dst_track_id, ParamMsg* out) {
    if (!nb.handle) return 0;
    uint32_t n = 0;
    auto stack = [&](int pidx, float src, const vivid::audio::ControlShape& sh) {
        if (pidx < 0 || pidx >= static_cast<int>(nb.handle->params.size())) return;
        float base; if (!nb.handle->aeff_load(pidx, base)) return;   // effective base (bridge if active, else authored)
        const ParamID id = nb.handle->params[pidx].id;
        int slot = -1;
        for (uint32_t j = 0; j < n; ++j) if (out[j].id == id) { slot = static_cast<int>(j); break; }
        const float cur = slot >= 0 ? out[slot].value : base;
        const float v = vivid::audio::control_resolve(cur, src, sh, 0.f, 1.f);   // VST3: normalized range
        if (slot >= 0) out[slot].value = v;
        else if (n < vivid::audio::kMaxControlInputs) out[n++] = { id, v };
    };
    for (int k = 0; k < s.n_control_in; ++k) {
        const vivid::audio::ControlIn& ci = s.control_in[k];
        if (ci.src_buf < 0 || ci.param < 0) continue;
        stack(ci.param, b.ctl_pool[b.ctl_base + static_cast<size_t>(ci.src_buf) * kGraphMaxBlock], ci.shape);
    }
    for (uint32_t x = 0; x < b.xctl_count; ++x) {
        const XCtlApply& xa = b.xctl[x];
        if (xa.dst_track_id != dst_track_id || xa.dst_out_buf != s.out_buf || xa.dst_param < 0) continue;
        stack(xa.dst_param, b.ctl_pool[xa.src_pool_index], xa.shape);
    }
    return n;
}

// ADR-0022 P2a.1b: run one MODULATOR step — resolve its own control inputs (a modulator can be
// modulated), run the op into its control-pool region, publish the UI live dot. `scL`/`scR` receive
// its (discarded) silent audio output. Pure DSP + atomics; no alloc/lock.
static void run_modulator_step(const vivid::audio::CompiledStep& s, const GNodeBind& nb, Track& t,
                               const GraphBlockCtx& b, float* scL, float* scR, uint32_t frames) {
    vivid::AudioOpParamOverride ovr[vivid::audio::kMaxControlInputs];
    const uint32_t novr = resolve_control_inputs(s, nb, b, ovr);
    std::memset(scL, 0, frames * sizeof(float)); std::memset(scR, 0, frames * sizeof(float));
    float* cout = nullptr;
    if (s.control_out_buf >= 0 && s.control_out_buf < kGraphMaxNodes)
        cout = &b.ctl_pool[b.ctl_base + static_cast<size_t>(s.control_out_buf) * kGraphMaxBlock];
    // Feed the track's note union to the modulator so a note-gated envelope (ADSR) sees note on/off.
    // The prepass runs a block before t.nev is reassembled, so this is last block's notes — a few ms
    // of gate latency, imperceptible for an envelope. The LFO ignores notes, so this is a no-op for it.
    vivid::audio_op_process(nb.op, scL, scR, frames, b.sample_rate, b.bpm, b.bpb, b.beats,
                            t.nev.data(), static_cast<uint32_t>(t.nev.size()),
                            nullptr, 0, nullptr, ovr, novr, cout, frames);
    if (cout && s.out_buf >= 0 && s.out_buf < kGraphMaxNodes)
        t.ctl_pub[s.out_buf].store(cout[0], std::memory_order_relaxed);
}

// ADR-0022 P2a.1b: the modulator PRE-PASS for one track — run every `NativeMod` step (in the plan's
// topological order, so a modulator that drives another runs first) into the session control pool
// BEFORE any track renders audio. This is what makes control-edge resolution independent of track
// render order — the basis for cross-track modulation (P2a.2). Uses the currently published plan.
static void run_track_modulators(Track& t, const GraphBlockCtx& b, float* scL, float* scR, uint32_t frames) {
    const vivid::audio::CompiledAudioGraph& cg = t.gcg;
    if (frames > kGraphMaxBlock || cg.steps.empty() || static_cast<int>(t.gbinds.size()) < cg.buf_count) return;
    for (const vivid::audio::CompiledStep& s : cg.steps) {
        if (s.out_buf < 0 || s.out_buf >= static_cast<int>(t.gbinds.size())) continue;
        const GNodeBind& nb = t.gbinds[s.out_buf];
        if (nb.kind != GNKind::NativeMod || !nb.op) continue;
        // ADR-0033 Phase 3: a bypassed modulator is disabled — zero its control-out buffer so any
        // param it drove falls back to its base (the pool is not cleared per block, so we must clear
        // it explicitly rather than just skip, else the param would freeze at the last value).
        if (s.bypassed) {
            if (s.control_out_buf >= 0 && s.control_out_buf < kGraphMaxNodes)
                std::memset(&b.ctl_pool[b.ctl_base + static_cast<size_t>(s.control_out_buf) * kGraphMaxBlock],
                            0, frames * sizeof(float));
            if (s.out_buf >= 0 && s.out_buf < kGraphMaxNodes)
                t.ctl_pub[s.out_buf].store(0.f, std::memory_order_relaxed);   // live dot goes dark
            continue;
        }
        run_modulator_step(s, nb, t, b, scL, scR, frames);
    }
}

// ADR-0022 P2b.2: process ONE compiled step — the per-node body of the render loop, factored out so a
// session executor can drive steps from any track through a single path (each call carries the step's
// owning Track, that track's node-pool region `pool`, and the block context `b`/`gctx`). Bit-identical
// to the inline body it replaced; the loop's `continue`s became `return`s. `oL/oR` = the node's output
// buffer in `pool`.
static void process_step(const vivid::audio::CompiledStep& s, Track& t, float* pool, uint32_t stride,
                         int scratch, const GraphBlockCtx& b, const VividAudioContext& gctx, uint32_t frames) {
    float* oL = pool + static_cast<size_t>(s.out_buf) * 2 * stride;
    float* oR = oL + stride;
    const GNodeBind& nb = t.gbinds[s.out_buf];

    // ADR-0022 P2a.1b: modulators already ran in the pre-pass (into the session control pool) — skip
    // them here, just keep their audio buffer silent (nothing consumes a modulator's audio).
    if (nb.kind == GNKind::NativeMod) {
        std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
        return;
    }
    // Resolve this node's control inputs into param overrides (shared helper — reads the session
    // control pool region; the base stays live, so guardrail 3 holds).
    vivid::AudioOpParamOverride ovr[vivid::audio::kMaxControlInputs];
    uint32_t novr = resolve_control_inputs(s, nb, b, ovr);
    // ADR-0022 P2a.2: apply cross-track control edges targeting this node — read the SOURCE track's
    // region of the session pool and combine with the SAME live base+shape (guardrail 3), stacking
    // onto any in-track override on the same param. Only the source region differs from in-track.
    if (b.xctl_count > 0 && nb.op) {
        for (uint32_t x = 0; x < b.xctl_count; ++x) {
            const XCtlApply& xa = b.xctl[x];
            if (xa.dst_track_id != t.id || xa.dst_out_buf != s.out_buf || xa.dst_param < 0) continue;
            const float srcv = b.ctl_pool[xa.src_pool_index];
            const float lo = vivid::audio_op_param_min(nb.op, xa.dst_param);
            const float hi = vivid::audio_op_param_max(nb.op, xa.dst_param);
            int slot = -1;
            for (uint32_t j = 0; j < novr; ++j) if (ovr[j].param == xa.dst_param) { slot = static_cast<int>(j); break; }
            const float base = slot >= 0 ? ovr[slot].value : vivid::audio_op_param_effective(nb.op, xa.dst_param);   // ADR-0030 P2: effective base
            const float v = vivid::audio::control_resolve(base, srcv, xa.shape, lo, hi);
            if (slot >= 0) ovr[slot].value = v;
            else if (novr < vivid::audio::kMaxControlInputs) ovr[novr++] = { xa.dst_param, v };
        }
    }
    // ADR-0034: the CLAP twin of the native override resolve above — a plugin node's modulation,
    // delivered as param events by render_clap_*. Empty (cmod_n == 0) unless a control edge drives a
    // CLAP node (and it has a captured base anchor). VST3 modulation lands in Phase 2.
    ClapParamMsg cmod[vivid::audio::kMaxControlInputs];
    const uint32_t cmod_n = nb.clap ? resolve_clap_control(s, nb, b, t.id, cmod) : 0;
    // ADR-0034 Phase 2: the VST3 twin — resolved param points injected into the plugin's
    // Vst3ParamChanges by render_vst3_*. Empty unless a control edge drives a VST3 node.
    ParamMsg vmod[vivid::audio::kMaxControlInputs];
    const uint32_t vmod_n = nb.handle ? resolve_vst3_control(s, nb, b, t.id, vmod) : 0;

    if (s.n_in == 0) {   // source node
        // ADR-0033 Phase 3: a bypassed source/generator gates to silence — no audio, and no notes
        // on its note-out (so a bypassed MidiClip/generator/note-fx stops driving downstream too).
        if (s.bypassed) {
            std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
            if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size()))
                t.npool[static_cast<size_t>(s.note_out_buf)].clear();
            return;
        }
        const bool full_range = (nb.key_lo == 0 && nb.key_hi == 127);
        // ADR-0015: the notes THIS node consumes — its note edge if it has one, else the
        // track-wide stream (the pre-note-edge behavior, which is what keeps parity).
        const std::vector<NoteEvent>& nsrc = graph_note_input(t, s, b);
        // MidiIn / MidiClip: note production AS NODES. Each publishes its stream on its note-out
        // buffer for whatever it feeds, and makes no sound of its own. ADR-0022 P3.1b split what
        // P3.1a's single MidiIn carried: MidiIn now emits t.nev_live (live MIDI + editor preview),
        // MidiClip emits t.nev_clip (clip scheduler + release flush). ADR-0022 P3.2b: there is one
        // MidiClip node per scene, but only the node whose scene == the active scene emits the clip
        // stream (t.nev_clip is the active clip, from the shared scheduler); the rest gate to silence.
        if (nb.kind == GNKind::MidiIn || nb.kind == GNKind::MidiClip) {
            std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
            if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size())) {
                const bool emit = (nb.kind == GNKind::MidiIn) ||
                                  (nb.scene == t.active.load(std::memory_order_relaxed));   // active scene's clip only
                std::vector<NoteEvent>& outn = t.npool[static_cast<size_t>(s.note_out_buf)];
                outn.clear();
                if (emit) {
                    const std::vector<NoteEvent>& srcn = (nb.kind == GNKind::MidiClip) ? t.nev_clip : t.nev_live;
                    for (const NoteEvent& e : srcn) {
                        if (outn.size() >= kGraphMaxNotes) break;   // truncate, never allocate
                        outn.push_back(e);
                    }
                }
            }
            return;
        }
        // ADR-0022 P3.2: the per-track-out note SELECTOR. It merges its note inputs (the scene
        // clip nodes) into its own note-out for the instrument — selection is by SOURCE gating
        // (only the active scene's clip node emits, so the merge is exactly the active clip). It
        // makes no sound. `nsrc` is already the merged+sorted note input (graph_note_input), so
        // the selector just republishes it on its note buffer, collapsing fan-in to 1 downstream.
        if (nb.kind == GNKind::Selector) {
            std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
            if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size())) {
                std::vector<NoteEvent>& outn = t.npool[static_cast<size_t>(s.note_out_buf)];
                outn.clear();
                for (const NoteEvent& e : nsrc) {
                    if (outn.size() >= kGraphMaxNotes) break;   // truncate, never allocate
                    outn.push_back(e);
                }
            }
            return;
        }
        // ADR-0022 P3.3: a NOTE GENERATOR in a scene cell (Euclid / Chord / RandMelody). A scene-gated
        // note SOURCE — it reads NO input notes and emits its own from the transport into its note-out.
        // Only the node whose scene == the active scene runs; the rest gate to silence (like MidiClip),
        // so the Selector's merge is exactly the active cell. Its scene-switch release goes through
        // t.scene_rel (see the switch block + audio_op_note_flush), like a clip's.
        if (nb.kind == GNKind::NativeGen && nb.op) {
            std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
            std::vector<NoteEvent>* outn = nullptr;
            if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size()))
                outn = &t.npool[static_cast<size_t>(s.note_out_buf)];
            if (outn) {
                outn->clear();
                if (nb.scene == t.active.load(std::memory_order_relaxed)) {
                    uint32_t produced = 0;
                    outn->resize(kGraphMaxNotes);   // reserved capacity: no allocation
                    vivid::audio_op_process(nb.op, oL, oR, frames, b.sample_rate, b.bpm, b.bpb, b.beats,
                                            nullptr, 0,                     // a generator reads no input notes
                                            outn->data(), kGraphMaxNotes, &produced, ovr, novr);
                    outn->resize(produced);
                }
            }
            return;
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
            return;
        }
        if (nb.kind == GNKind::NativeInst && nb.op) {
            const NoteEvent* nn = nsrc.data(); uint32_t nc = static_cast<uint32_t>(nsrc.size());
            if (!full_range) {   // key-split: hand this source only its in-range notes
                filter_notes_by_range(nsrc, nb.key_lo, nb.key_hi, t.src_nev);
                nn = t.src_nev.data(); nc = static_cast<uint32_t>(t.src_nev.size());
            }
            // Prepend this block's scene-switch note-offs, exactly as the VST3 path does via t.vev and
            // the CLAP path does in render_clap_instrument. Without this a NATIVE instrument never
            // received scene-switch releases (they only reached t.vev / t.scene_rel), so switching away
            // from a native clip with held notes left them stuck. The releases carry the OUTGOING clip's
            // note_ids, so the op releases exactly those voices regardless of order. Only builds the
            // scratch on a switch block (t.scene_rel non-empty); the steady-state path is unchanged.
            if (!t.scene_rel.empty()) {
                t.ni_nev.clear();
                for (const NoteEvent& e : t.scene_rel) {
                    if (t.ni_nev.size() >= kGraphMaxNotes) break;   // truncate, never allocate
                    t.ni_nev.push_back(e);
                }
                for (uint32_t i = 0; i < nc; ++i) {
                    if (t.ni_nev.size() >= kGraphMaxNotes) break;
                    t.ni_nev.push_back(nn[i]);
                }
                nn = t.ni_nev.data(); nc = static_cast<uint32_t>(t.ni_nev.size());
            }
            vivid::audio_op_process(nb.op, oL, oR, frames, b.sample_rate, b.bpm, b.bpb, b.beats, nn, nc,
                                    nullptr, 0, nullptr, ovr, novr);
        }
        else if (nb.kind == GNKind::Vst3Inst && nb.handle) {
            std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));  // silent input, matches inline
            // ADR-0045 Tier 2a: a watchdog-disabled instrument stays silent and emits no notes (its
            // output is already zeroed) — the auto-bypass of a bad plugin, like a bypassed source.
            if (nb.handle->watchdog.faulted.load(std::memory_order_relaxed)) {
                if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size()))
                    t.npool[static_cast<size_t>(s.note_out_buf)].clear();
                return;
            }
            if (full_range) {   // t.vev is primed with scene-switch releases (identical to today)
                emit_vst3(t.vev, nsrc, t.eev);
                render_vst3_instrument(t, nb.handle, t.vev, gctx, frames, oL, oR, vmod, vmod_n);
            } else {            // key-split: independent filtered event list for this source
                filter_notes_by_range(nsrc, nb.key_lo, nb.key_hi, t.src_nev);
                filter_expr_by_range(t.eev, nb.key_lo, nb.key_hi, t.src_eev);
                t.src_vev.clear();
                emit_vst3(t.src_vev, t.src_nev, t.src_eev);
                render_vst3_instrument(t, nb.handle, t.src_vev, gctx, frames, oL, oR, vmod, vmod_n);
            }
            // ADR-0015 (M3): a plugin that GENERATES notes (a chord generator, an arpeggiator)
            // publishes them on its note output, so they can drive another instrument.
            if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size()))
                drain_vst3_notes(nb.handle, t.npool[static_cast<size_t>(s.note_out_buf)]);
        }
        else if (nb.kind == GNKind::ClapInst && nb.clap) {
            std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));
            if (nb.clap->watchdog.faulted.load(std::memory_order_relaxed)) {   // ADR-0045 Tier 2a: disabled → silent, no notes
                if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size()))
                    t.npool[static_cast<size_t>(s.note_out_buf)].clear();
                return;
            }
            if (full_range) render_clap_instrument(t, nb.clap, nsrc, t.eev, frames, oL, oR, cmod, cmod_n);
            else { filter_notes_by_range(nsrc, nb.key_lo, nb.key_hi, t.src_nev);   // key-split
                   filter_expr_by_range(t.eev, nb.key_lo, nb.key_hi, t.src_eev);   // ...and its expression
                   render_clap_instrument(t, nb.clap, t.src_nev, t.src_eev, frames, oL, oR, cmod, cmod_n); }
            // ADR-0015 (M2): a CLAP that GENERATES notes publishes them on its note output.
            if (s.note_out_buf >= 0 && s.note_out_buf < static_cast<int>(t.npool.size()))
                drain_clap_notes(nb.clap, t.npool[static_cast<size_t>(s.note_out_buf)]);
        }
        else if (nb.kind == GNKind::Sampler) {
            std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float));  // silent until the clip renders, matches inline
            render_sampler_block(t, b.beats, b.delta, frames, b.sample_rate, b.playing, oL, oR);
        }
        else { std::memset(oL, 0, frames * sizeof(float)); std::memset(oR, 0, frames * sizeof(float)); }
        return;
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
    // ADR-0022 P2b.4: cross-track AUDIO edges targeting this node — add the SOURCE track's rendered
    // node output (another region of the one session node pool) into the summed input. The source
    // track rendered earlier this block (render-order topo-sort), so its region is current. Reads the
    // whole buffer at the live `frames` stride (the region base was precomputed at kGraphMaxBlock).
    for (uint32_t x = 0; x < b.xaudio_count; ++x) {
        const XAudioApply& xa = b.xaudio[x];
        if (xa.dst_track_id != t.id || xa.dst_out_buf != s.out_buf) continue;
        const float* sL = b.node_pool + xa.src_pool_base + static_cast<size_t>(xa.src_out_buf) * 2 * stride;
        const float* sR = sL + stride;
        for (uint32_t i = 0; i < frames; ++i) { iL[i] += sL[i]; iR[i] += sR[i]; }
    }
    std::memcpy(oL, iL, frames * sizeof(float));   // start from the summed input
    std::memcpy(oR, iR, frames * sizeof(float));
    // ADR-0033 Phase 3: a bypassed effect passes its input straight through — oL/oR already hold the
    // summed input, so we simply skip the transform (the plugin/op is not run).
    if (nb.kind == GNKind::NativeFx && nb.op && !s.bypassed)   // effect: transform in place
        vivid::audio_op_process(nb.op, oL, oR, frames, b.sample_rate, b.bpm, b.bpb, b.beats, nullptr, 0,
                                nullptr, 0, nullptr, ovr, novr);
    // ADR-0045 Tier 2a: a watchdog-disabled effect passes its input through DRY (oL/oR already hold the
    // summed input) — the transform is skipped, exactly like a bypassed effect, so the track keeps playing.
    else if (nb.kind == GNKind::Vst3Fx && nb.handle && nb.handle->processing && !s.bypassed
             && !nb.handle->watchdog.faulted.load(std::memory_order_relaxed))  // non-processing = passthrough (matches inline skip)
        render_vst3_effect(t, nb.handle, gctx, frames, oL, oR, vmod, vmod_n);
    else if (nb.kind == GNKind::ClapFx && nb.clap && nb.clap->processing && !s.bypassed
             && !nb.clap->watchdog.faulted.load(std::memory_order_relaxed))
        render_clap_effect(t, nb.clap, frames, oL, oR, cmod, cmod_n);
    else if (nb.kind == GNKind::Output) {
        // ADR-0022 P1a — the Track-Out node applies the track GAIN, so its buffer IS the track's
        // final output. (Was applied downstream in session_process's mix; relocated here so P1b's
        // master node can simply SUM the track-out buffers.) Bit-identical: x*g here == the old
        // L[i]*g in the mix. `oL/oR` already hold the summed inputs (passthrough above).
        const float g = t.gain.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < frames; ++i) { oL[i] *= g; oR[i] *= g; }
    }
}

// The VST3 ProcessContext values a node reads, rebuilt from the block context (native/sampler read
// the GraphBlockCtx directly). Same values for every step of a block, so the executor can rebuild it
// cheaply per Node step and keep each step self-describing (P2b.4 interleaves tracks).
static inline VividAudioContext block_gctx(const GraphBlockCtx& b) {
    VividAudioContext g{};
    g.sample_rate = b.sample_rate; g.metronome_bpm = b.bpm;
    g.metronome_beats_per_bar = b.bpb; g.metronome_beats_elapsed = b.beats;
    return g;
}

// ADR-0022 P2b.3b: the per-track FINALIZE step of the flat plan. The track's node steps have already
// run (writing into its node-pool region); this copies its output node into its track-out slot
// ADR-0025: one sample into a signal's running analysis — push the spectrum ring, advance the crossover
// one-poles, accumulate the 3-band + total energy. Inlined; shared by finalize_track (a track's output)
// and master_mix (the summed mix) so the per-sample meter math lives in ONE place. Bit-identical to the
// two loops it replaces (same operations, same order).
static inline void analyze_sample(MeterState& a, float l, float a_lo, float a_hi,
                                  double& sum_sq, double& slo, double& smi, double& shi) {
    sum_sq += static_cast<double>(l) * l;
    a.an_ring.push(l);   // spectrum ring (frame-side FFT) — atomic slots (ADR-0029)
    a.flt_lo += (l - a.flt_lo) * a_lo;
    a.flt_hi += (l - a.flt_hi) * a_hi;
    const float lo = a.flt_lo, mi = a.flt_hi - a.flt_lo, hi = l - a.flt_hi;
    slo += static_cast<double>(lo) * lo; smi += static_cast<double>(mi) * mi; shi += static_cast<double>(hi) * hi;
}
// ADR-0025: publish a block's accumulated analysis into the atomics — 3-band RMS, overall level, and the
// onset transient over a slow baseline. Called once per block; shared by finalize_track + master_mix.
static inline void publish_meters(MeterState& a, double sum_sq, double slo, double smi, double shi, uint32_t frames) {
    const float inv = 1.f / (frames > 0 ? frames : 1);
    a.band_low.store(static_cast<float>(std::sqrt(slo * inv)), std::memory_order_relaxed);
    a.band_mid.store(static_cast<float>(std::sqrt(smi * inv)), std::memory_order_relaxed);
    a.band_high.store(static_cast<float>(std::sqrt(shi * inv)), std::memory_order_relaxed);
    const float rms = static_cast<float>(std::sqrt(sum_sq / (frames > 0 ? frames : 1)));
    a.level.store(rms, std::memory_order_relaxed);
    const float tr = std::max(0.f, (rms - a.tr_baseline) * 6.f);  // onset over baseline
    a.tr_baseline += (rms - a.tr_baseline) * 0.04f;
    a.transient.store(std::min(1.f, tr), std::memory_order_relaxed);
}

// (`slotL`/`slotL+kGraphMaxBlock`), taps the per-node waveform scope, and computes the track meters.
// `valid` is the RT bail-net verdict decided once at plan-build time: a valid track's output is
// copied out (and its scope tapped); an invalid or gok=false track's slot is silenced. Bit-identical
// to the old tail of run_track_graph + the render loop's metering block, in the same order.
static void finalize_track(Track& t, float* slotL, bool valid, uint32_t frames, uint32_t sample_rate) {
    float* L = slotL;
    float* R = slotL + kGraphMaxBlock;
    if (valid) {
        const vivid::audio::CompiledAudioGraph& cg = t.gcg;
        const uint32_t stride = frames;
        float* pool = t.blk.node_pool + t.blk.node_base;   // this track's region of the session node pool
        // Tap each node's output (L) into its waveform-scope ring for the UI preview. Display-only:
        // fixed buffers, no alloc/lock; a few decimated samples per block advance the rolling scope.
        if (t.node_scope.allocated()) {
            for (const vivid::audio::CompiledStep& s : cg.steps) {
                if (s.out_buf < 0 || s.out_buf >= kGraphMaxNodes) continue;
                const float* nl = pool + static_cast<size_t>(s.out_buf) * 2 * stride;
                float dec[kScopePerBlock];
                for (int c = 0; c < kScopePerBlock; ++c) {
                    uint32_t si = static_cast<uint32_t>((2 * c + 1)) * frames / (2u * kScopePerBlock);
                    dec[c] = nl[si < frames ? si : frames - 1];
                }
                t.node_scope.push_block(s.out_buf, dec, kScopePerBlock);
            }
        }
        // Gated per-node FFT capture: for each WATCHED node (mask bit set, set by the UI when its fft
        // source is consumed), copy this block's CONTIGUOUS output samples into its analysis ring. The
        // frame side FFTs it. Ring is UI-allocated; the acquire-load of the mask pairs with that.
        if (const uint64_t am = t.node_analyze_mask.load(std::memory_order_acquire); am && t.node_an.allocated()) {
            for (const vivid::audio::CompiledStep& s : cg.steps) {
                if (s.out_buf < 0 || s.out_buf >= kGraphMaxNodes || !(am & (uint64_t(1) << s.out_buf))) continue;
                const float* nl = pool + static_cast<size_t>(s.out_buf) * 2 * stride;
                t.node_an.push_block(s.out_buf, nl, static_cast<int>(frames));
            }
        }
        const float* outL = pool + static_cast<size_t>(cg.output_buf) * 2 * stride;
        std::memcpy(L, outL, frames * sizeof(float));
        std::memcpy(R, outL + stride, frames * sizeof(float));
    } else {
        std::memset(L, 0, frames * sizeof(float));
        std::memset(R, 0, frames * sizeof(float));
    }
    t.steady += frames;

    // Per-track meters over the gained track output (or silence). The track GAIN was applied inside
    // the Track-Out node (P1a), so L/R already hold the final output; metering stays PRE-mute (a muted
    // track still shows its own level). Runs for every render_list track — including a bailed / no-
    // source one — so the meters cover those cases too.
    const float sr = static_cast<float>(sample_rate > 0 ? sample_rate : 48000);
    const float a_lo = 1.f - std::exp(-6.2832f * 200.f / sr);    // crossover @ ~200 Hz
    const float a_hi = 1.f - std::exp(-6.2832f * 2000.f / sr);   // crossover @ ~2 kHz
    double sum_sq = 0.0, slo = 0.0, smi = 0.0, shi = 0.0;
    bool cap_locked = false;
    size_t cap = 0, cap_pos = 0, cap_filled = 0;
    if (!t.capture_l.empty() && t.capture_mtx.try_lock()) {
        cap_locked = !t.capture_l.empty() && !t.capture_r.empty();
        if (cap_locked) {
            t.capture_sample_rate = sample_rate;
            cap = t.capture_l.size();
            cap_pos = t.capture_write_pos;
            cap_filled = t.capture_filled;
        } else {
            t.capture_mtx.unlock();
        }
    }
    for (uint32_t i = 0; i < frames; ++i) {
        const float l = L[i], r = R[i];
        if (cap_locked) {
            t.capture_l[cap_pos] = l;
            t.capture_r[cap_pos] = r;
            cap_pos = (cap_pos + 1) % cap;
            cap_filled = std::min(cap, cap_filled + 1);
        }
        analyze_sample(t.meter, l, a_lo, a_hi, sum_sq, slo, smi, shi);
    }
    if (cap_locked) {
        t.capture_write_pos = cap_pos;
        t.capture_filled = cap_filled;
        t.capture_mtx.unlock();
    }
    publish_meters(t.meter, sum_sq, slo, smi, shi, frames);
}

// ADR-0022 P2b.3b: the MASTER step of the flat plan (one, after every track's finalize). Sums each
// rendered track's slot (with its solo/mute multiplier) into `out`, applies the master gain, and
// computes the master meters — bit-identical to the master block it replaces.
static void master_mix(Session* s, float* out, uint32_t frames, uint32_t sample_rate) {
    Master& m = s->master;
    // ADR-0032 E1: plugin-delay compensation. When enabled, a track with a published pdc_delay is summed
    // `delay` samples late via its per-track ring, aligning it to the highest-latency track. Off, or
    // delay<=0 (the max-latency track / an uncompensated track), takes the byte-identical fast path.
    const bool pdc = s->pdc_enabled.load(std::memory_order_relaxed);
    for (size_t slot = 0; slot < s->render_list.size(); ++slot) {
        Track* t = s->render_list[slot];
        // ADR-0022 P1b.4: apply the track's solo/mute multiplier here (0 silences it in the mix; its
        // own meter, computed in finalize, stays pre-mute). At the default 1.0 this is an IEEE
        // identity, so an all-audible session sums bit-identically to before.
        const float scale = t->mix_scale.load(std::memory_order_relaxed);
        const float* L = s->track_out_pool.data() + slot * 2 * kGraphMaxBlock;
        const float* R = L + kGraphMaxBlock;
        const int D = pdc ? t->pdc_delay.load(std::memory_order_relaxed) : 0;
        if (D <= 0 || t->pdc_ring.empty()) {
            for (uint32_t i = 0; i < frames; ++i) { out[2 * i] += scale * L[i]; out[2 * i + 1] += scale * R[i]; }
        } else {
            t->pdc_w = vivid::audio::pdc_delay_accumulate(
                t->pdc_ring.data(), t->pdc_ring.data() + vivid::audio::kPdcRingCap,
                t->pdc_w, static_cast<uint32_t>(D), L, R, out, scale, frames);
        }
    }
    const float mg = m.gain.load(std::memory_order_relaxed);
    const float sr = static_cast<float>(sample_rate > 0 ? sample_rate : 48000);
    const float a_lo = 1.f - std::exp(-6.2832f * 200.f / sr);
    const float a_hi = 1.f - std::exp(-6.2832f * 2000.f / sr);
    double sum_sq = 0.0, slo = 0.0, smi = 0.0, shi = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        const float l = out[2 * i] * mg, r = out[2 * i + 1] * mg;
        out[2 * i] = l; out[2 * i + 1] = r;
        analyze_sample(m.meter, l, a_lo, a_hi, sum_sq, slo, smi, shi);
    }
    publish_meters(m.meter, sum_sq, slo, smi, shi, frames);
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

// A dynamically-added instrument track: empty clips (the user authors them) across all
// scenes, so set_clip/launch work immediately.
static Track* make_instrument_track(Vst3Handle* h, const std::string& name, int scenes, uint32_t sample_rate) {
    auto* t = new Track();
    t->handle = h;
    t->name = name;
    t->clips.reserve(kMaxScenes);   // reserve to the scene cap so session_add_scene appends without realloc
    for (int i = 0; i < scenes; ++i) { MidiClip c; c.length = 4.0; t->clips.push_back(c); }
    t->sched.reset(&t->clips[0]);
    t->nev.reserve(128); t->nev_clip.reserve(64); t->nev_live.reserve(64); t->scene_rel.reserve(64);
t->eev.reserve(256);
    t->edit_clips = t->clips;
    t->edit_clips.reserve(kMaxScenes);   // operator= may shrink capacity to size — re-reserve
    t->gen_cells.reserve(kMaxScenes);    // ADR-0022 P3.3: one cell per scene (all clips by default)
    t->gen_cells.resize(scenes);
    t->effects.reserve(16); t->effects_edit.reserve(16);
    t->op_effects.reserve(16); t->op_effects_edit.reserve(16);
    reserve_track_graph(t);
    configure_track_capture(t, sample_rate);
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

Session* session_create(uint32_t sample_rate) {

    auto* s = new Session();
    s->sample_rate = sample_rate;
    s->master.gnid = s->next_gnid++;   // ADR-0022 P2b.3c: the master claims global node id 0
    s->tracks.reserve(kMaxTracks);
    s->tracks_pub.reserve(kMaxTracks);
    s->tracks_view.reserve(kMaxTracks);
    s->render_list.reserve(kMaxTracks);   // ADR-0022 P1b: master-node input list (audio thread)
    // ADR-0022 P2b.3b: the flat session plan — worst case one node step per node on every track
    // (kGraphMaxNodes), one finalize per track, and one master step.
    s->session_plan.reserve(static_cast<size_t>(kMaxTracks) * (kGraphMaxNodes + 1) + 1);
    // ADR-0022 P1b.3a: the session track-output pool — one stereo kGraphMaxBlock slot per track.
    s->track_out_pool.assign(static_cast<size_t>(kMaxTracks) * 2 * kGraphMaxBlock, 0.f);
    // ADR-0022 P2a.1: the session control pool — kMaxTracks regions of kGraphMaxNodes control buffers.
    s->ctl_pool.assign(static_cast<size_t>(kMaxTracks) * kGraphMaxNodes * kGraphMaxBlock, 0.f);
    // ADR-0022 P2b.1: the session node-buffer pool — kMaxTracks regions of (kGraphMaxNodes+1) stereo buffers.
    s->node_pool.assign(static_cast<size_t>(kMaxTracks) * (kGraphMaxNodes + 1) * 2 * kGraphMaxBlock, 0.f);
    s->mod_scratch.assign(static_cast<size_t>(2) * kGraphMaxBlock, 0.f);   // P2a.1b modulator pre-pass scratch
    s->xctl_edges.reserve(256); s->xctl_view.reserve(256); s->xctl_ho.reserve(256);   // P2a.2 cross-track edges
    s->xaudio_edges.reserve(256); s->xaudio_view.reserve(256); s->xaudio_ho.reserve(256);   // P2b.4 cross-track audio
    s->xnote_edges.reserve(256); s->xnote_view.reserve(256); s->xnote_ho.reserve(256);   // P2b.5 cross-track notes
    rebuild_track_view(s);   // publish the initial set to the audio thread
    // AG-0: build each track's derived audio graph up front so gok tracks (native + VST3) run through
    // the graph from the first block — not only after a device edit. AudioClip tracks stay inline until
    // Stage 4. Parity-by-construction (same source/FX helpers, same order).
    for (auto& t : s->tracks) rebuild_track_graph(t.get());
    std::fprintf(stderr, "[Session] %zu tracks, %d scenes\n", s->tracks.size(), s->scenes);
    return s;
}

int  session_track_count(Session* s) { return s ? static_cast<int>(s->tracks.size()) : 0; }

// ADR-0032 Phase B: sum the plugin-reported latency over ALL of a track's plugin handles. Two storage
// models coexist — the ADR-0033 graph slots (pslots) AND the legacy linear chain (handle/effects +
// clap_inst/clap_effects, populated by set_track_clap_instrument etc.). Iterate both and dedup by handle
// pointer (a plugin lives in one model, but dedup is cheap insurance against a mirror). VST3 always
// reports (getLatencySamples); a CLAP without clap.latency is "unknown" (counts 0, flags unknown).
static int track_plugin_latency_sum(const Track& t, bool* any_unknown) {
    int sum = 0;
    std::vector<const void*> seen;
    auto first_time = [&](const void* p) {
        if (!p || std::find(seen.begin(), seen.end(), p) != seen.end()) return false;
        seen.push_back(p); return true;
    };
    auto add_vst3 = [&](const Vst3Handle* h) { if (first_time(h) && h->latency_samples > 0) sum += h->latency_samples; };
    auto add_clap = [&](const ClapHandle* h) {
        if (!first_time(h)) return;
        sum += static_cast<int>(h->latency_samples);
        if (!h->latency_known && any_unknown) *any_unknown = true;
    };
    for (const auto& ps : t.pslots) { if (ps.dead) continue; add_vst3(ps.vst3); add_clap(ps.clap); }
    add_vst3(t.handle);
    for (const auto* fx : t.effects) add_vst3(fx);
    add_clap(t.clap_inst);
    for (const auto* fx : t.clap_effects) add_clap(fx);
    return sum;
}
int session_track_latency_samples(Session* s, int track) {
    if (!s || track < 0 || track >= static_cast<int>(s->tracks.size())) return 0;
    return track_plugin_latency_sum(*s->tracks[static_cast<size_t>(track)], nullptr);
}
int session_max_plugin_latency_samples(Session* s) {
    if (!s) return 0;
    int mx = 0;
    for (const auto& tp : s->tracks) { const int l = track_plugin_latency_sum(*tp, nullptr); if (l > mx) mx = l; }
    return mx;
}
int session_any_plugin_latency_unknown(Session* s) {
    if (!s) return 0;
    bool unk = false;
    for (const auto& tp : s->tracks) track_plugin_latency_sum(*tp, &unk);
    return unk ? 1 : 0;
}

// ADR-0032 E1.1: a track is LIVE-INPUT-SOURCED if any of its native source ops is the AudioInput op —
// such a track carries live hardware monitoring and must never be delayed (that would add latency to what
// the performer hears). Detected by the stable op TYPE ("AudioInput"), not the display name. Main thread.
static bool track_is_live_input_sourced(const Track& t) {
    auto is_ai = [](const vivid::AudioOp* op) {
        const char* ty = op ? vivid::audio_op_type(op) : nullptr;
        return ty && std::strcmp(ty, "AudioInput") == 0;
    };
    if (is_ai(t.op_instrument_edit)) return true;
    for (const auto* op : t.op_sources_edit) if (is_ai(op)) return true;
    return false;
}

// ADR-0032 E1.1: a track is CROSS-TRACK-AUDIO involved if its stable id is an endpoint of any xaudio
// edge — its output is routed/consumed off the track_out seam, so delaying track_out would desync it.
// Left live in E1. Main thread (reads the UI-authoritative xaudio_edges).
static bool track_in_xaudio(const Session* s, const Track& t) {
    for (const XAudioEdge& e : s->xaudio_edges)
        if (e.src_track_id == t.id || e.dst_track_id == t.id) return true;
    return false;
}

// ADR-0032 E1.1: (re)classify every track and publish its compensating delay. Main/UI thread only.
// COMPENSABLE = every plugin reports latency (not unknown) AND the path is linear (no cross-track audio)
// AND it is not a live-input monitor AND within the cap. L_max is taken over the compensable set ONLY, so
// an unknown/live track never over-delays the rest (the movie-A/V invariant). Off, or no compensable
// track, publishes all-zero delays (master_mix then takes the byte-identical fast path).
static void pdc_recompute(Session* s) {
    if (!s) return;
    const size_t n = s->tracks.size();
    const bool on = s->pdc_enabled.load(std::memory_order_relaxed);
    const int  cap = static_cast<int>(vivid::audio::kPdcMaxComp);

    std::vector<int>           lat(n, 0);
    std::vector<unsigned char> comp(n, 0);   // 0/1 per track; unsigned char so .data() feeds the pure helper
    std::vector<int>           delays(n, 0);
    bool clamped = false;
    if (on) {
        for (size_t i = 0; i < n; ++i) {
            Track& t = *s->tracks[i];
            bool unk = false;
            int l = track_plugin_latency_sum(t, &unk);
            const bool compensable = !unk && l >= 0
                                   && !track_is_live_input_sourced(t)
                                   && !track_in_xaudio(s, t);
            if (compensable && l > cap) { l = cap; clamped = true; }
            lat[i]  = l;
            comp[i] = compensable ? 1 : 0;
        }
        const int lmax = vivid::audio::pdc_compute_delays(lat.data(), comp.data(),
                                                          static_cast<int>(n), delays.data());
        s->pdc_applied_delay.store(lmax, std::memory_order_relaxed);
    } else {
        s->pdc_applied_delay.store(0, std::memory_order_relaxed);
    }

    int n_comp = 0, n_live = 0;
    for (size_t i = 0; i < n; ++i) {
        Track& t = *s->tracks[i];
        const int D = delays[i];
        if (on) { if (comp[i]) n_comp++; else n_live++; }
        // Allocate the ring on first non-zero use, on THIS (main) thread, while pdc_delay is still 0 so the
        // audio thread is provably not indexing it. Allocate-once: never resized afterwards.
        if (D > 0 && t.pdc_ring.empty())
            t.pdc_ring.assign(2 * static_cast<size_t>(vivid::audio::kPdcRingCap), 0.f);
        t.pdc_delay.store(D, std::memory_order_relaxed);
    }
    s->pdc_tracks_comp.store(n_comp, std::memory_order_relaxed);
    s->pdc_tracks_live.store(n_live, std::memory_order_relaxed);
    s->pdc_clamped.store(clamped, std::memory_order_relaxed);
}

// ADR-0032 E1: playback plugin-delay compensation. Main/UI thread.
bool session_pdc_enabled(Session* s) { return s && s->pdc_enabled.load(std::memory_order_relaxed); }
void session_set_pdc_enabled(Session* s, bool enabled) {
    if (!s) return;
    s->pdc_enabled.store(enabled, std::memory_order_relaxed);
    pdc_recompute(s);   // (re)classify + publish per-track delays (or clear them all when disabling)
}
void session_pdc_set_track_delay(Session* s, int track, int delay_samples) {
    if (!s || track < 0 || track >= static_cast<int>(s->tracks.size())) return;
    Track& t = *s->tracks[static_cast<size_t>(track)];
    int d = delay_samples < 0 ? 0 : delay_samples;
    if (d > static_cast<int>(vivid::audio::kPdcMaxComp)) d = static_cast<int>(vivid::audio::kPdcMaxComp);
    // Allocate the ring on first non-zero use, on THIS (main) thread, while pdc_delay is still 0 so the
    // audio thread is provably not indexing it. Allocate-once: never resized after.
    if (d > 0 && t.pdc_ring.empty()) t.pdc_ring.assign(2 * static_cast<size_t>(vivid::audio::kPdcRingCap), 0.f);
    t.pdc_delay.store(d, std::memory_order_relaxed);
}
int session_pdc_applied_delay(Session* s)      { return s ? s->pdc_applied_delay.load(std::memory_order_relaxed) : 0; }
int session_pdc_tracks_compensated(Session* s) { return s ? s->pdc_tracks_comp.load(std::memory_order_relaxed) : 0; }
int session_pdc_tracks_live(Session* s)        { return s ? s->pdc_tracks_live.load(std::memory_order_relaxed) : 0; }
int session_pdc_clamped(Session* s)            { return s && s->pdc_clamped.load(std::memory_order_relaxed) ? 1 : 0; }
int session_pdc_track_delay(Session* s, int track) {
    if (!s || track < 0 || track >= static_cast<int>(s->tracks.size())) return 0;
    return s->tracks[static_cast<size_t>(track)]->pdc_delay.load(std::memory_order_relaxed);
}
int session_sample_rate(Session* s)            { return s ? static_cast<int>(s->sample_rate) : 0; }
int  session_scene_count(Session* s) { return s ? s->scenes : 0; }

// ADR-0022 P3.3: the default display name for scene i — A..Z, then "Scene N". UI-thread only.
static std::string default_scene_name(int i) {
    if (i >= 0 && i < 26) return std::string(1, static_cast<char>('A' + i));
    return "Scene " + std::to_string(i + 1);
}
// Grow/shrink scene_names to match s->scenes, filling any new slots with defaults. UI-thread only.
static void ensure_scene_names(Session* s) {
    if (!s) return;
    const int n = s->scenes;
    if (static_cast<int>(s->scene_names.size()) > n) s->scene_names.resize(n);
    while (static_cast<int>(s->scene_names.size()) < n)
        s->scene_names.push_back(default_scene_name(static_cast<int>(s->scene_names.size())));
}
const char* session_scene_name(Session* s, int scene) {
    if (!s || scene < 0 || scene >= s->scenes) return "";
    ensure_scene_names(s);
    return s->scene_names[static_cast<size_t>(scene)].c_str();
}
void session_set_scene_name(Session* s, int scene, const char* name) {
    if (!s || scene < 0 || scene >= s->scenes) return;
    ensure_scene_names(s);
    s->scene_names[static_cast<size_t>(scene)] = (name && *name) ? name : default_scene_name(scene);
}
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
    s->live_in.push(LiveMidi::kNoteOn, static_cast<uint16_t>(pitch), vel, beat);
    if (s->recording.load(std::memory_order_relaxed) && beat >= s->rec_capture_from) {
        std::lock_guard<std::mutex> lk(s->rec_mtx);
        s->rec_notes.push_back(RecNote{ pitch, beat, beat, vel, true });
    }
}
// P4 Phase D. Mirrors session_note_on's shape: resolve the armed track, push to the live queue, and
// (Phase E will) capture while recording. Deliberately does NOT gate on `pitch` bounds — a controller
// has no pitch — and does not touch the recording buffer yet.
void session_ctrl(Session* s, int cc, float value) {
    Track* t = armed_track_ptr(s);
    if (!t || t->is_audio || cc < 0 || cc >= kCcCount) return;
    const double beat = s->play_beats.load(std::memory_order_relaxed);
    const float v = std::clamp(value, 0.f, 1.f);
    s->live_in.push(LiveMidi::kCtrl, static_cast<uint16_t>(cc), v, beat);
    if (s->recording.load(std::memory_order_relaxed) && beat >= s->rec_capture_from) {
        std::lock_guard<std::mutex> lk(s->rec_mtx);
        s->rec_cc.push_back(RecCc{ static_cast<uint16_t>(cc), 0, beat, v });
    }
}
void session_note_off(Session* s, int pitch) {
    Track* t = armed_track_ptr(s);
    if (!t || t->is_audio || pitch < 0 || pitch > 127) return;
    const double beat = s->play_beats.load(std::memory_order_relaxed);
    s->live_in.push(LiveMidi::kNoteOff, static_cast<uint16_t>(pitch), 0.f, beat);
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
    s->tracks[track]->preview_in.push(LiveMidi::kNoteOn, static_cast<uint16_t>(pitch), vel, 0.0);
}
void session_preview_off(Session* s, int track, int pitch) {
    if (!s || track < 0 || track >= static_cast<int>(s->tracks.size())) return;
    if (s->tracks[track]->is_audio || pitch < 0 || pitch > 127) return;
    s->tracks[track]->preview_in.push(LiveMidi::kNoteOff, static_cast<uint16_t>(pitch), 0.f, 0.0);
}

// Recording (M6.3). Start snaps the capture origin (optionally after a count-in of
// `count_in_beats`); stop closes any held notes, maps captures to clip-local beats
// (fmod by the clip length), and overdubs them into the armed track's active clip.
static int commit_recording(Session* s);
int session_set_recording(Session* s, bool on, double count_in_beats) {
    if (!s) return 0;
    if (on) {
        std::lock_guard<std::mutex> lk(s->rec_mtx);
        s->rec_notes.clear();
        s->rec_cc.clear();
        const double now = s->play_beats.load(std::memory_order_relaxed);
        s->rec_capture_from = now + (count_in_beats > 0 ? count_in_beats : 0.0);
        s->recording.store(true, std::memory_order_relaxed);
        return 0;
    } else {
        if (!s->recording.exchange(false, std::memory_order_relaxed)) return 0;
        return commit_recording(s);
    }
}
int  session_is_recording(Session* s) { return (s && s->recording.load(std::memory_order_relaxed)) ? 1 : 0; }
void session_set_metronome(Session* s, int on) { if (s) s->metronome.store(on != 0, std::memory_order_relaxed); }
int  session_get_metronome(Session* s) { return (s && s->metronome.load(std::memory_order_relaxed)) ? 1 : 0; }

// Returns 1 if a take was committed into the armed clip, else 0. The return exists so the CALLER
// can record an undo entry: EditGateway lives in app/ (layering rank 40) and this is audio/ (20), so
// reaching up to it here is forbidden — and before this, a take was not undoable at all, meaning an
// accidental record over a good clip was simply unrecoverable.
static int commit_recording(Session* s) {
    std::vector<RecNote> rec;
    std::vector<RecCc>   rcc;
    { std::lock_guard<std::mutex> lk(s->rec_mtx);
      const double now = s->play_beats.load(std::memory_order_relaxed);
      for (auto& r : s->rec_notes) if (r.open) { r.beat_off = now; r.open = false; }  // close held notes
      rec.swap(s->rec_notes);
      rcc.swap(s->rec_cc); }
    // Bail only if BOTH are empty: a pass that moved only the pedal or the mod wheel is still a take.
    if (rec.empty() && rcc.empty()) return 0;
    Track* t = armed_track_ptr(s);
    if (!t || t->is_audio) return 0;
    const int ti = session_armed_track(s);          // armed track *index* for session_set_clip
    if (ti < 0) return 0;
    const int sc = t->active.load(std::memory_order_relaxed);
    if (sc < 0 || sc >= static_cast<int>(t->clips.size())) return 0;
    const MidiClip& clip = t->clips[sc];
    const double len = clip.length > 0.0 ? clip.length : 4.0;

    // SUSTAIN FIRST, in absolute beats — before the fmod below folds everything into the loop.
    // Doing it after would break any note the pedal holds across the loop point.
    if (!rec.empty()) {
        std::vector<CcBp> pedal;
        for (const RecCc& c : rcc) if (c.cc == kCcSustain) pedal.push_back({ c.beat, c.value });
        if (!pedal.empty()) {
            std::vector<double> on(rec.size()), off(rec.size());
            for (size_t i = 0; i < rec.size(); ++i) { on[i] = rec[i].beat_on; off[i] = rec[i].beat_off; }
            apply_sustain(on.data(), off.data(), rec.size(), pedal);
            for (size_t i = 0; i < rec.size(); ++i) rec[i].beat_off = off[i];
        }
    }

    // Start from the clip's current notes (overdub) and append the captures, mapped to clip-local
    // beats. A zero/short clip defaults to a 4-beat loop.
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

    // Controllers -> clip lanes. CC64 is deliberately DROPPED once it has been baked into the note
    // durations above: keeping both would have the host extend the note AND the plugin sustain it,
    // so playback would run longer than what was played. (Baking is the Ableton default; a future
    // `sustain_mode` option would make the alternative explicit.)
    if (!rcc.empty()) {
        std::vector<CcLane> lanes;
        { const int have = session_clip_cc_count(s, ti, sc);
          if (have > 0) { lanes.resize(static_cast<size_t>(have));
                          const int got = session_get_clip_cc(s, ti, sc, lanes.data(), have);
                          lanes.resize(static_cast<size_t>(got > 0 ? got : 0)); } }
        std::vector<uint16_t> seen;
        for (const RecCc& c : rcc) {
            if (c.cc == kCcSustain) continue;
            if (std::find(seen.begin(), seen.end(), c.cc) == seen.end()) seen.push_back(c.cc);
        }
        for (uint16_t cc : seen) {
            std::vector<CcBp> pts;
            for (const RecCc& c : rcc) {
                if (c.cc != cc) continue;
                double t_local = std::fmod(c.beat - s->rec_capture_from, len);
                if (t_local < 0) t_local += len;
                pts.push_back({ t_local, c.value });
            }
            pts = decimate_cc(std::move(pts), kCcDecimateEps, kCcDecimateMinDt);
            if (pts.empty()) continue;
            // Replace a lane the take covered; leave lanes it never touched alone.
            auto it = std::find_if(lanes.begin(), lanes.end(), [cc](const CcLane& l) { return l.cc == cc; });
            if (it != lanes.end()) { it->bp = std::move(pts); }
            else if (lanes.size() < static_cast<size_t>(kMaxCcLanes)) {
                CcLane l; l.cc = cc; l.channel = 0; l.bp = std::move(pts);
                lanes.push_back(std::move(l));
            }
        }
        session_set_clip_cc(s, ti, sc, lanes.data(), static_cast<int>(lanes.size()));
    }
    return 1;
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
// Stop a track's playing clip: queue the -2 STOP sentinel (distinct from -1 = "nothing queued"). The RT
// boundary applies it like a launch — the clip goes idle (active = -1) on the next launch-quantize bar,
// releasing its held notes, and stays silent until a clip/scene is launched again.
void session_stop_track(Session* s, int t) {
    if (s && t >= 0 && t < static_cast<int>(s->tracks.size()))
        s->tracks[t]->queued.store(-2, std::memory_order_relaxed);
}
void session_stop_all(Session* s) {
    if (s) for (auto& tp : s->tracks) tp->queued.store(-2, std::memory_order_relaxed);
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
// ADR-0033 P4: rebuild a track's node-solo audible mask from its soloed_node_ids (UI thread; caller
// holds t->gmtx). Prunes ids no longer in the graph first, so if the solo set empties out (or every
// soloed node was deleted) the mask falls back to ~0 = ALL audible — never all-muted. Otherwise the
// mask is the union of each soloed node's signal path (ancestors + itself + descendants), by node index
// (== out_buf). Read lock-free by the audio-thread executor.
static void recompute_node_audible(Track* t) {
    if (!t) return;
    auto& ids = t->soloed_node_ids;
    ids.erase(std::remove_if(ids.begin(), ids.end(),
                             [&](int id) { return t->agraph.node_index(id) < 0; }), ids.end());
    if (ids.empty()) { t->node_audible_mask.store(~0ull, std::memory_order_relaxed); return; }
    uint64_t mask = 0;
    std::vector<int> path;
    for (int id : ids) {
        t->agraph.collect_signal_path(id, path);
        for (int nid : path) {
            const int idx = t->agraph.node_index(nid);
            if (idx >= 0 && idx < 64) mask |= (1ull << idx);
        }
    }
    t->node_audible_mask.store(mask, std::memory_order_relaxed);
}
// ADR-0022 P2a.2: resolve every cross-track control edge into its audio-thread apply form and publish
// (UI thread). For each edge: locate the src+dst tracks by stable id (their POSITION = region base),
// map node ids to compiled indices via the agraph, read the source modulator's control buffer from
// its compiled plan, and compute the absolute sample-0 index into the session control pool. An edge
// whose endpoints don't currently resolve (track/node gone, or the source isn't a control emitter) is
// dropped from this publish and revives when they reappear. Called on connect/disconnect, membership
// change, and any plan recompile (indices move). Cheap (few edges); UI-thread only.
static void republish_xctl(Session* s) {
    if (!s) return;
    std::vector<XCtlApply> resolved;
    resolved.reserve(s->xctl_edges.size());
    for (const XCtlEdge& e : s->xctl_edges) {
        int src_pos = -1, dst_pos = -1;
        for (size_t i = 0; i < s->tracks.size(); ++i) {
            if (s->tracks[i]->id == e.src_track_id) src_pos = static_cast<int>(i);
            if (s->tracks[i]->id == e.dst_track_id) dst_pos = static_cast<int>(i);
        }
        if (src_pos < 0 || dst_pos < 0) continue;
        Track* srcT = s->tracks[static_cast<size_t>(src_pos)].get();
        Track* dstT = s->tracks[static_cast<size_t>(dst_pos)].get();
        const int src_idx = srcT->agraph.node_index(e.src_node_id);
        const int dst_idx = dstT->agraph.node_index(e.dst_node_id);
        if (src_idx < 0 || dst_idx < 0) continue;
        int src_ctl_buf = -1;   // the source modulator's control buffer, from its compiled plan
        for (const auto& st : srcT->gcg_edit.steps) if (st.out_buf == src_idx) { src_ctl_buf = st.control_out_buf; break; }
        if (src_ctl_buf < 0) continue;   // source node isn't a control emitter
        XCtlApply a;
        a.dst_track_id  = e.dst_track_id;
        a.dst_out_buf   = dst_idx;
        a.dst_param     = e.dst_param;
        a.src_pool_index = static_cast<size_t>(src_pos) * kGraphMaxNodes * kGraphMaxBlock
                         + static_cast<size_t>(src_ctl_buf) * kGraphMaxBlock;
        a.shape = e.shape;
        resolved.push_back(a);
    }
    std::lock_guard<std::mutex> lk(s->xctl_mtx);
    s->xctl_ho = resolved;   // UI thread; the audio thread swaps it in
    s->xctl_gen.fetch_add(1, std::memory_order_release);
}
// ADR-0022 P2a.2: create a cross-track control edge (a modulator on `src_track` node `src_node`
// driving `dst_track` node `dst_node`'s `param`). Tracks are INDICES (converted to stable ids for
// storage so the edge survives reorders); nodes are stable agraph node ids. Returns 1 on success, 0
// on a bad track/node or a duplicate (src,dst,param).
int session_connect_control(Session* s, int src_track, int src_node, int dst_track, int dst_node,
                            int param, float amount, float curve, int invert, int bipolar) {
    if (!s) return 0;
    const int nt = static_cast<int>(s->tracks.size());
    if (src_track < 0 || src_track >= nt || dst_track < 0 || dst_track >= nt || param < 0) return 0;
    Track* srcT = s->tracks[static_cast<size_t>(src_track)].get();
    Track* dstT = s->tracks[static_cast<size_t>(dst_track)].get();
    if (srcT->agraph.node_index(src_node) < 0 || dstT->agraph.node_index(dst_node) < 0) return 0;
    const int src_id = srcT->id, dst_id = dstT->id;
    for (const XCtlEdge& e : s->xctl_edges)
        if (e.src_track_id == src_id && e.src_node_id == src_node &&
            e.dst_track_id == dst_id && e.dst_node_id == dst_node && e.dst_param == param) return 0;  // duplicate
    XCtlEdge e;
    e.src_track_id = src_id; e.src_node_id = src_node;
    e.dst_track_id = dst_id; e.dst_node_id = dst_node; e.dst_param = param;
    e.shape.amount = amount; e.shape.curve = curve; e.shape.invert = invert != 0; e.shape.bipolar = bipolar != 0;
    s->xctl_edges.push_back(e);
    republish_xctl(s);
    return 1;
}
void session_disconnect_control(Session* s, int src_track, int src_node, int dst_track, int dst_node, int param) {
    if (!s) return;
    const int nt = static_cast<int>(s->tracks.size());
    if (src_track < 0 || src_track >= nt || dst_track < 0 || dst_track >= nt) return;
    const int src_id = s->tracks[static_cast<size_t>(src_track)]->id;
    const int dst_id = s->tracks[static_cast<size_t>(dst_track)]->id;
    bool changed = false;
    for (size_t i = 0; i < s->xctl_edges.size();) {
        const XCtlEdge& e = s->xctl_edges[i];
        if (e.src_track_id == src_id && e.src_node_id == src_node &&
            e.dst_track_id == dst_id && e.dst_node_id == dst_node && e.dst_param == param) {
            s->xctl_edges.erase(s->xctl_edges.begin() + static_cast<long>(i)); changed = true;
        } else ++i;
    }
    if (changed) republish_xctl(s);
}
// ADR-0022 P2a.3: re-shape an existing cross-track control edge without rewiring. Returns false if
// no edge matches (src,dst,param).
bool session_set_control_shape(Session* s, int src_track, int src_node, int dst_track, int dst_node,
                               int param, float amount, float curve, int invert, int bipolar) {
    if (!s) return false;
    const int nt = static_cast<int>(s->tracks.size());
    if (src_track < 0 || src_track >= nt || dst_track < 0 || dst_track >= nt) return false;
    const int src_id = s->tracks[static_cast<size_t>(src_track)]->id;
    const int dst_id = s->tracks[static_cast<size_t>(dst_track)]->id;
    for (XCtlEdge& e : s->xctl_edges) {
        if (e.src_track_id == src_id && e.src_node_id == src_node &&
            e.dst_track_id == dst_id && e.dst_node_id == dst_node && e.dst_param == param) {
            e.shape.amount = amount; e.shape.curve = curve; e.shape.invert = invert != 0; e.shape.bipolar = bipolar != 0;
            republish_xctl(s);
            return true;
        }
    }
    return false;
}
// ADR-0022 P2a.3: enumerate cross-track control edges for persist / introspection. `session_xctl_get`
// fills the edge at `i` with track INDICES (resolved from the stored stable ids); returns false if
// `i` is out of range or an endpoint track no longer exists.
int session_xctl_count(Session* s) { return s ? static_cast<int>(s->xctl_edges.size()) : 0; }
bool session_xctl_get(Session* s, int i, int* src_track, int* src_node, int* dst_track, int* dst_node,
                      int* param, float* amount, float* curve, int* invert, int* bipolar) {
    if (!s || i < 0 || i >= static_cast<int>(s->xctl_edges.size())) return false;
    const XCtlEdge& e = s->xctl_edges[static_cast<size_t>(i)];
    int src_idx = -1, dst_idx = -1;
    for (size_t k = 0; k < s->tracks.size(); ++k) {
        if (s->tracks[k]->id == e.src_track_id) src_idx = static_cast<int>(k);
        if (s->tracks[k]->id == e.dst_track_id) dst_idx = static_cast<int>(k);
    }
    if (src_idx < 0 || dst_idx < 0) return false;
    if (src_track) *src_track = src_idx;   if (src_node) *src_node = e.src_node_id;
    if (dst_track) *dst_track = dst_idx;   if (dst_node) *dst_node = e.dst_node_id;
    if (param) *param = e.dst_param;
    if (amount) *amount = e.shape.amount;  if (curve) *curve = e.shape.curve;
    if (invert) *invert = e.shape.invert ? 1 : 0;   if (bipolar) *bipolar = e.shape.bipolar ? 1 : 0;
    return true;
}
// ADR-0022 P2b.4: resolve every cross-track AUDIO edge into its RT-applicable form. Mirrors
// republish_xctl: scan `s->tracks` for the endpoint positions, resolve the source to its node-pool
// REGION BASE (track position · the per-track region stride) and its node index (out_buf), and the
// dst to its node index. Publishes into `xaudio_ho` via the mutex+gen handoff; the audio thread swaps
// it into `xaudio_view`. Skips an edge whose endpoints are gone or uncompiled.
static void republish_xaudio(Session* s) {
    if (!s) return;
    std::vector<XAudioApply> resolved;
    resolved.reserve(s->xaudio_edges.size());
    for (const XAudioEdge& e : s->xaudio_edges) {
        int src_pos = -1, dst_pos = -1;
        for (size_t i = 0; i < s->tracks.size(); ++i) {
            if (s->tracks[i]->id == e.src_track_id) src_pos = static_cast<int>(i);
            if (s->tracks[i]->id == e.dst_track_id) dst_pos = static_cast<int>(i);
        }
        if (src_pos < 0 || dst_pos < 0) continue;
        Track* srcT = s->tracks[static_cast<size_t>(src_pos)].get();
        Track* dstT = s->tracks[static_cast<size_t>(dst_pos)].get();
        const int src_idx = srcT->agraph.node_index(e.src_node_id);
        const int dst_idx = dstT->agraph.node_index(e.dst_node_id);
        if (src_idx < 0 || dst_idx < 0) continue;
        XAudioApply a;
        a.src_track_id = e.src_track_id;
        a.dst_track_id = e.dst_track_id;
        a.dst_out_buf  = dst_idx;
        a.src_out_buf  = src_idx;
        a.src_pool_base = static_cast<size_t>(src_pos) * (kGraphMaxNodes + 1) * 2 * kGraphMaxBlock;
        resolved.push_back(a);
    }
    std::lock_guard<std::mutex> lk(s->xaudio_mtx);
    s->xaudio_ho = resolved;
    s->xaudio_gen.fetch_add(1, std::memory_order_release);
}
// Would adding src_id -> dst_id (source renders before dst) create a cross-track cycle? A cycle
// exists iff dst can already reach src by following existing edges (dst →* src), so the render order
// could not be topo-sorted. UI thread; scans the small edge list.
static bool xaudio_would_cycle(Session* s, int src_id, int dst_id) {
    if (src_id == dst_id) return true;
    std::vector<int> stack{ dst_id };
    std::vector<int> seen;
    while (!stack.empty()) {
        const int n = stack.back(); stack.pop_back();
        if (n == src_id) return true;
        if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
        seen.push_back(n);
        for (const XAudioEdge& e : s->xaudio_edges)
            if (e.src_track_id == n) stack.push_back(e.dst_track_id);
    }
    return false;
}
// ADR-0022 P2b.4: create a cross-track audio edge (node `src_node` on `src_track` summed into node
// `dst_node` on `dst_track`). Tracks are INDICES (stored as stable ids so the edge survives reorders);
// nodes are stable agraph node ids. Returns 1 on success; 0 on a bad track/node, a same-track edge (use
// the in-track graph), a dst that is a SOURCE node (no audio input to sum into), a duplicate, or a
// cross-track cycle.
int session_connect_audio(Session* s, int src_track, int src_node, int dst_track, int dst_node) {
    if (!s) return 0;
    const int nt = static_cast<int>(s->tracks.size());
    if (src_track < 0 || src_track >= nt || dst_track < 0 || dst_track >= nt) return 0;
    if (src_track == dst_track) return 0;   // same track → use the in-track graph, not a cross-track edge
    Track* srcT = s->tracks[static_cast<size_t>(src_track)].get();
    Track* dstT = s->tracks[static_cast<size_t>(dst_track)].get();
    const int src_idx = srcT->agraph.node_index(src_node);
    const int dst_idx = dstT->agraph.node_index(dst_node);
    if (src_idx < 0 || dst_idx < 0) return 0;
    // A source/instrument node has no summed audio input, so it can't be a cross-track destination.
    const auto& dn = dstT->agraph.nodes();
    if (dst_idx < static_cast<int>(dn.size()) && dn[static_cast<size_t>(dst_idx)].is_source) return 0;
    const int src_id = srcT->id, dst_id = dstT->id;
    for (const XAudioEdge& e : s->xaudio_edges)
        if (e.src_track_id == src_id && e.src_node_id == src_node &&
            e.dst_track_id == dst_id && e.dst_node_id == dst_node) return 0;   // duplicate
    if (xaudio_would_cycle(s, src_id, dst_id)) return 0;
    XAudioEdge e;
    e.src_track_id = src_id; e.src_node_id = src_node;
    e.dst_track_id = dst_id; e.dst_node_id = dst_node;
    s->xaudio_edges.push_back(e);
    republish_xaudio(s);
    return 1;
}
void session_disconnect_audio(Session* s, int src_track, int src_node, int dst_track, int dst_node) {
    if (!s) return;
    const int nt = static_cast<int>(s->tracks.size());
    if (src_track < 0 || src_track >= nt || dst_track < 0 || dst_track >= nt) return;
    const int src_id = s->tracks[static_cast<size_t>(src_track)]->id;
    const int dst_id = s->tracks[static_cast<size_t>(dst_track)]->id;
    bool changed = false;
    for (size_t i = 0; i < s->xaudio_edges.size();) {
        const XAudioEdge& e = s->xaudio_edges[i];
        if (e.src_track_id == src_id && e.src_node_id == src_node &&
            e.dst_track_id == dst_id && e.dst_node_id == dst_node) {
            s->xaudio_edges.erase(s->xaudio_edges.begin() + static_cast<long>(i)); changed = true;
        } else ++i;
    }
    if (changed) republish_xaudio(s);
}
// ADR-0022 P2b.4: enumerate cross-track audio edges for persist / introspection. Fills the edge at `i`
// with track INDICES (resolved from stored stable ids); false if `i` out of range or an endpoint gone.
int session_xaudio_count(Session* s) { return s ? static_cast<int>(s->xaudio_edges.size()) : 0; }
bool session_xaudio_get(Session* s, int i, int* src_track, int* src_node, int* dst_track, int* dst_node) {
    if (!s || i < 0 || i >= static_cast<int>(s->xaudio_edges.size())) return false;
    const XAudioEdge& e = s->xaudio_edges[static_cast<size_t>(i)];
    int src_idx = -1, dst_idx = -1;
    for (size_t k = 0; k < s->tracks.size(); ++k) {
        if (s->tracks[k]->id == e.src_track_id) src_idx = static_cast<int>(k);
        if (s->tracks[k]->id == e.dst_track_id) dst_idx = static_cast<int>(k);
    }
    if (src_idx < 0 || dst_idx < 0) return false;
    if (src_track) *src_track = src_idx;   if (src_node) *src_node = e.src_node_id;
    if (dst_track) *dst_track = dst_idx;   if (dst_node) *dst_node = e.dst_node_id;
    return true;
}
// ADR-0022 P2b.5: resolve every cross-track NOTE edge into its RT-applicable form. Mirrors
// republish_xaudio: find the endpoint tracks, resolve the source node's note-out buffer to a stable
// pointer into the source track's `npool`, and the dst to its node index. Skips an edge whose
// endpoints are gone, uncompiled, or whose source isn't a note emitter (no note_out_buf).
static void republish_xnote(Session* s) {
    if (!s) return;
    std::vector<XNoteApply> resolved;
    resolved.reserve(s->xnote_edges.size());
    for (const XNoteEdge& e : s->xnote_edges) {
        int src_pos = -1, dst_pos = -1;
        for (size_t i = 0; i < s->tracks.size(); ++i) {
            if (s->tracks[i]->id == e.src_track_id) src_pos = static_cast<int>(i);
            if (s->tracks[i]->id == e.dst_track_id) dst_pos = static_cast<int>(i);
        }
        if (src_pos < 0 || dst_pos < 0) continue;
        Track* srcT = s->tracks[static_cast<size_t>(src_pos)].get();
        Track* dstT = s->tracks[static_cast<size_t>(dst_pos)].get();
        const int src_idx = srcT->agraph.node_index(e.src_node_id);
        const int dst_idx = dstT->agraph.node_index(e.dst_node_id);
        if (src_idx < 0 || dst_idx < 0) continue;
        int src_note_buf = -1;   // the source node's note-out buffer, from its compiled plan
        for (const auto& st : srcT->gcg_edit.steps) if (st.out_buf == src_idx) { src_note_buf = st.note_out_buf; break; }
        if (src_note_buf < 0 || src_note_buf >= static_cast<int>(srcT->npool.size())) continue;   // not a note emitter
        XNoteApply a;
        a.src_track_id = e.src_track_id;
        a.dst_track_id = e.dst_track_id;
        a.dst_out_buf  = dst_idx;
        a.src_notes    = &srcT->npool[static_cast<size_t>(src_note_buf)];
        resolved.push_back(a);
    }
    std::lock_guard<std::mutex> lk(s->xnote_mtx);
    s->xnote_ho = resolved;
    s->xnote_gen.fetch_add(1, std::memory_order_release);
}
static bool xnote_would_cycle(Session* s, int src_id, int dst_id) {
    if (src_id == dst_id) return true;
    std::vector<int> stack{ dst_id };
    std::vector<int> seen;
    while (!stack.empty()) {
        const int n = stack.back(); stack.pop_back();
        if (n == src_id) return true;
        if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
        seen.push_back(n);
        for (const XNoteEdge& e : s->xnote_edges)
            if (e.src_track_id == n) stack.push_back(e.dst_track_id);
    }
    return false;
}
// ADR-0022 P2b.5: create a cross-track note edge (note-emitting `src_node` on `src_track` merged into
// note-consuming `dst_node` on `dst_track`). Returns 1 on success; 0 on a bad track/node, a same-track
// edge (use the in-track graph), a source that doesn't emit notes / a dst that doesn't consume notes, a
// duplicate, or a cross-track cycle.
int session_connect_note(Session* s, int src_track, int src_node, int dst_track, int dst_node) {
    if (!s) return 0;
    const int nt = static_cast<int>(s->tracks.size());
    if (src_track < 0 || src_track >= nt || dst_track < 0 || dst_track >= nt) return 0;
    if (src_track == dst_track) return 0;   // same track → use the in-track graph
    Track* srcT = s->tracks[static_cast<size_t>(src_track)].get();
    Track* dstT = s->tracks[static_cast<size_t>(dst_track)].get();
    const int src_idx = srcT->agraph.node_index(src_node);
    const int dst_idx = dstT->agraph.node_index(dst_node);
    if (src_idx < 0 || dst_idx < 0) return 0;
    const auto& sn = srcT->agraph.nodes();
    const auto& dn = dstT->agraph.nodes();
    if (src_idx >= static_cast<int>(sn.size()) || !sn[static_cast<size_t>(src_idx)].note_out) return 0;   // src must emit notes
    if (dst_idx >= static_cast<int>(dn.size()) || !dn[static_cast<size_t>(dst_idx)].note_in) return 0;    // dst must consume notes
    const int src_id = srcT->id, dst_id = dstT->id;
    for (const XNoteEdge& e : s->xnote_edges)
        if (e.src_track_id == src_id && e.src_node_id == src_node &&
            e.dst_track_id == dst_id && e.dst_node_id == dst_node) return 0;   // duplicate
    if (xnote_would_cycle(s, src_id, dst_id)) return 0;
    XNoteEdge e;
    e.src_track_id = src_id; e.src_node_id = src_node;
    e.dst_track_id = dst_id; e.dst_node_id = dst_node;
    s->xnote_edges.push_back(e);
    republish_xnote(s);
    return 1;
}
void session_disconnect_note(Session* s, int src_track, int src_node, int dst_track, int dst_node) {
    if (!s) return;
    const int nt = static_cast<int>(s->tracks.size());
    if (src_track < 0 || src_track >= nt || dst_track < 0 || dst_track >= nt) return;
    const int src_id = s->tracks[static_cast<size_t>(src_track)]->id;
    const int dst_id = s->tracks[static_cast<size_t>(dst_track)]->id;
    bool changed = false;
    for (size_t i = 0; i < s->xnote_edges.size();) {
        const XNoteEdge& e = s->xnote_edges[i];
        if (e.src_track_id == src_id && e.src_node_id == src_node &&
            e.dst_track_id == dst_id && e.dst_node_id == dst_node) {
            s->xnote_edges.erase(s->xnote_edges.begin() + static_cast<long>(i)); changed = true;
        } else ++i;
    }
    if (changed) republish_xnote(s);
}
int session_xnote_count(Session* s) { return s ? static_cast<int>(s->xnote_edges.size()) : 0; }
bool session_xnote_get(Session* s, int i, int* src_track, int* src_node, int* dst_track, int* dst_node) {
    if (!s || i < 0 || i >= static_cast<int>(s->xnote_edges.size())) return false;
    const XNoteEdge& e = s->xnote_edges[static_cast<size_t>(i)];
    int src_idx = -1, dst_idx = -1;
    for (size_t k = 0; k < s->tracks.size(); ++k) {
        if (s->tracks[k]->id == e.src_track_id) src_idx = static_cast<int>(k);
        if (s->tracks[k]->id == e.dst_track_id) dst_idx = static_cast<int>(k);
    }
    if (src_idx < 0 || dst_idx < 0) return false;
    if (src_track) *src_track = src_idx;   if (src_node) *src_node = e.src_node_id;
    if (dst_track) *dst_track = dst_idx;   if (dst_node) *dst_node = e.dst_node_id;
    return true;
}
// ADR-0022 P2b: drop every session-level cross-track edge (control / audio / note) and republish the
// now-empty resolved lists. Called at the start of a session load so a loaded document fully REPLACES
// the cross-track edges rather than leaking the previous session's; the load then restores the file's
// edges by index. UI/main thread.
void session_clear_cross_track_edges(Session* s) {
    if (!s) return;
    s->xctl_edges.clear();
    s->xaudio_edges.clear();
    s->xnote_edges.clear();
    republish_xctl(s);
    republish_xaudio(s);
    republish_xnote(s);
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
int   session_master_gnid(Session* s) { return s ? s->master.gnid : -1; }   // ADR-0022 P2b.3c: the sink's global node id
void  session_set_master_gain(Session* s, float g) { if (s) s->master.gain.store(std::max(0.f, g), std::memory_order_relaxed); }
// Scene-launch quantization in bars (1 = next bar; typically 4 = let the phrase finish).
int   session_launch_quantum_bars(Session* s) { return s ? s->launch_quantum_bars.load(std::memory_order_relaxed) : 1; }
void  session_set_launch_quantum_bars(Session* s, int bars) { if (s) s->launch_quantum_bars.store(std::max(1, bars), std::memory_order_relaxed); }
// Session music-theory context (root + scale NAME). The core stores two strings; the theory
// vocabulary + validation live in the Python bridge (mcp/theory.py). UI/main thread only.
const char* session_music_root(Session* s)  { return s ? s->music_root.c_str()  : "C"; }
const char* session_music_scale(Session* s) { return s ? s->music_scale.c_str() : "major"; }
void session_set_music(Session* s, const char* root, const char* scale) {
    if (!s) return;
    if (root  && *root)  s->music_root  = root;
    if (scale && *scale) s->music_scale = scale;
}
// The analysis / publication READ surface (meters, note scalars, held notes, spectrum + per-node FFT
// rings, node control-out) moved to vst3_host_analysis.cpp (ADR-0025) — cold frame-thread accessors over
// state the render path below publishes. The capture-snapshot API stays here (it's the export ring).
int session_track_capture_snapshot(Session* s, int t, double seconds, std::vector<float>& outL,
                                   std::vector<float>& outR, uint32_t* out_sample_rate) {
    outL.clear(); outR.clear();
    if (out_sample_rate) *out_sample_rate = 0;
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0;
    Track& tr = *s->tracks[t];
    std::lock_guard<std::mutex> lk(tr.capture_mtx);
    if (tr.capture_l.empty() || tr.capture_filled == 0 || tr.capture_sample_rate == 0) return 0;
    size_t want = tr.capture_filled;
    if (seconds > 0.0) {
        want = std::min(tr.capture_filled,
                        static_cast<size_t>(seconds * static_cast<double>(tr.capture_sample_rate)));
    }
    if (want == 0) return 0;
    outL.resize(want); outR.resize(want);
    const size_t cap = tr.capture_l.size();
    const size_t start = (tr.capture_write_pos + cap - want) % cap;
    for (size_t i = 0; i < want; ++i) {
        const size_t src = (start + i) % cap;
        outL[i] = tr.capture_l[src];
        outR[i] = tr.capture_r[src];
    }
    if (out_sample_rate) *out_sample_rate = tr.capture_sample_rate;
    return static_cast<int>(want);
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

// The per-track preset browse/load C API (session_track_preset_*) lives in vst3_host_presets.cpp
// (ADR-0025 split) — pure adapter glue with no engine state; declared in vst3_host.h.
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
    return s->tracks[t]->aud_clips[sc].peak_bins(out, n);   // cached; see AudioClip::peak_bins
}
int session_audio_copy_pcm(Session* s, int t, int sc, std::vector<float>& outL, std::vector<float>& outR,
                           uint32_t* out_sample_rate) {
    outL.clear(); outR.clear();
    if (out_sample_rate) *out_sample_rate = 0;
    if (!aud_valid(s, t, sc)) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    const AudioClip& smp = s->tracks[t]->aud_clips[sc];
    if (!smp.ok()) return 0;
    outL = smp.L;
    outR = smp.R.empty() ? smp.L : smp.R;
    if (out_sample_rate) *out_sample_rate = smp.sr ? smp.sr : (s->sample_rate ? s->sample_rate : 48000);
    return static_cast<int>(outL.size());
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
// Read on the UI thread; src_path is only ever written on the UI thread (audio_clip_load_wav / the swap
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
    AudioClip smp;
    if (!audio_clip_load_wav(path, s->sample_rate, src_bpm > 0.0 ? src_bpm : 120.0, smp)) return false;
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
// Optimistic-concurrency revision for a clip's note content (see MidiClip::rev). 0 for an invalid cell.
uint64_t session_clip_rev(Session* s, int t, int sc) {
    if (!clip_valid(s, t, sc)) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->edit_mtx);
    return s->tracks[t]->edit_clips[sc].rev;
}
void session_set_clip(Session* s, int t, int sc, const ClipNote* notes, int n, double length) {
    if (!clip_valid(s, t, sc)) return;
    Track& tr = *s->tracks[t];
    bool was_empty, now_empty;
    {
        std::lock_guard<std::mutex> lk(tr.edit_mtx);
        was_empty = tr.edit_clips[sc].notes.empty();
        tr.edit_clips[sc].notes.assign(notes, notes + (n > 0 ? n : 0));
        tr.edit_clips[sc].length = length > 0 ? length : tr.edit_clips[sc].length;
        tr.edit_clips[sc].rev++;   // optimistic-concurrency: every note-content write advances the revision
        now_empty = tr.edit_clips[sc].notes.empty();
    }
    tr.edit_gen.fetch_add(1, std::memory_order_release);
    // An empty scene slot has NO Clip node in the audio graph; a populated one does. When a slot flips
    // empty<->populated, REPUBLISH (reconcile + recompile the existing graph in place) so its Clip node
    // is added/removed — NOT a full rebuild_track_graph, which would reset a derived graph and wipe
    // per-node edits like key-splits.
    if (!tr.is_audio && was_empty != now_empty) { std::lock_guard<std::mutex> lk(tr.gmtx); republish_track_graph(&tr); }
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

// --- Clip-level controller automation (P4) ---
// Deliberately separate writes from session_set_clip: every existing note-editing caller passes
// notes only, so folding lanes into that call would wipe recorded automation on any transpose or
// quantize. They share `rev`, so a read-modify-write client still has one token for the clip.
int session_clip_cc_count(Session* s, int t, int sc) {
    if (!clip_valid(s, t, sc)) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->edit_mtx);
    return static_cast<int>(s->tracks[t]->edit_clips[sc].cc.size());
}
int session_get_clip_cc(Session* s, int t, int sc, CcLane* out, int max) {
    if (!clip_valid(s, t, sc) || !out || max <= 0) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->edit_mtx);
    const auto& lanes = s->tracks[t]->edit_clips[sc].cc;
    const int n = std::min(static_cast<int>(lanes.size()), max);
    for (int i = 0; i < n; ++i) out[i] = lanes[i];
    return n;
}
void session_set_clip_cc(Session* s, int t, int sc, const CcLane* lanes, int n) {
    if (!clip_valid(s, t, sc)) return;
    Track& tr = *s->tracks[t];
    {
        std::lock_guard<std::mutex> lk(tr.edit_mtx);
        tr.edit_clips[sc].cc.assign(lanes, lanes + (n > 0 ? std::min(n, kMaxCcLanes) : 0));
        tr.edit_clips[sc].rev++;
    }
    tr.edit_gen.fetch_add(1, std::memory_order_release);
    // No republish: unlike notes, a CC lane does not change whether the slot has a Clip node.
}

// --- Clip pool (loose clips outside the grid) — UI/main thread only. ---
static bool pool_valid(Session* s, int i) { return s && i >= 0 && i < static_cast<int>(s->pool.size()); }
int session_pool_cc_count(Session* s, int i) {
    if (!pool_valid(s, i)) return 0;
    return static_cast<int>(s->pool[i].clip.cc.size());
}
int session_pool_get_cc(Session* s, int i, CcLane* out, int max) {
    if (!pool_valid(s, i) || !out || max <= 0) return 0;
    const auto& lanes = s->pool[i].clip.cc;
    const int n = std::min(static_cast<int>(lanes.size()), max);
    for (int k = 0; k < n; ++k) out[k] = lanes[k];
    return n;
}
void session_pool_set_cc(Session* s, int i, const CcLane* lanes, int n) {
    if (!pool_valid(s, i)) return;
    s->pool[i].clip.cc.assign(lanes, lanes + (n > 0 ? std::min(n, kMaxCcLanes) : 0));
}
int session_pool_count(Session* s) { return s ? static_cast<int>(s->pool.size()) : 0; }
double session_pool_length(Session* s, int i) {
    if (!pool_valid(s, i)) return 0.0;
    return s->pool[i].is_audio ? s->pool[i].audio.loop_beats : s->pool[i].clip.length;
}
const char* session_pool_name(Session* s, int i) { return pool_valid(s, i) ? s->pool[i].name.c_str() : ""; }
int session_pool_note_count(Session* s, int i) {
    if (!pool_valid(s, i)) return 0;
    return static_cast<int>(s->pool[i].clip.notes.size());
}
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
bool session_pool_is_audio(Session* s, int i) { return pool_valid(s, i) && s->pool[i].is_audio; }
int  session_pool_audio_bpm(Session* s, int i) {
    return (pool_valid(s, i) && s->pool[i].is_audio) ? static_cast<int>(std::lround(s->pool[i].audio.src_bpm)) : 0;
}
int  session_pool_audio_waveform(Session* s, int i, float* out, int n) {
    return (pool_valid(s, i) && s->pool[i].is_audio) ? s->pool[i].audio.peak_bins(out, n) : 0;  // cached
}
// MOVE an audio grid clip into the pool: the source cell is cleared (under aud_mtx so the
// audio thread never sees a torn AudioClip). Returns the new pool index, or -1.
int session_pool_stash_audio(Session* s, int t, int sc, const char* name) {
    if (!aud_valid(s, t, sc)) return -1;
    Track& tr = *s->tracks[t];
    if (!tr.aud_clips[sc].ok()) return -1;   // empty cell — nothing to stash
    PoolClip pc; pc.is_audio = true;
    {
        std::lock_guard<std::mutex> lk(tr.aud_mtx);
        pc.audio = std::move(tr.aud_clips[sc]);   // O(1) move out
        tr.aud_clips[sc] = AudioClip{};             // leave an empty cell
    }
    pc.name = name ? name : "";
    s->pool.push_back(std::move(pc));
    return static_cast<int>(s->pool.size()) - 1;
}
// Copy a pooled audio clip into an audio grid cell (under aud_mtx). The pool keeps its copy.
bool session_pool_place_audio(Session* s, int i, int t, int sc) {
    if (!pool_valid(s, i) || !s->pool[i].is_audio || !aud_valid(s, t, sc)) return false;
    Track& tr = *s->tracks[t];
    AudioClip copy = s->pool[i].audio;   // copy the PCM on the UI thread before locking
    {
        std::lock_guard<std::mutex> lk(tr.aud_mtx);
        tr.aud_clips[sc] = std::move(copy);
    }
    tr.aud_trim0[sc].store(0.f, std::memory_order_relaxed);
    tr.aud_trim1[sc].store(1.f, std::memory_order_relaxed);
    return true;
}

// AudioClip-loop source: render the active-scene clip into L/R (silence if not playing / no clip /
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

// One track's DSP: run its compiled-graph node steps into its OWN node-pool region, then finalize into
// its OWN track_out slot. Each track is an island (distinct pool region + distinct output slot, no
// cross-track writes), so this is safe to run in parallel across tracks (see the parallel executor in
// session_process). Bit-identical to the flat-plan Node/Finalize steps it replaces.
static void process_one_track(Session* s, uint32_t slot, uint32_t frames, uint32_t sample_rate) {
    Track& t = *s->render_list[slot];
    const vivid::audio::CompiledAudioGraph& cg = t.gcg;
    // RT bail-net: decided ONCE so a track's node steps and its finalize can never disagree.
    const bool valid =
        t.gok && frames <= kGraphMaxBlock && cg.output_buf >= 0 && !cg.steps.empty() && t.blk.node_pool
        && static_cast<int>(t.gbinds.size()) >= cg.buf_count
        && static_cast<size_t>(cg.buf_count + 1) * 2 * frames <= static_cast<size_t>(kGraphMaxNodes + 1) * 2 * kGraphMaxBlock;
    if (valid) {
        float* pool = t.blk.node_pool + t.blk.node_base;   // its region of the session node pool
        const VividAudioContext gctx = block_gctx(t.blk);
        for (const vivid::audio::CompiledStep& st : cg.steps) {
            process_step(st, t, pool, frames /*stride*/, t.gcg.buf_count /*scratch*/, t.blk, gctx, frames);
            // ADR-0033 P4: node solo — zero a muted node's output AFTER it ran (voice/plugin state is
            // untouched, so clearing solo restores it with no dropped note-on). ~0 mask = no-op (common).
            const int ob = st.out_buf;
            if (ob >= 0 && ob < 64 && !((t.node_audible_mask.load(std::memory_order_relaxed) >> ob) & 1ull)) {
                float* oL = pool + static_cast<size_t>(ob) * 2 * frames;
                std::memset(oL, 0, frames * sizeof(float));
                std::memset(oL + frames, 0, frames * sizeof(float));
            }
        }
    }
    finalize_track(t, s->track_out_pool.data() + static_cast<size_t>(slot) * 2 * kGraphMaxBlock,
                   valid, frames, sample_rate);
}

#if defined(__APPLE__)
// A persistent audio worker (ADR-0052): waits for the master (audio callback) to post `aw_go`,
// work-steals tracks from the shared atomic index, processes each (its own island), then decrements
// the participant barrier — posting `aw_done` when it is the last. Joins the CoreAudio device
// workgroup so the kernel schedules it real-time alongside the audio I/O thread (shared deadline).
static void audio_worker_main(Session* s) {
    os_workgroup_join_token_s tok;
    bool joined = false;
    if (s->aw_workgroup) joined = (os_workgroup_join(static_cast<os_workgroup_t>(s->aw_workgroup), &tok) == 0);
    while (s->aw_running.load(std::memory_order_acquire)) {
        dispatch_semaphore_wait(s->aw_go, DISPATCH_TIME_FOREVER);
        if (!s->aw_running.load(std::memory_order_acquire)) break;
        const uint32_t n = s->aw_n, frames = s->aw_frames, sr = s->aw_sr;   // published (release) before `go`
        uint32_t slot;
        while ((slot = s->aw_next_slot.fetch_add(1, std::memory_order_acq_rel)) < n)
            process_one_track(s, slot, frames, sr);
        if (s->aw_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
            dispatch_semaphore_signal(s->aw_done);
    }
    if (joined) os_workgroup_leave(static_cast<os_workgroup_t>(s->aw_workgroup), &tok);
}

// Fan the per-track DSP out to the worker pool AND the master (which also does work, to use every
// core). Returns true if it processed the tracks (parallel path), false if the caller should run the
// serial path — workers disabled, too few tracks to amortize dispatch, cross-track edges present, or
// no pool. RT-safe: scalar publish + atomics + two dispatch_semaphores; no alloc, no lock.
static bool run_tracks_parallel(Session* s, uint32_t n_tracks, uint32_t frames, uint32_t sample_rate, bool cross_track) {
    if (!s->aw_enabled || s->aw_n_workers <= 0 || cross_track || n_tracks < 2) return false;
    const int participants = std::min<int>(static_cast<int>(n_tracks), s->aw_n_workers + 1);
    const int wake = participants - 1;
    s->aw_frames = frames; s->aw_sr = sample_rate; s->aw_n = n_tracks;   // publish before posting `go`
    s->aw_next_slot.store(0, std::memory_order_release);
    s->aw_remaining.store(participants, std::memory_order_release);
    for (int i = 0; i < wake; ++i) dispatch_semaphore_signal(s->aw_go);
    uint32_t slot;                                                       // the master is a participant too
    while ((slot = s->aw_next_slot.fetch_add(1, std::memory_order_acq_rel)) < n_tracks)
        process_one_track(s, slot, frames, sample_rate);
    if (s->aw_remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
        dispatch_semaphore_signal(s->aw_done);
    dispatch_semaphore_wait(s->aw_done, DISPATCH_TIME_FOREVER);          // wait for all participants
    return true;
}
#else
static bool run_tracks_parallel(Session*, uint32_t, uint32_t, uint32_t, bool) { return false; }
#endif

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
        } else vivid::audio::health::note_handoff_skip();   // ADR-0031 §3: contention skip (kept stale)
    }
    if (s->tracks_view.empty()) return false;
    s->render_list.clear();   // ADR-0022 P1b: rebuilt each block; the master node sums it

    // ADR-0022 P2a.2: pick up a re-resolved cross-track control edge list (pointer-swap handoff, like
    // the per-track plan). On try_lock contention we keep the current list and retry next block.
    if (s->xctl_gen.load(std::memory_order_acquire) != s->xctl_gen_seen) {
        if (s->xctl_mtx.try_lock()) {
            s->xctl_view.swap(s->xctl_ho);
            s->xctl_gen_seen = s->xctl_gen.load(std::memory_order_acquire);
            s->xctl_mtx.unlock();
        } else vivid::audio::health::note_handoff_skip();   // ADR-0031 §3: contention skip (kept stale)
    }
    // ADR-0022 P2b.4: same handoff for the resolved cross-track AUDIO edges.
    if (s->xaudio_gen.load(std::memory_order_acquire) != s->xaudio_gen_seen) {
        if (s->xaudio_mtx.try_lock()) {
            s->xaudio_view.swap(s->xaudio_ho);
            s->xaudio_gen_seen = s->xaudio_gen.load(std::memory_order_acquire);
            s->xaudio_mtx.unlock();
        } else vivid::audio::health::note_handoff_skip();   // ADR-0031 §3: contention skip (kept stale)
    }
    // ADR-0022 P2b.5: same handoff for the resolved cross-track NOTE edges.
    if (s->xnote_gen.load(std::memory_order_acquire) != s->xnote_gen_seen) {
        if (s->xnote_mtx.try_lock()) {
            s->xnote_view.swap(s->xnote_ho);
            s->xnote_gen_seen = s->xnote_gen.load(std::memory_order_acquire);
            s->xnote_mtx.unlock();
        } else vivid::audio::health::note_handoff_skip();   // ADR-0031 §3: contention skip (kept stale)
    }

    const uint32_t bpb = beats_per_bar ? beats_per_bar : 4;
    // Scene switches fire on the next launch-quantization boundary (every `launch_quantum_bars` bars;
    // 1 = every bar). Beat 0 is a boundary (q_idx 0 != initial -1), so a scene queued before playback
    // starts launches immediately rather than waiting a phrase.
    const int qbars = std::max(1, s->launch_quantum_bars.load(std::memory_order_relaxed));
    const long long q_idx = static_cast<long long>(std::floor(beats / (static_cast<double>(bpb) * qbars)));
    const bool new_launch = q_idx != s->last_launch_q;
    s->last_launch_q = q_idx;
    const double delta = frames * (bpm / 60.0) / (sample_rate > 0 ? sample_rate : 48000);

    // ADR-0022 P2a.1b: apply each track's pending audio-graph plan swap (P1b.2 handoff) for EVERY
    // track — BEFORE the pre-pass and BEFORE the render loop's no-instrument skip. A modulator-only
    // track (a dedicated LFO track, no instrument) is skipped in the render loop, so if its plan were
    // only applied there its modulators would never reach the audio thread; applying here makes its
    // gok/plan current so the pre-pass runs its LFO. vector::swap is O(1); try_lock skips on contention.
    for (Track* tp : s->tracks_view) {
        Track& t = *tp;
        if (t.ggen.load(std::memory_order_acquire) == t.ggen_seen) continue;
        if (t.gmtx.try_lock()) {
            t.gcg.steps.swap(t.gcg_ho.steps);
            std::swap(t.gcg.buf_count,  t.gcg_ho.buf_count);
            std::swap(t.gcg.output_buf, t.gcg_ho.output_buf);
            t.gbinds.swap(t.gbinds_ho);
            std::swap(t.gok, t.gok_ho);
            t.ggen_seen = t.ggen.load(std::memory_order_acquire);
            t.gmtx.unlock();
        } else vivid::audio::health::note_handoff_skip();   // ADR-0031 §3: contention skip (kept stale)
    }

    // ADR-0022 P2a.1b: the modulator PRE-PASS — run every track's modulators into the session control
    // pool BEFORE any track renders audio, so control-edge resolution no longer depends on track
    // render order (the basis for cross-track modulation, P2a.2). In-track modulation is unchanged:
    // a modulator still runs exactly once per block, into the same region its consumer reads. Now that
    // the plan swap above ran for all tracks, the pre-pass sees the current plan. Modulators need only
    // transport + their control region.
    for (size_t tv_i = 0; tv_i < s->tracks_view.size(); ++tv_i) {
        Track& t = *s->tracks_view[tv_i];
        if (!t.gok) continue;
        GraphBlockCtx mb;
        mb.frames = frames; mb.sample_rate = sample_rate;
        mb.bpm = static_cast<float>(bpm); mb.bpb = bpb; mb.beats = beats;
        mb.ctl_pool = s->ctl_pool.data();
        mb.ctl_base = tv_i * static_cast<size_t>(kGraphMaxNodes) * kGraphMaxBlock;
        run_track_modulators(t, mb, s->mod_scratch.data(), s->mod_scratch.data() + kGraphMaxBlock, frames);
    }

    // ADR-0022 P2b.3a: PREP phase — build the render list (a track's index here IS its track-out slot)
    // and get every rendering track ready: apply pending edits (clips / fx / audio ops), prep the
    // note/event stream, set up the block context. Split from the RENDER phase below so all per-track
    // state is ready BEFORE any track renders — the shape the flat session executor (P2b.3b) needs.
    // The skip decision is made HERE, once (recorded in render_list), so an edit applied below can't
    // make the two phases disagree. Bit-identical: the work is per-track independent, hoisted ahead.
    for (size_t tv_i = 0; tv_i < s->tracks_view.size(); ++tv_i) {
        Track& t = *s->tracks_view[tv_i];
        // Skip a MIDI track only if it has NO source at all: no processing VST3 instrument
        // AND no native instrument operator (live or pending). A native-instrument-only track
        // (e.g. the AudioClip from slice-to-MIDI) has no VST3 handle but still must run.
        // A2: a plugin added as a graph NODE (session_audio_graph_add_plugin) lives in a
        // PluginSlot, NOT the legacy t.handle / t.clap_inst source slots — so a track whose
        // only instrument is a graph-node plugin would otherwise be skipped here and never
        // render (silent). Its instrument surfaces in the PUBLISHED plan as a Vst3Inst / ClapInst
        // node, so detect it there (t.gbinds is the audio-thread copy — reading t.pslots would
        // race the UI thread's push_back). A pending (still-loading) node already carries its
        // kind, so the track stays in the list and is live the instant the handle binds.
        bool graph_plugin_source = false;
        for (const GNodeBind& gb : t.gbinds)
            if (gb.kind == GNKind::Vst3Inst || gb.kind == GNKind::ClapInst) { graph_plugin_source = true; break; }
        if (!t.is_audio && (!t.handle || !t.handle->processing) && !t.op_instrument
            && !t.op_instrument_edit && !t.clap_inst && !graph_plugin_source) continue;
        s->render_list.push_back(&t);   // renders this block — its index in render_list is its track-out slot

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
                    t.clips[sc].cc         = t.edit_clips[sc].cc;           // P4 clip automation — omit
                                                                            // this and lanes persist,
                                                                            // edit and draw but never play
                    t.clips[sc].length     = t.edit_clips[sc].length;
                    t.clips[sc].loop_start = t.edit_clips[sc].loop_start;   // in-clip loop region
                    t.clips[sc].loop_end   = t.edit_clips[sc].loop_end;
                }
                // notes[] was re-assigned (may have reallocated) — the scheduler's
                // active[].src pointers now dangle. Null them (note-offs still fire).
                t.sched.invalidate_active_src();
                t.edit_gen_seen = t.edit_gen.load(std::memory_order_acquire);
                t.edit_mtx.unlock();
            } else vivid::audio::health::note_handoff_skip();   // ADR-0031 §3: contention skip (kept stale)
        }
        // Apply pending FX-chain edits (copy the UI's pointer list into the working
        // one; reserved capacity avoids a realloc). Only runs after an add/remove.
        if (t.fx_gen.load(std::memory_order_acquire) != t.fx_gen_seen) {
            if (t.fx_mtx.try_lock()) {
                t.effects = t.effects_edit;
                t.fx_gen_seen = t.fx_gen.load(std::memory_order_acquire);
                t.fx_mtx.unlock();
            } else vivid::audio::health::note_handoff_skip();   // ADR-0031 §3: contention skip (kept stale)
        }
        // Apply pending native audio-operator edits (instrument slot + effect chain).
        if (t.op_fx_gen.load(std::memory_order_acquire) != t.op_fx_gen_seen) {
            if (t.op_fx_mtx.try_lock()) {
                t.op_effects = t.op_effects_edit;
                t.op_instrument = t.op_instrument_edit;
                t.op_fx_gen_seen = t.op_fx_gen.load(std::memory_order_acquire);
                t.op_fx_mtx.unlock();
            } else vivid::audio::health::note_handoff_skip();   // ADR-0031 §3: contention skip (kept stale)
        }
        if (t.is_audio) {
            // Quantized scene switch (a transport action the AudioClip graph node reads each block).
            if (new_launch) {
                const int q = t.queued.load(std::memory_order_relaxed);
                if (q == -2) t.active.store(-1, std::memory_order_relaxed);   // STOP → idle
                else if (q >= 0 && q != t.active.load(std::memory_order_relaxed)) t.active.store(q, std::memory_order_relaxed);
                if (q != -1) t.queued.store(-1, std::memory_order_relaxed);   // clear the queue (launch OR stop)
            }
        } else {
            t.vev.clear();   // this block's VST3 event list (on the Track so the graph node can read it)
            t.scene_rel.clear();   // scene-switch note-offs for the CLAP path (parallel to t.vev)
            if (new_launch) {
                const int q = t.queued.load(std::memory_order_relaxed);
                const int old_scene = t.active.load(std::memory_order_relaxed);
                const bool do_stop   = (q == -2);   // STOP sentinel → the track goes idle
                const bool do_launch = (q >= 0 && q != old_scene && q < static_cast<int>(t.clips.size()));
                if (do_launch || do_stop) {
                    t.nev.clear(); t.eev.clear(); t.sched.flush(t.nev);   // outgoing CLIP's held notes
                    // ADR-0022 P3.3: if the outgoing scene's cell is a GENERATOR, release its held
                    // voices into the same scene_rel path. Found via the audio-thread PLAN (t.gbinds),
                    // not gen_cells (UI-thread-only). audio_op_note_flush also forgets the op's voices,
                    // so the generator starts clean when its scene is relaunched.
                    for (const GNodeBind& gb : t.gbinds)
                        if (gb.kind == GNKind::NativeGen && gb.scene == old_scene && gb.op) {
                            NoteEvent gof[64]; uint32_t gn = 0;
                            vivid::audio_op_note_flush(gb.op, gof, 64, &gn);
                            for (uint32_t i = 0; i < gn && t.nev.size() < kGraphMaxNotes; ++i) t.nev.push_back(gof[i]);
                            break;
                        }
                    emit_vst3(t.vev, t.nev, t.eev);
                    t.scene_rel.assign(t.nev.begin(), t.nev.end());   // keep the releases for CLAP (t.nev is cleared below)
                    if (do_launch) {
                        t.sched.reset(&t.clips[q], beats);   // anchor the launched clip to THIS bar so it starts at its beat 0
                        t.active.store(q, std::memory_order_relaxed);
                    } else {
                        t.active.store(-1, std::memory_order_relaxed);   // STOP → idle (held notes released above)
                    }
                }
                if (q != -1) t.queued.store(-1, std::memory_order_relaxed);   // clear the queue (launch OR stop)
            }
            // ADR-0022 P3.1b: split note production into two source streams that P3.1a's single
            // MidiIn used to carry together. nev_clip = the clip scheduler + play-stop release
            // flush (feeds the MidiClip node); nev_live = live MIDI + editor preview (feeds the
            // MidiIn node). t.nev is then rebuilt as nev_clip ++ nev_live — byte-identical to the
            // pre-split stream (same sources, same push order) — so the broadcast fallback and
            // blk.notes are unchanged. The instrument now reads BOTH via note edges, merged and
            // time-sorted by graph_note_input.
            t.nev_clip.clear(); t.eev.clear();
            if (release_all) {
                t.held.clear();                                      // stop → no notes held (instancer clears)
                t.sched.flush(t.nev_clip);                            // play->stop edge: release the clip's held notes
                // ADR-0022 P3.3: also release every generator's held voices (Euclid/RandMelody/…)
                // into the same stream — otherwise a note a generator is holding hangs on pause.
                // note_flush also forgets the op's voices, so it resyncs clean when play resumes.
                for (const GNodeBind& gb : t.gbinds)
                    if (gb.kind == GNKind::NativeGen && gb.op) {
                        NoteEvent gof[64]; uint32_t gn = 0;
                        vivid::audio_op_note_flush(gb.op, gof, 64, &gn);
                        for (uint32_t i = 0; i < gn && t.nev_clip.size() < kGraphMaxNotes; ++i) t.nev_clip.push_back(gof[i]);
                    }
            }
            else if (playing)   t.sched.emit(beats, delta, frames, t.nev_clip, t.eev);  // paused: emit nothing (tails still ring)
            // P4: this block's clip-level controller automation. Separate from emit() — it needs
            // none of the note bookkeeping — and skipped while paused for the same reason notes are.
            t.cev_clip.clear();
            if (playing) t.sched.emit_cc(beats, delta, frames, t.cev_clip);
            t.nev_live.clear();
            t.cev_live.clear();   // P4: live controllers, refilled from the same queue below
            // Live MIDI monitoring (M6): the armed track drains the session live-input
            // queue into its own event stream so played/typed notes sound through its
            // instrument, whether or not the transport is running. note_id lives in the
            // reserved live range so offs match ons and never collide with clip notes.
            if (t.id == s->armed_track.load(std::memory_order_relaxed)) {
                LiveMidi::Ev le;
                while (s->live_in.pop(le)) {
                    if (le.kind == LiveMidi::kCtrl) {
                        // P4 Phase D: a live controller. Broadcast (a CC is a channel message), at
                        // sample_offset 0 — the same block-start quantization live NOTES already use.
                        t.cev_live.push_back(CcEvent{ 0u, le.pitch, 0, le.vel });
                    } else {
                        t.nev_live.push_back(NoteEvent{ 0u, le.kind == LiveMidi::kNoteOn,
                                                       static_cast<int>(le.pitch), le.vel,
                                                       kLiveNoteIdBase + static_cast<int>(le.pitch), 0.f });
                    }
                }
            }
            // Editor keyboard-audition: this track's own preview queue, sounded whatever
            // the arm state (a distinct note_id range so its offs never hit clip notes).
            { LiveMidi::Ev pe;
              while (t.preview_in.pop(pe))
                  t.nev_live.push_back(NoteEvent{ 0u, pe.kind == LiveMidi::kNoteOn, static_cast<int>(pe.pitch), pe.vel,
                                                 kLiveNoteIdBase + 1000 + pe.pitch, 0.f }); }
            // Rebuild the legacy full stream = clip ++ live, exact same order as before the split
            // (within reserved capacity → no RT allocation).
            t.nev.clear();
            t.nev.insert(t.nev.end(), t.nev_clip.begin(), t.nev_clip.end());
            t.nev.insert(t.nev.end(), t.nev_live.begin(), t.nev_live.end());
            // P4: same shape for controllers. A CC is a CHANNEL message, so this stream is read
            // directly by the render primitives rather than through graph_note_input's key-range
            // filter — every instrument on the track receives it.
            t.cev.clear();
            t.cev.insert(t.cev.end(), t.cev_clip.begin(), t.cev_clip.end());
            t.cev.insert(t.cev.end(), t.cev_live.begin(), t.cev_live.end());
            // Note-derived bridge sources: t.nev is now the authoritative track-wide note union for
            // this block (clip ++ live), assembled once regardless of how many instrument/key-split
            // nodes consume filtered subsets — so scan it here, not the per-node graph_note_input. Take
            // the most-recent note-ON (greatest sample_offset; the two sub-streams aren't globally
            // offset-sorted, so compare rather than take the last element). pitch/vel HOLD across
            // note-less blocks (sustain keeps its colour); gate pulses only on a note-on.
            {
                const NoteEvent* newest = nullptr;
                for (const NoteEvent& e : t.nev)
                    if (e.on && (!newest || e.sample_offset >= newest->sample_offset)) newest = &e;
                if (newest) {
                    t.note_pitch.store(std::clamp(newest->pitch / 127.f, 0.f, 1.f), std::memory_order_relaxed);
                    t.note_vel.store(std::clamp(newest->vel, 0.f, 1.f), std::memory_order_relaxed);
                    t.note_gate.store(1.f, std::memory_order_relaxed);
                } else {
                    t.note_gate.store(0.f, std::memory_order_relaxed);   // pitch/vel left held
                }
                // Polyphonic active-notes: maintain the persistent held set from this block's on/off
                // events (dedup/replace by note_id on on; swap-remove on off). Published for the instancer.
                // Also enqueue each as a DISCRETE event (note_id carried) so one-shot visual ops can fire
                // per note-on — including a re-struck held pitch, which the held set alone can't express.
                for (const NoteEvent& e : t.nev) {
                    if (e.on) t.held.add(e.note_id, e.pitch, e.vel);
                    else      t.held.remove(e.note_id);
                    t.note_events.push(e.on ? 1 : 0, e.pitch, e.vel, e.note_id, e.sample_offset);
                }
            }
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
            // ADR-0022 P2b.1: this track's region of the session node-buffer pool.
            t.blk.node_pool = s->node_pool.data();
            t.blk.node_base = tv_i * static_cast<size_t>(kGraphMaxNodes + 1) * 2 * kGraphMaxBlock;
            // ADR-0022 P2a.2: the cross-track control edges (each node matches those targeting it).
            t.blk.xctl = s->xctl_view.data();
            t.blk.xctl_count = static_cast<uint32_t>(s->xctl_view.size());
            // ADR-0022 P2b.4: the cross-track audio edges (each node matches those targeting it).
            t.blk.xaudio = s->xaudio_view.data();
            t.blk.xaudio_count = static_cast<uint32_t>(s->xaudio_view.size());
            // ADR-0022 P2b.5: the cross-track note edges (each note consumer matches those targeting it).
            t.blk.xnote = s->xnote_view.data();
            t.blk.xnote_count = static_cast<uint32_t>(s->xnote_view.size());
        }
    }

    // ADR-0022 P2b.4/P2b.5: order the render list so a cross-track SOURCE track (audio OR note) renders
    // before its consumer (a stable topological sort by the resolved edges). With no cross-track edges
    // this is the identity (each track keeps its render_list position), so the master sum order is
    // unchanged and the block is bit-identical. Cross-track cycles are rejected at connect time, so the
    // edges here are acyclic; the bounded passes are a belt-and-braces guard that never reorders past
    // render_list.size() swaps. Track regions are keyed by tracks_view index (not this order), so
    // reordering only changes WHEN a track renders, never WHERE.
    if ((!s->xaudio_view.empty() || !s->xnote_view.empty()) && s->render_list.size() > 1) {
        const size_t n = s->render_list.size();
        auto pull_src_before_dst = [&](int src_track_id, int dst_track_id) -> bool {
            int si = -1, di = -1;
            for (size_t i = 0; i < n; ++i) {
                if (s->render_list[i]->id == src_track_id) si = static_cast<int>(i);
                if (s->render_list[i]->id == dst_track_id) di = static_cast<int>(i);
            }
            if (si >= 0 && di >= 0 && si > di) {   // source after consumer → pull source just before it
                Track* src = s->render_list[static_cast<size_t>(si)];
                s->render_list.erase(s->render_list.begin() + si);
                s->render_list.insert(s->render_list.begin() + di, src);
                return true;
            }
            return false;
        };
        for (size_t pass = 0; pass < n; ++pass) {
            bool moved = false;
            for (const XAudioApply& xa : s->xaudio_view) moved |= pull_src_before_dst(xa.src_track_id, xa.dst_track_id);
            for (const XNoteApply&  xn : s->xnote_view)  moved |= pull_src_before_dst(xn.src_track_id, xn.dst_track_id);
            if (!moved) break;
        }
    }
    // ADR-0022 P2b.4: a cross-track AUDIO source that isn't rendering this block (no source / gok=false)
    // would leave a STALE buffer in its node-pool region for the consumer to read. Zero those source
    // buffers so an absent/silent source contributes silence, not last block's audio.
    for (const XAudioApply& xa : s->xaudio_view) {
        bool src_rendering = false;
        for (Track* rt : s->render_list) if (rt->id == xa.src_track_id) { src_rendering = true; break; }
        if (!src_rendering) {
            float* sL = s->node_pool.data() + xa.src_pool_base + static_cast<size_t>(xa.src_out_buf) * 2 * frames;
            std::memset(sL, 0, 2 * frames * sizeof(float));
        }
    }
    // ADR-0022 P2b.5: same for a cross-track NOTE source that isn't rendering — clear its note buffer so
    // the consumer merges nothing (not last block's notes). A rendering source repopulates it (its
    // note-emitter node clears+refills each block), so this only bites an idle/instrument-less source
    // track. (A dedicated note-generator track with no instrument is gok=false and won't render — like a
    // modulator-only track; wiring notes from an instrument-bearing track is the supported path for now.)
    for (const XNoteApply& xn : s->xnote_view) {
        bool src_rendering = false;
        for (Track* rt : s->render_list) if (rt->id == xn.src_track_id) { src_rendering = true; break; }
        if (!src_rendering && xn.src_notes) xn.src_notes->clear();
    }

    // Per-track DSP. Each track is an island (own node-pool region + own track_out slot); master_mix
    // then sums the slots in render_list order, so this is bit-identical regardless of the order tracks
    // are processed in — which is what makes the parallel path below safe. Cross-track audio/note edges
    // (xaudio_view/xnote_view) are the exception: those introduce real inter-track reads, so the
    // parallel path is gated off when they're present (see run_tracks_parallel) and this serial path
    // runs instead. Master_mix sums into `out` (already silent from the memset at the top); the
    // metronome click is mixed in downstream (audio_callback).
    const uint32_t n_tracks = static_cast<uint32_t>(s->render_list.size());
    const bool cross_track = !s->xaudio_view.empty() || !s->xnote_view.empty();
    if (!run_tracks_parallel(s, n_tracks, frames, sample_rate, cross_track)) {
        for (uint32_t slot = 0; slot < n_tracks; ++slot)
            process_one_track(s, slot, frames, sample_rate);
    }
    master_mix(s, out, frames, sample_rate);
    return !s->render_list.empty();
}

static void destroy_handle(Vst3Handle* h) {
    if (!h) return;
    if (h->processing) h->processor->setProcessing(false);
    h->destroy(); delete h;
}
#if defined(__APPLE__)
// Start the track-parallel worker pool (ADR-0052). Called from main() AFTER the audio device exists,
// so the CoreAudio workgroup handle (or null) is available. `os_workgroup` must already be +1 retained
// by the caller; we release it in stop_worker_pool. Reads VIVID_AUDIO_WORKERS once (non-RT thread).
void session_set_audio_workgroup(Session* s, void* os_workgroup) {
    if (!s || !s->audio_workers.empty()) return;   // start once
    const char* env = std::getenv("VIVID_AUDIO_WORKERS");
    s->aw_enabled = !(env && env[0] == '0');
    if (!s->aw_enabled) { if (os_workgroup) os_release(static_cast<os_workgroup_t>(os_workgroup)); return; }
    unsigned hw = std::thread::hardware_concurrency();
    int n = static_cast<int>(hw > 2 ? hw - 1 : 1);       // leave a core for the master (CoreAudio I/O) thread
    if (n > kMaxTracks - 1) n = kMaxTracks - 1;
    if (env && std::isdigit(static_cast<unsigned char>(env[0]))) { int req = std::atoi(env); if (req >= 0) n = std::min(n, req); }
    s->aw_n_workers = n;
    if (n <= 0) { if (os_workgroup) os_release(static_cast<os_workgroup_t>(os_workgroup)); return; }
    s->aw_workgroup = os_workgroup;                      // already +1 retained by the caller
    s->aw_go   = dispatch_semaphore_create(0);
    s->aw_done = dispatch_semaphore_create(0);
    s->aw_running.store(true, std::memory_order_release);
    s->audio_workers.reserve(n);
    for (int i = 0; i < n; ++i) s->audio_workers.emplace_back(audio_worker_main, s);
    std::fprintf(stderr, "[vivid] audio worker pool: %d worker(s), workgroup=%s\n", n, os_workgroup ? "yes" : "no");
}

static void stop_worker_pool(Session* s) {
    if (s->audio_workers.empty()) return;
    s->aw_running.store(false, std::memory_order_release);
    for (size_t i = 0; i < s->audio_workers.size(); ++i) dispatch_semaphore_signal(s->aw_go);   // wake to exit
    for (auto& th : s->audio_workers) if (th.joinable()) th.join();
    s->audio_workers.clear();
    if (s->aw_go)        { dispatch_release(s->aw_go);   s->aw_go = nullptr; }
    if (s->aw_done)      { dispatch_release(s->aw_done); s->aw_done = nullptr; }
    if (s->aw_workgroup) { os_release(static_cast<os_workgroup_t>(s->aw_workgroup)); s->aw_workgroup = nullptr; }
}
#else
void session_set_audio_workgroup(Session*, void*) {}
static void stop_worker_pool(Session*) {}
#endif

void session_destroy(Session* s) {
    if (!s) return;
    stop_worker_pool(s);   // ADR-0052: join the audio workers (callback already stopped) before teardown
    stop_clap_loader(s);   // join the async loader + free unapplied handles before the tracks go
    auto teardown = [](Track* t) {
        for (Vst3Handle* fx : t->effects_edit) destroy_handle(fx);  // authoritative FX list
        for (Vst3Handle* fx : t->fx_retired)   destroy_handle(fx);  // removed-but-not-freed
        destroy_handle(t->handle);
        for (vivid::AudioOp* op : t->op_effects_edit) vivid::audio_op_destroy(op);   // native audio ops
        for (vivid::AudioOp* op : t->op_sources_edit) vivid::audio_op_destroy(op);   // extra graph sources
        for (vivid::AudioOp* op : t->op_retired)      vivid::audio_op_destroy(op);
        for (auto& gc : t->gen_cells) vivid::audio_op_destroy(gc.op);   // per-scene generator ops (owned here)
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

// The ASYNC CLAP LOADER (clap_worker_main / enqueue_clap_load / session_request_track_clap_* /
// session_poll_plugin_loads / stop_clap_loader) lives in vst3_host_clap_loader.cpp (ADR-0025 split).
// It calls back into rebuild_track_graph() (shared via vst3_host_internal.h) when a plugin binds.

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
uint32_t session_audio_op_role(Session* s, const char* name) {
    // Stays behind the opaque audio-op runtime (no operator_api leak into vst3_host); returns
    // VIVID_OP_ROLE_DEFAULT (0) for a null/unknown name or a non-native op.
    return (s && s->op_reg && name) ? vivid::audio_op_role(*s->op_reg, name) : 0u;
}

// AG-1 graph introspection. All read `t->agraph`/`t->agnodes` under the track's graph lock and
// bounds-check every index (safe defaults on miss). `agnodes` is parallel to `agraph.nodes()`
// (same index), so a node index maps to both its topology entry and its binding.
Track* graph_track(Session* s, int t) {   // ADR-0025: external (declared in vst3_host_internal.h) so the extracted param TU can resolve a track
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
// ADR-0022 P4.4: restore a saved session-global id onto a node (by local id) on LOAD, and keep the
// session's next_gnid past it so future assignments don't collide. Set before finish_load's republish
// so assign_node_gnids (which only fills gnid<0) leaves the restored id in place. UI/main thread.
void session_set_node_gnid(Session* s, int t, int node_id, int gnid) {
    Track* tr = graph_track(s, t);
    if (!tr || gnid < 0) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const auto& n = tr->agraph.nodes();
    for (size_t i = 0; i < tr->agnodes.size() && i < n.size(); ++i)
        if (n[i].id == node_id) { tr->agnodes[i].gnid = gnid; break; }
    if (gnid >= s->next_gnid) s->next_gnid = gnid + 1;
}
// ADR-0022 P2b.3c: node i's SESSION-GLOBAL id (-1 if unassigned — a derived-chain track's nodes, which
// aren't cross-addressable). agnodes is parallel to nodes() by index (same lock as node_kind reads it).
int session_track_audio_graph_node_gnid(Session* s, int t, int i) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return (i >= 0 && i < static_cast<int>(tr->agnodes.size())) ? tr->agnodes[i].gnid : -1;
}
// ADR-0022 P3.3/P4: the scene a per-scene note node (MidiClip / NativeGen) represents, or -1 for any
// other node. Persist saves it so an authoritative note sub-graph round-trips (the node kinds the
// loader recreates need to know which scene they gate).
int session_track_audio_graph_node_cell_scene(Session* s, int t, int i) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return (i >= 0 && i < static_cast<int>(tr->agnodes.size())) ? tr->agnodes[i].scene : -1;
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
        case GNKind::MidiIn:       return 3;   // ADR-0015: the track's live/preview note stream as a node
        case GNKind::NativeNoteFx: return 4;   // ADR-0015: a note effect (Arp) — notes in, notes out
        case GNKind::NativeMod:    return 5;   // ADR-0022: a modulator (LFO) — no audio, emits control
        case GNKind::MidiClip:     return 6;   // ADR-0022 P3.1b: the clip scheduler as a note source
        case GNKind::Selector:     return 7;   // ADR-0022 P3.2: the per-track-out note selector/mux
        case GNKind::NativeGen:    return 8;   // ADR-0022 P3.3: a note generator in a scene cell
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
    switch (nb.kind) {                                   // VST3/sampler/internal nodes have no AudioOp
        case GNKind::Vst3Inst: case GNKind::Vst3Fx:      // VST3: the plugin's own name, not just "VST3"
            return (nb.handle && !nb.handle->plugin_name.empty()) ? nb.handle->plugin_name.c_str() : "VST3";
        case GNKind::Sampler:  return "Sampler";
        // The derived note-path nodes every instrument track carries — label them instead of "?".
        case GNKind::MidiIn:   return "MIDI In";
        case GNKind::Selector: return "Notes";
        case GNKind::MidiClip: return "Clip";
        case GNKind::Output:   return "Output";
        default:               return "";
    }
}
// Persistence discriminator for a node's binding family (the UI-facing node_type returns the
// plugin's display name, which the loader can't map back to a plugin family). Stable codes:
// 0 = native op (createable via audio_op_create) or Output, 1 = VST3, 2 = CLAP, 3 = AudioClip.
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
// unavailable). Display-only: the audio thread writes the atomic-slot ring lock-free, so a concurrent read
// is a benign 1-pixel blip (well-defined — ADR-0029). node index i == the compiled out_buf.
int session_track_audio_graph_node_scope(Session* s, int t, int i, float* out, int n) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    return tr->node_scope.snapshot(i, out, n);
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

// The CLAP plugin handle (ClapHandle*) behind a graph node (ClapInst / ClapFx), so the audio graph
// can open the plugin's native clap.gui editor. Null for VST3 / native / sampler / output nodes.
void* session_audio_graph_node_clap(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t);
    if (!tr) return nullptr;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return nullptr;
    const GNodeBind& nb = tr->agnodes[idx];
    return (nb.clap && (nb.kind == GNKind::ClapInst || nb.kind == GNKind::ClapFx)) ? nb.clap : nullptr;
}

int session_track_audio_graph_output_id(Session* s, int t) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.output_id();
}
// ADR-0022 P2b.3c: this track's TRACK-OUT node's session-global id — the is_track_out sink of the
// track, the per-track complement of the master's is_master gnid. -1 if the track has no output node
// (a non-derivable / empty track) or the node is unassigned (a derived-chain track). This is how a
// track's sink is named in the one global node-id space.
int session_track_out_gnid(Session* s, int t) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int oid = tr->agraph.output_id();
    if (oid < 0) return -1;
    const int idx = tr->agraph.node_index(oid);
    return (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) ? tr->agnodes[idx].gnid : -1;
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

// Load an audio file directly into an existing AudioClip node's PCM (the thing the op has no file param
// for — audio nodes can't carry file paths). Decodes any miniaudio format (WAV/AIFF/MP3/FLAC/OGG) at
// its native rate off the audio thread, then injects it through the SamplerLoadable escape hatch with
// no slices → one keyboard-spanning region (a melodic instrument). SamplerOp swaps its sample bank
// ATOMICALLY, so this is safe on a LIVE, published node: no topology change, no graph rebuild, no lost
// params — the audio thread just picks up the new bank on its next block. Returns frames loaded, or 0
// (node not found / not a AudioClip / decode failed).
int session_audio_graph_load_sampler(Session* s, int t, int node_id, const char* path, int base_note) {
    Track* tr = graph_track(s, t);
    if (!tr || !path || !*path) return 0;
    vivid::AudioOp* op = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);          // resolve the node's op (topology guard)
      const int idx = tr->agraph.node_index(node_id);
      if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) op = tr->agnodes[idx].op; }
    if (!op || std::strcmp(vivid::audio_op_type(op), "Sampler") != 0) return 0;   // must be a AudioClip node
    auto data = vivid::sample_engine::decode_audio_native(path);                  // native-rate decode (off RT)
    if (!data || data->samples_L.empty()) return 0;
    const float* L = data->samples_L.data();
    const float* R = data->stereo ? data->samples_R.data() : nullptr;
    const uint32_t n = static_cast<uint32_t>(data->samples_L.size());
    if (!vivid::audio_op_load_sampler(op, L, R, n, data->sample_rate, nullptr, nullptr, 0, base_note))
        return 0;   // atomic bank swap inside the op — live-safe, no republish
    vivid::audio_op_set_sampler_source(op, path);   // ADR-0049: retain the source path for the editor identity
    return static_cast<int>(n);
}

// ADR-0049: the Sampler editor's read side, resolved through a node id. sample_info returns 1 if a
// sample is loaded (fills geometry + slice count + playback mode); slices fills up to `cap` and returns
// the true count; source returns the loaded path ("" if none). All UI/main-thread, like the peaks read.
int session_sampler_info(Session* s, int t, int node_id, ::vivid::SamplerInfo* out) {
    Track* tr = graph_track(s, t);
    if (!tr || !out) return 0;
    vivid::AudioOp* op = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);
      const int idx = tr->agraph.node_index(node_id);
      if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) op = tr->agnodes[idx].op; }
    return (op && vivid::audio_op_sampler_info(op, *out)) ? 1 : 0;
}
int session_sampler_slices(Session* s, int t, int node_id, ::vivid::SamplerSlice* out, int cap) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    vivid::AudioOp* op = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);
      const int idx = tr->agraph.node_index(node_id);
      if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) op = tr->agnodes[idx].op; }
    return op ? vivid::audio_op_sampler_slices(op, out, cap) : 0;
}
const char* session_sampler_source(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t);
    if (!tr) return "";
    vivid::AudioOp* op = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);
      const int idx = tr->agraph.node_index(node_id);
      if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) op = tr->agnodes[idx].op; }
    return op ? vivid::audio_op_sampler_source(op) : "";
}
// ADR-0049 slice 6: edit the played window / slice map. These MUTATE the op's bank (atomic publish +
// retained old banks), so they hold gmtx across the call to serialize with a concurrent load.
unsigned long long session_sampler_source_frames(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    vivid::AudioOp* op = (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) ? tr->agnodes[idx].op : nullptr;
    return op ? vivid::audio_op_sampler_source_frames(op) : 0;
}
void session_sampler_set_trim(Session* s, int t, int node_id, unsigned int in, unsigned int out) {
    Track* tr = graph_track(s, t);
    if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size()) && tr->agnodes[idx].op)
        vivid::audio_op_sampler_set_trim(tr->agnodes[idx].op, in, out);
}
void session_sampler_reslice(Session* s, int t, int node_id, const unsigned int* starts,
                             const unsigned int* ends, int n, int base) {
    Track* tr = graph_track(s, t);
    if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size()) && tr->agnodes[idx].op)
        vivid::audio_op_sampler_reslice(tr->agnodes[idx].op, starts, ends, n, base);
}
int session_sampler_source_peaks(Session* s, int t, int node_id, float* out, int n) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    vivid::AudioOp* op = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);
      const int idx = tr->agraph.node_index(node_id);
      if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) op = tr->agnodes[idx].op; }
    return op ? vivid::audio_op_sampler_source_peaks(op, out, n) : 0;
}
int session_sampler_edit_boundaries(Session* s, int t, int node_id, unsigned int* starts, unsigned int* ends, int cap) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    vivid::AudioOp* op = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);
      const int idx = tr->agraph.node_index(node_id);
      if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) op = tr->agnodes[idx].op; }
    return op ? vivid::audio_op_sampler_edit_boundaries(op, starts, ends, cap) : 0;
}
int session_sampler_detect_slices(Session* s, int t, int node_id, float sensitivity) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    vivid::AudioOp* op = (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) ? tr->agnodes[idx].op : nullptr;
    return op ? vivid::audio_op_sampler_detect_slices(op, sensitivity) : 0;
}
void session_sampler_set_slice_tune(Session* s, int t, int node_id, int slice, int semitones) {
    Track* tr = graph_track(s, t);
    if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const int idx = tr->agraph.node_index(node_id);
    if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size()) && tr->agnodes[idx].op)
        vivid::audio_op_sampler_set_slice_tune(tr->agnodes[idx].op, slice, semitones);
}

// Copy a Sampler node's loaded-sample peak envelope for its waveform thumbnail (the UI node card).
// Resolves the node's op under gmtx, then reads the cached peaks via the SamplerPreviewable escape
// hatch. Returns bins written, 0 for a non-Sampler node or an empty sampler.
int session_audio_graph_node_sampler_peaks(Session* s, int t, int node_id, float* out, int n) {
    Track* tr = graph_track(s, t);
    if (!tr || !out || n <= 0) return 0;
    vivid::AudioOp* op = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);
      const int idx = tr->agraph.node_index(node_id);
      if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) op = tr->agnodes[idx].op; }
    return op ? vivid::audio_op_sampler_peaks(op, out, n) : 0;
}

// The Sampler node's playhead position (0..1) for its animated waveform thumbnail, or -1 if silent.
float session_audio_graph_node_sampler_playhead(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t);
    if (!tr) return -1.f;
    vivid::AudioOp* op = nullptr;
    { std::lock_guard<std::mutex> lk(tr->gmtx);
      const int idx = tr->agraph.node_index(node_id);
      if (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) op = tr->agnodes[idx].op; }
    return op ? vivid::audio_op_sampler_playhead(op) : -1.f;
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

// ADR-0033 Phase 3: bypass a node (route signal around it) or restore it. Mutates the authoritative
// graph and republishes, so the recompiled plan carries the new CompiledStep::bypassed to the audio
// thread (same gen-counter + try_lock handoff every topology edit uses). Idempotent; returns 1 for a
// valid node (state set), 0 for a bad track/node.
int session_audio_graph_set_node_bypass(Session* s, int t, int node_id, int on) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (tr->agraph.node_index(node_id) < 0) return 0;
    if (tr->agraph.node_bypassed(node_id) == (on != 0)) return 1;   // no change, still a valid node
    // Bypass changes the compiled plan, so — like every other graph-node edit (add_op / remove_node /
    // connect) — the graph becomes the authoritative source of truth. This also makes the flag durable:
    // a later derived-chain rebuild can't silently drop it. Then republish so the recompile carries it.
    tr->agraph.set_node_bypass(node_id, on != 0);
    tr->graph_authoritative = true;
    republish_track_graph(tr);
    return 1;
}
int session_audio_graph_node_bypassed(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.node_bypassed(node_id) ? 1 : 0;
}
// ADR-0033 P4: solo / un-solo a node (audition its signal path — ancestors + itself + descendants —
// muting sibling branches). Performance state: it recomputes the audible mask but does NOT touch the
// compiled plan (no recompile) and is never persisted or undone. Idempotent; returns 1 for a valid
// node, 0 for a bad track/node.
int session_audio_graph_set_node_solo(Session* s, int t, int node_id, int on) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (tr->agraph.node_index(node_id) < 0) return 0;
    auto& ids = tr->soloed_node_ids;
    const auto it = std::find(ids.begin(), ids.end(), node_id);
    if (on) { if (it == ids.end()) ids.push_back(node_id); }
    else    { if (it != ids.end()) ids.erase(it); }
    recompute_node_audible(tr);
    return 1;
}
int session_audio_graph_node_soloed(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const auto& ids = tr->soloed_node_ids;
    return std::find(ids.begin(), ids.end(), node_id) != ids.end() ? 1 : 0;
}

// Add an edge from_id -> to_id. Rejected (returns 0) on a bad/duplicate/self edge, or if the
// edge would create a cycle (the graph is reverted and the last good plan keeps playing).
int session_audio_graph_connect_kind(Session* s, int t, int from_id, int to_id, int kind) {
    Track* tr = graph_track(s, t);
    if (!tr) return 0;
    const auto ek = (kind == 1) ? vivid::audio::EdgeKind::Note : vivid::audio::EdgeKind::Audio;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    // ADR-0047: typed validation — a note edge is only valid from a note EMITTER to a note CONSUMER
    // (the audio graph's connect() is capability-blind; this mirrors the cross-track session_connect_note
    // check). Interactive path only; the persistence/load path replays edges separately and is untouched.
    if (ek == vivid::audio::EdgeKind::Note) {
        const int fi = tr->agraph.node_index(from_id), ti = tr->agraph.node_index(to_id);
        const auto& ns = tr->agraph.nodes();
        if (fi < 0 || fi >= static_cast<int>(ns.size()) || !ns[static_cast<size_t>(fi)].note_out) return 0;  // src must emit notes
        if (ti < 0 || ti >= static_cast<int>(ns.size()) || !ns[static_cast<size_t>(ti)].note_in)  return 0;  // dst must consume notes
    }
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

// ==== ADR-0022 P4.2: the session-global (gnid) node API ====================================
// A parallel "session_graph_*" surface addressing a node by its session-global id (gnid) instead
// of (track, local node id). resolve_gnid finds the owning track + node position; each shim
// delegates to the existing per-track function, so behaviour is identical — this is the C-API the
// MCP + persist surfaces migrate onto (P4.3/P4.4), collapsing the (track,node) addressing.
static bool resolve_gnid(Session* s, int gnid, int* out_track, int* out_pos) {
    if (!s || gnid < 0) return false;
    for (size_t t = 0; t < s->tracks.size(); ++t) {
        const std::vector<GNodeBind>& ag = s->tracks[t]->agnodes;
        for (size_t i = 0; i < ag.size(); ++i)
            if (ag[i].gnid == gnid) { if (out_track) *out_track = static_cast<int>(t); if (out_pos) *out_pos = static_cast<int>(i); return true; }
    }
    return false;
}
static int gnid_node_id(Session* s, int track, int pos) { return session_track_audio_graph_node_id(s, track, pos); }

int         session_graph_node_track(Session* s, int gnid) { int t=-1; return resolve_gnid(s, gnid, &t, nullptr) ? t : -1; }
int         session_graph_node_kind(Session* s, int gnid)  { int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_track_audio_graph_node_kind(s, t, p) : -1; }
const char* session_graph_node_type(Session* s, int gnid)  { int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_track_audio_graph_node_type(s, t, p) : ""; }
int         session_graph_node_param_count(Session* s, int gnid) { int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_audio_graph_node_param_count(s, t, gnid_node_id(s,t,p)) : 0; }
const char* session_graph_node_param_name(Session* s, int gnid, int i) { int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_audio_graph_node_param_name(s, t, gnid_node_id(s,t,p), i) : ""; }
float       session_graph_node_param_get(Session* s, int gnid, int i) { int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_audio_graph_node_param_get(s, t, gnid_node_id(s,t,p), i) : 0.f; }
void        session_graph_node_param_set(Session* s, int gnid, int i, float v) { int t,p; if (resolve_gnid(s, gnid, &t, &p)) session_audio_graph_node_param_set(s, t, gnid_node_id(s,t,p), i, v); }
int         session_graph_remove_node(Session* s, int gnid) { int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_audio_graph_remove_node(s, t, gnid_node_id(s,t,p)) : 0; }
int         session_graph_node_set_bypass(Session* s, int gnid, int on) { int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_audio_graph_set_node_bypass(s, t, gnid_node_id(s,t,p), on) : 0; }
int         session_graph_node_bypassed(Session* s, int gnid) { int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_audio_graph_node_bypassed(s, t, gnid_node_id(s,t,p)) : 0; }
int         session_graph_node_set_solo(Session* s, int gnid, int on) { int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_audio_graph_set_node_solo(s, t, gnid_node_id(s,t,p), on) : 0; }
int         session_graph_node_soloed(Session* s, int gnid) { int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_audio_graph_node_soloed(s, t, gnid_node_id(s,t,p)) : 0; }

// Unified connect/disconnect: intra-track OR cross-track, chosen by whether the endpoints share a
// track — the payoff of session-global addressing (one call spans the whole session). kind: 0 audio,
// 1 note. (Control edges take a dest_param, so they keep their own entry point.)
int session_graph_connect(Session* s, int from_gnid, int to_gnid, int kind) {
    int ta,pa,tb,pb;
    if (!resolve_gnid(s, from_gnid, &ta, &pa) || !resolve_gnid(s, to_gnid, &tb, &pb)) return 0;
    const int fa = gnid_node_id(s, ta, pa), fb = gnid_node_id(s, tb, pb);
    if (ta == tb) return session_audio_graph_connect_kind(s, ta, fa, fb, kind);
    return (kind == 1) ? session_connect_note(s, ta, fa, tb, fb) : session_connect_audio(s, ta, fa, tb, fb);
}
int session_graph_disconnect(Session* s, int from_gnid, int to_gnid, int kind) {
    int ta,pa,tb,pb;
    if (!resolve_gnid(s, from_gnid, &ta, &pa) || !resolve_gnid(s, to_gnid, &tb, &pb)) return 0;
    const int fa = gnid_node_id(s, ta, pa), fb = gnid_node_id(s, tb, pb);
    if (ta == tb) { session_audio_graph_disconnect(s, ta, fa, fb); return 1; }
    if (kind == 1) session_disconnect_note(s, ta, fa, tb, fb); else session_disconnect_audio(s, ta, fa, tb, fb);
    return 1;
}

// ADR-0022 P4.3: CONTROL (modulation) edges by gnid — the modulation analog of session_graph_connect,
// unified across intra-track and cross-track. dest_param is the target op's param index; shape =
// amount/curve/invert/bipolar (the ControlShape fields).
int session_graph_connect_control(Session* s, int from_gnid, int to_gnid, int dest_param,
                                  float amount, float curve, int invert, int bipolar) {
    int ta,pa,tb,pb;
    if (!resolve_gnid(s, from_gnid, &ta, &pa) || !resolve_gnid(s, to_gnid, &tb, &pb)) return 0;
    const int fa = gnid_node_id(s, ta, pa), fb = gnid_node_id(s, tb, pb);
    return (ta == tb) ? session_audio_graph_connect_control(s, ta, fa, fb, dest_param, amount, curve, invert, bipolar)
                      : session_connect_control(s, ta, fa, tb, fb, dest_param, amount, curve, invert, bipolar);
}
int session_graph_disconnect_control(Session* s, int from_gnid, int to_gnid, int dest_param) {
    int ta,pa,tb,pb;
    if (!resolve_gnid(s, from_gnid, &ta, &pa) || !resolve_gnid(s, to_gnid, &tb, &pb)) return 0;
    const int fa = gnid_node_id(s, ta, pa), fb = gnid_node_id(s, tb, pb);
    if (ta == tb) return session_audio_graph_disconnect_control(s, ta, fa, fb, dest_param);
    session_disconnect_control(s, ta, fa, tb, fb, dest_param); return 1;
}
int session_graph_set_control_shape(Session* s, int from_gnid, int to_gnid, int dest_param,
                                    float amount, float curve, int invert, int bipolar) {
    int ta,pa,tb,pb;
    if (!resolve_gnid(s, from_gnid, &ta, &pa) || !resolve_gnid(s, to_gnid, &tb, &pb)) return 0;
    const int fa = gnid_node_id(s, ta, pa), fb = gnid_node_id(s, tb, pb);
    return (ta == tb) ? session_audio_graph_set_control_shape(s, ta, fa, fb, dest_param, amount, curve, invert, bipolar)
                      : (session_set_control_shape(s, ta, fa, tb, fb, dest_param, amount, curve, invert, bipolar) ? 1 : 0);
}

// ADR-0022 P4.3: key-split range on a source node, by gnid.
void session_graph_node_key_range_set(Session* s, int gnid, int lo, int hi) {
    int t,p; if (resolve_gnid(s, gnid, &t, &p)) session_audio_graph_node_key_range_set(s, t, gnid_node_id(s,t,p), lo, hi);
}
int session_graph_node_key_range_get(Session* s, int gnid, int* lo, int* hi) {
    int t,p; return resolve_gnid(s, gnid, &t, &p) ? session_audio_graph_node_key_range_get(s, t, gnid_node_id(s,t,p), lo, hi) : 0;
}

// ADR-0015: add a native NOTE EFFECT (Arp / chord / transpose) as a node. It is wired with NOTE
// edges only — it makes no sound, so it gets no audio wiring at all (an audio edge to Output would
// just add silence). Returns the new node id, or -1 (unknown op / cap / no track).
int session_audio_graph_add_note_op(Session* s, int t, const char* op_type) {
    Track* tr = graph_track(s, t);
    if (!tr || !s->op_reg) return -1;
    // ADR-0047: only a registered NOTE EFFECT (Arp; a dylib with NOTE_EFFECT role). Mirrors add_mod_op —
    // without this any op could be added here and marked note-in/note-out, minting a node whose declared
    // ports lie about what it does. Gate before creating the op so a wrong type never allocates.
    if (!op_type || !vivid::audio_op_is_note_op(*s->op_reg, op_type)) return -1;
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
    if (!op_type || !vivid::audio_op_is_mod_op(*s->op_reg, op_type)) return -1;   // only a registered modulator (built-in mark OR dylib role)
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
    // ADR-0034: capture-on-wire. A modulator wired onto a plugin param that has no authored base yet
    // captures the plugin's current value as the base, so the audio thread has a stable anchor to
    // resolve modulation against (and disconnect has a value to restore). Main thread — reading plugin
    // state here is safe.
    if (const int nidx = tr->agraph.node_index(to_id);
        nidx >= 0 && nidx < static_cast<int>(tr->agnodes.size()) && dest_param >= 0) {
        const GNodeBind& nb = tr->agnodes[static_cast<size_t>(nidx)];
        if (nb.clap && dest_param < static_cast<int>(nb.clap->params.size()) &&
            (dest_param >= static_cast<int>(nb.clap->has_base.size()) || !nb.clap->has_base[dest_param]))
            nb.clap->base_author(dest_param, clap_param_value(nb.clap, nb.clap->params[dest_param].id));
        else if (nb.handle && nb.handle->controller && dest_param < static_cast<int>(nb.handle->params.size()) &&
                 (dest_param >= static_cast<int>(nb.handle->has_base.size()) || !nb.handle->has_base[dest_param]))
            nb.handle->base_author(dest_param,
                static_cast<float>(nb.handle->controller->getParamNormalized(nb.handle->params[dest_param].id)));
    }
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
    // ADR-0034 Phase 3: restore-on-disconnect. Once the LAST control edge on a plugin param is gone,
    // the plugin holds its last modulated value (VST3/CLAP params latch — no injected event returns
    // them to base). Deliver the authored base once so it snaps back. Native ops need nothing: with no
    // control edge the op reads `pvals` (its base) directly. Only when no edge still targets the param.
    bool still_driven = false;
    for (const vivid::audio::AudioGraphEdge& e : tr->agraph.edges())
        if (e.kind == vivid::audio::EdgeKind::Control && e.to_id == to_id && e.dest_param == dest_param) { still_driven = true; break; }
    if (!still_driven && dest_param >= 0) {
        const int nidx = tr->agraph.node_index(to_id);
        if (nidx >= 0 && nidx < static_cast<int>(tr->agnodes.size())) {
            const GNodeBind& nb = tr->agnodes[static_cast<size_t>(nidx)];
            if (nb.clap && dest_param < static_cast<int>(nb.clap->params.size()) &&
                dest_param < static_cast<int>(nb.clap->has_base.size()) && nb.clap->has_base[dest_param])
                nb.clap->param_q.push(nb.clap->params[dest_param].id, nb.clap->host_base[dest_param]);
            else if (nb.handle && nb.handle->controller && dest_param < static_cast<int>(nb.handle->params.size()) &&
                     dest_param < static_cast<int>(nb.handle->has_base.size()) && nb.handle->has_base[dest_param]) {
                const ParamID id = nb.handle->params[dest_param].id;
                nb.handle->param_q.push(id, nb.handle->host_base[dest_param]);
                nb.handle->controller->setParamNormalized(id, nb.handle->host_base[dest_param]);
            }
        }
    }
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

// The node-id-keyed PARAM API (session_audio_graph_node_param_* + curated-inspector metadata) and its
// graph-node / param-base helpers live in vst3_host_params.cpp (ADR-0025 split); they reach a track
// via graph_track(), now declared in vst3_host_internal.h.

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

// ADR-0033 P5: per-track graph sticky notes. UI-thread authoring state on the track's AudioGraph;
// never touched by the audio thread (notes aren't nodes), so mutating one needs no republish.
int session_audio_graph_annotation_add(Session* s, int t, float x, float y) {
    Track* tr = graph_track(s, t); if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.add_annotation(x, y);
}
void session_audio_graph_annotation_add_raw(Session* s, int t, int id, const char* text,
                                            float x, float y, float w, float h) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.add_annotation_raw(id, text ? text : "", x, y, w, h);
}
int session_audio_graph_annotation_remove(Session* s, int t, int id) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.remove_annotation(id) ? 1 : 0;
}
int session_audio_graph_annotation_set_text(Session* s, int t, int id, const char* text) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.set_annotation_text(id, text ? text : "") ? 1 : 0;
}
int session_audio_graph_annotation_move(Session* s, int t, int id, float x, float y) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.move_annotation(id, x, y) ? 1 : 0;
}
int session_audio_graph_annotation_count(Session* s, int t) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.annotation_count();
}
int session_audio_graph_annotation_at(Session* s, int t, int i, int* id,
                                      float* x, float* y, float* w, float* h) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    int aid = 0; std::string txt; float ax = 0.f, ay = 0.f, aw = 0.f, ah = 0.f;
    if (!tr->agraph.get_annotation(i, aid, txt, ax, ay, aw, ah)) return 0;
    if (id) *id = aid; if (x) *x = ax; if (y) *y = ay; if (w) *w = aw; if (h) *h = ah;
    return 1;
}
const char* session_audio_graph_annotation_text(Session* s, int t, int id) {
    Track* tr = graph_track(s, t); if (!tr) return nullptr;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.annotation_text(id);
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
int session_audio_graph_load_node(Session* s, int t, int kind, int plugin_kind, const char* op_type, int scene) {
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
    if (kind == 7) {   // ADR-0022 P3.2: the per-track-out Selector (note in -> note out; no op/scene)
        const int nid_se = tr->agraph.add_node(true, false, nullptr, nullptr, "sel");
        tr->agraph.set_note_ports(nid_se, true, true);
        GNodeBind nbs; nbs.kind = GNKind::Selector;
        tr->agnodes.push_back(nbs);
        return nid_se;
    }
    if (kind == 6) {   // ADR-0022 P3.1b: a per-scene MidiClip node (emits the scene's clip stream)
        const int nid_mc = tr->agraph.add_node(true, false, nullptr, nullptr, "clip");
        tr->agraph.set_note_ports(nid_mc, false, true);
        GNodeBind nbc; nbc.kind = GNKind::MidiClip; nbc.scene = scene;
        tr->agnodes.push_back(nbc);
        return nid_mc;
    }
    if (kind == 8) {   // ADR-0022 P3.3: a per-scene NativeGen node (Euclid/Chord/RandMelody in a cell)
        vivid::AudioOp* gop = vivid::audio_op_create(*s->op_reg, op_type);
        if (!gop) return -1;
        // Own the op in gen_cells (parity with place_generator) — NOT op_retired — so gen_cells is the
        // single source of truth for "which scenes hold a generator". reconcile_note_subgraph keys the
        // scene node's kind off gen_cells; leaving this empty would make it flip the loaded generator
        // back to a plain clip on the first republish and orphan the op.
        if (scene >= static_cast<int>(tr->gen_cells.size())) tr->gen_cells.resize(scene + 1);
        if (tr->gen_cells[scene].op) tr->op_retired.push_back(tr->gen_cells[scene].op);   // retire any prior
        tr->gen_cells[scene].op   = gop;
        tr->gen_cells[scene].type = op_type ? op_type : "";
        const int nid_gn = tr->agraph.add_node(true, false, nullptr, nullptr, "gen");
        tr->agraph.set_note_ports(nid_gn, false, true);
        GNodeBind nbg; nbg.kind = GNKind::NativeGen; nbg.op = gop; nbg.scene = scene;
        tr->agnodes.push_back(nbg);
        return nid_gn;
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
        // Plugin source/effect (VST3/CLAP) or the audio-loop AudioClip: no native op. Create the
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

// A6: slice an audio clip into a new MIDI track driven by a native AudioClip. Computes the
// clip's slices (`slice_mode`: 1=transients, 3=16-grid), creates a paired instrument track
// whose native instrument is a AudioClip loaded with the clip's PCM + those slices, and writes
// a MIDI clip mapping ascending pitches (base C1=36) → slices at their beat positions. The
// AudioClip is loaded BEFORE it is published to the audio thread (no RT race). Returns the new
// track index, or -1. Runs on the UI thread (control-server drain / editor req).
int session_slice_to_midi(Session* s, int src_track, int src_scene, int slice_mode) {
    if (!aud_valid(s, src_track, src_scene) || !s->op_reg) return -1;
    if (static_cast<int>(s->tracks.size()) >= kMaxTracks) return -1;

    // 1. Snapshot the source clip's PCM + slice regions (UI-thread copy under aud_mtx).
    std::vector<float> L, R;
    std::vector<uint32_t> ss, se;
    double loop_beats = 4.0; uint32_t sr = 0, N = 0;
    std::string src_path;   // the source clip's WAV path — retained on the sampler so it persists
    {
        std::lock_guard<std::mutex> lk(s->tracks[src_track]->aud_mtx);
        const AudioClip& c = s->tracks[src_track]->aud_clips[src_scene];
        if (c.L.empty()) return -1;
        L = c.L; R = c.R; N = static_cast<uint32_t>(c.L.size()); sr = c.sr;
        src_path = c.src_path;   // snapshot the WAV path under the same lock as the PCM/slices
        loop_beats = c.loop_beats > 0 ? c.loop_beats : 4.0;
        const int m = (slice_mode == 3) ? 3 : 1;   // transients unless 16-grid asked
        for (const auto& rg : audio_clip_ed::compile_slices(m, c.transients, {}, 0, N)) {
            ss.push_back(rg.start); se.push_back(rg.end);
        }
    }
    if (ss.empty()) return -1;
    const int base_note = 36;   // C1 → slice 0

    // 2. Create the paired instrument (MIDI) track — no VST3, empty clips.
    Track* nt = make_instrument_track(nullptr, s->tracks[src_track]->name + " Slices", s->scenes, s->sample_rate);
    s->tracks.emplace_back(nt);
    s->tracks.back()->id = s->next_track_id++;
    const int new_track = static_cast<int>(s->tracks.size()) - 1;
    Track& tr = *s->tracks[new_track];

    // 3. Set its native instrument to a AudioClip + inject PCM/slices, then publish it.
    if (vivid::AudioOp* op = vivid::audio_op_create(*s->op_reg, "Sampler")) {
        if (vivid::audio_op_is_source(op) &&
            vivid::audio_op_load_sampler(op, L.data(), R.empty() ? nullptr : R.data(), N, sr,
                                         ss.data(), se.data(), static_cast<int>(ss.size()), base_note)) {
            // Retain the WAV path so the sliced sampler PERSISTS: sampler_save_block writes it, and on
            // reload sampler_restore reloads the SAME raw wav + re-applies the saved slice boundaries.
            // AudioClip.L is the raw imported PCM (warp is a playback-time transform, never baked in),
            // so reslicing the reloaded wav with those boundaries is exact regardless of warp/tempo.
            // Empty path (a generated source clip) degrades to today's behaviour — no persistence.
            vivid::audio_op_set_sampler_source(op, src_path.c_str());
            std::lock_guard<std::mutex> lk(tr.op_fx_mtx);
            tr.op_instrument_edit = op;
        } else {
            vivid::audio_op_destroy(op);
        }
    }
    tr.op_fx_gen.fetch_add(1, std::memory_order_release);
    // 4. Write the MIDI clip: one ascending-pitch note per slice at its beat position. Do this BEFORE
    //    building the graph so reconcile_note_subgraph sees scene `src_scene` as populated and gives it
    //    a Clip node (an empty slot gets none).
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

    rebuild_track_graph(&tr);   // AG-0: recompile the audio graph from the new native chain (with the clip)
    rebuild_track_view(s);      // publish the fully-formed track to the audio thread
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
    s->tracks.emplace_back(make_instrument_track(h, name, s->scenes, s->sample_rate));
    s->tracks.back()->id = s->next_track_id++;
    rebuild_track_graph(s->tracks.back().get());   // derive the source->Output plan NOW (else gok=false → silent)
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
    configure_track_capture(at.get(), s->sample_rate);
    s->tracks.emplace_back(std::move(at));
    s->tracks.back()->id = s->next_track_id++;
    rebuild_track_graph(s->tracks.back().get());   // derive the AudioClip->Output plan NOW (else gok=false → silent):
    rebuild_track_view(s);                          // parity with session_create's line-951 up-front derive.
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
            t->gen_cells.push_back({});       // ADR-0022 P3.3: the new scene's cell is a clip by default
            t->edit_gen.fetch_add(1, std::memory_order_release);
        }
    }
    s->scenes = ns;
    // The new scene is an EMPTY slot — it gets no Clip node in any track's audio graph (empty slots
    // aren't shown). Its node materializes later, when the user puts a clip or generator in it
    // (session_set_clip / place_generator both republish → reconcile_note_subgraph adds it). So there
    // is nothing to do to the note graph here.
    ensure_scene_names(s);   // ADR-0022 P3.3: the new scene gets a default name ("A","B",…)
    rebuild_track_view(s);
    std::fprintf(stderr, "[Session] + scene %d (now %d scenes)\n", ns - 1, ns);
    return ns - 1;
}

// ADR-0022 P3.3: the note-generator ops available to place in a scene cell (Euclid/Chord/RandMelody).
int session_available_generator_count(Session* s) {
    return (s && s->op_reg) ? vivid::audio_gen_op_count(*s->op_reg) : 0;
}
const char* session_available_generator_name(Session* s, int idx) {
    return (s && s->op_reg) ? vivid::audio_gen_op_name(*s->op_reg, idx) : "";
}
// Place a generator of `type` into (track, scene): the cell becomes a GENERATOR cell (its clip is
// left intact but the graph now voices the generator for that scene). Replaces any generator already
// there. Returns 1 on success. Derived tracks only for now (an authoritative note graph is rebuilt by
// republish, not from gen_cells — a documented limitation, like the authoritative add_scene case).
int session_place_generator(Session* s, int track, int scene, const char* type) {
    Track* tr = graph_track(s, track);
    if (!tr || tr->is_audio || !s->op_reg || scene < 0 || scene >= s->scenes) return 0;
    vivid::AudioOp* op = vivid::audio_op_create(*s->op_reg, type);
    if (!op) return 0;
    if (scene >= static_cast<int>(tr->gen_cells.size())) tr->gen_cells.resize(scene + 1);
    if (tr->gen_cells[scene].op) tr->op_retired.push_back(tr->gen_cells[scene].op);   // retire the old (freed at shutdown)
    tr->gen_cells[scene].op = op;
    tr->gen_cells[scene].type = type ? type : "";
    rebuild_track_graph(tr);   // republish: the scene's node becomes a NativeGen
    return 1;
}
// Revert a scene cell to a clip (removes its generator). Returns 1 if a generator was removed.
int session_remove_generator(Session* s, int track, int scene) {
    Track* tr = graph_track(s, track);
    if (!tr || scene < 0 || scene >= static_cast<int>(tr->gen_cells.size()) || !tr->gen_cells[scene].op) return 0;
    tr->op_retired.push_back(tr->gen_cells[scene].op);   // retire, never free on the audio thread
    tr->gen_cells[scene].op = nullptr;
    tr->gen_cells[scene].type.clear();
    rebuild_track_graph(tr);   // republish: the scene's node reverts to MidiClip
    return 1;
}
int session_cell_is_generator(Session* s, int track, int scene) {
    Track* tr = graph_track(s, track);
    return (tr && scene >= 0 && scene < static_cast<int>(tr->gen_cells.size()) && tr->gen_cells[scene].op) ? 1 : 0;
}
const char* session_generator_type(Session* s, int track, int scene) {
    Track* tr = graph_track(s, track);
    return (tr && scene >= 0 && scene < static_cast<int>(tr->gen_cells.size()) && tr->gen_cells[scene].op)
               ? tr->gen_cells[scene].type.c_str() : "";
}
// A cell holds nothing playable: not a generator, and (audio) no imported clip / (MIDI) no notes. The
// session view draws these as recessed slots; a click on one STOPS the track (Ableton clip-stop idiom).
// Mirrors the classification in session_view.cpp so the click target and the drawn look stay in agreement.
int session_cell_is_empty(Session* s, int track, int scene) {
    if (session_cell_is_generator(s, track, scene)) return 0;
    if (session_track_is_audio(s, track)) return session_audio_clip_bpm(s, track, scene) <= 0 ? 1 : 0;
    ClipNote nb; return session_get_clip(s, track, scene, &nb, 1) == 0 ? 1 : 0;
}
// ADR-0022 P3.3: a scene cell's generator op params (empty if the cell is a clip). Params are
// lock-free atomics on the op, so set takes effect without a rebuild.
static vivid::AudioOp* gen_cell_op(Session* s, int track, int scene) {
    Track* tr = graph_track(s, track);
    return (tr && scene >= 0 && scene < static_cast<int>(tr->gen_cells.size())) ? tr->gen_cells[scene].op : nullptr;
}
int session_generator_param_count(Session* s, int track, int scene) {
    vivid::AudioOp* op = gen_cell_op(s, track, scene);
    return op ? vivid::audio_op_param_count(op) : 0;
}
const char* session_generator_param_name(Session* s, int track, int scene, int i) {
    vivid::AudioOp* op = gen_cell_op(s, track, scene);
    return op ? vivid::audio_op_param_name(op, i) : "";
}
float session_generator_param_value(Session* s, int track, int scene, int i) {
    vivid::AudioOp* op = gen_cell_op(s, track, scene);
    return op ? vivid::audio_op_param_get(op, i) : 0.f;
}
int session_set_generator_param(Session* s, int track, int scene, const char* name, float v) {
    vivid::AudioOp* op = gen_cell_op(s, track, scene);
    if (!op || !name) return 0;
    const int n = vivid::audio_op_param_count(op);
    for (int i = 0; i < n; ++i)
        if (std::strcmp(vivid::audio_op_param_name(op, i), name) == 0) { vivid::audio_op_param_set(op, i, v); return 1; }
    return 0;
}
int session_generator_draw_thumbnail(Session* s, int track, int scene, const ::VividThumbnailContext* ctx) {
    vivid::AudioOp* op = gen_cell_op(s, track, scene);
    if (!op || !ctx) return 0;
    vivid::audio_op_draw_thumbnail(op, ctx);   // op reads only ctx (param snapshot + draw API)
    return 1;
}

int session_op_draw_catalog_thumbnail(Session* s, const char* op_name, const ::VividThumbnailContext* ctx) {
    // ADR-0050 CATALOG preview: no placed instance, so make a transient op from the registry, let it
    // draw from `ctx` (the op reads only the ctx snapshot — draw API + purpose + params), and drop it.
    if (!s || !s->op_reg || !op_name || !*op_name || !ctx) return 0;
    vivid::AudioOp* op = vivid::audio_op_create(*s->op_reg, op_name);
    if (!op) return 0;
    vivid::audio_op_draw_thumbnail(op, ctx);
    vivid::audio_op_destroy(op);
    return 1;
}

// Load-time only: set the scene count BEFORE tracks are recreated (rebuild_tracks_from_doc),
// so each track is born with the right number of clip slots. Clamped to [1, kMaxScenes].
void session_set_scene_count(Session* s, int scenes) {
    if (!s) return;
    s->scenes = std::min(std::max(scenes, 1), kMaxScenes);
    ensure_scene_names(s);   // ADR-0022 P3.3: keep names sized to the scene count (defaults fill new slots)
}

// A bare native-instrument track (no VST3 handle) whose instrument + effects come from an
// authoritative audio graph loaded onto it — the home for a persisted rewired graph. Same shape
// as the track slice_to_midi builds; reserve_track_graph runs inside make_instrument_track.
int session_add_graph_track(Session* s, const char* name) {
    if (!s || static_cast<int>(s->tracks.size()) >= kMaxTracks) return -1;
    s->tracks.emplace_back(make_instrument_track(nullptr, (name && *name) ? name : "Graph", s->scenes, s->sample_rate));
    s->tracks.back()->id = s->next_track_id++;
    rebuild_track_graph(s->tracks.back().get());   // derive the plan NOW (parity with session_create's line-951 up-front derive)
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
// P4: -1 = the plugin implements no IMidiMapping, so no MIDI controller can reach it at all.
int session_param_midi_cc_count(Session* s, int t, int dev) {
    Vst3Handle* h = device_handle(s, t, dev);
    if (!h) return -1;
    return h->midi_map_ok ? h->midi_map_n : -1;
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
