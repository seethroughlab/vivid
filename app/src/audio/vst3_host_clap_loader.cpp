// ADR-0025 (vst3_host split, PR-E): the ASYNC CLAP LOADER, extracted from vst3_host.cpp. A CLAP
// plugin ctor can block for seconds (Surge scans its wavetable dir), so it must never run on the main
// thread: enqueue_clap_load() hands the request to one background worker (clap_worker_main), and the
// main thread applies finished handles via session_poll_plugin_loads() — all Track/graph edits stay on
// the main thread. This is the SEAM between async loading and the graph engine, so it calls back into
// rebuild_track_graph() (shared via vst3_host_internal.h). Pure code move; behaviour unchanged.
#include "audio/vst3_host_internal.h"   // Session/Track, ClapHandle, clap_load_plugin/_state, GNKind, the C API decls + rebuild_track_graph

namespace vivid::session {

// CLAP instrument/effect assignment is ASYNC — see session_request_track_clap_* below. (A slow
// plugin ctor must never run on the main thread.) The old synchronous session_set_track_clap_*
// entry points were removed once persist + the control server both moved to the async path.

// --- CLAP loading — HYBRID, to satisfy a hard JUCE invariant WITHOUT a per-load main-thread freeze.
// JUCE binds its message thread to the thread that FIRST initializes JUCE (i.e. constructs the first
// JUCE plugin); that thread must be the MAIN thread or the plugin's editor (clap.gui) deadlocks on the
// MessageManagerLock. But once bound, every LATER plugin can be built on ANY thread — they reuse the
// same main-bound MessageManager singleton. So: session_poll_plugin_loads() builds the FIRST CLAP of
// the session on the MAIN thread (a one-time block) and sets clap_juce_pinned; every subsequent load it
// hands to this background worker (async, no main-thread freeze), whose editors still work. ---
static void clap_worker_main(Session* s) {
    for (;;) {
        Session::ClapLoadReq req;
        {
            std::unique_lock<std::mutex> lk(s->clap_load_mtx);
            s->clap_load_cv.wait(lk, [s]{ return s->clap_worker_stop || !s->clap_bg_reqs.empty(); });
            if (s->clap_worker_stop) return;                 // dying: drop any queued requests
            req = std::move(s->clap_bg_reqs.front());
            s->clap_bg_reqs.pop_front();
        }
        // Only PINNED loads ever reach clap_bg_reqs (the poll routes them here after JUCE is bound to
        // main), so constructing off the main thread is safe: the plugin reuses the main-bound
        // MessageManager, and its editor still opens.
        ClapHandle* h = clap_load_plugin(req.path, req.sr, kGraphMaxBlock);   // SLOW — off the main thread
        if (h) clap_init_plugin(h);
        {
            std::lock_guard<std::mutex> lk(s->clap_load_mtx);
            s->clap_done.push_back({ req.track_id, req.is_instrument, req.path, h, std::move(req.state), req.slot });
        }
    }
}
static void ensure_clap_worker(Session* s) {
    if (!s->clap_worker.joinable()) s->clap_worker = std::thread(clap_worker_main, s);
}
int enqueue_clap_load(Session* s, int t, bool is_instrument, const char* clap_path,
                      const char* state, int slot) {   // default slot=-1 is on the decl in vst3_host_internal.h
    const double sr = s->sample_rate > 0 ? s->sample_rate : 48000;
    const int tid = s->tracks[t]->id;   // capture the STABLE id (callers validated t in range)
    {
        std::lock_guard<std::mutex> lk(s->clap_load_mtx);
        s->clap_last_error.clear();
        s->clap_reqs.push_back({ tid, is_instrument, clap_path, sr, state ? state : "", slot });   // poll routes it
    }
    s->clap_pending.fetch_add(1, std::memory_order_release);
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
    // 1) Route/construct ONE queued request per poll. The FIRST CLAP load of the session is built on
    //    THIS (main) thread, which pins JUCE's message thread to main — required so every plugin's
    //    editor (clap.gui) can open. That one construction blocks the loop (Surge scans its wavetables;
    //    mostly a first-load cost). Once pinned, every subsequent load is handed to the background
    //    worker (no main-thread freeze); those plugins reuse the main-bound JUCE singleton, so their
    //    editors work too.
    {
        Session::ClapLoadReq req; bool have = false;
        {
            std::lock_guard<std::mutex> lk(s->clap_load_mtx);
            if (!s->clap_reqs.empty()) { req = std::move(s->clap_reqs.front()); s->clap_reqs.pop_front(); have = true; }
        }
        if (have) {
            if (!s->clap_juce_pinned.load(std::memory_order_acquire)) {
                ClapHandle* h = clap_load_plugin(req.path, req.sr, kGraphMaxBlock);   // MAIN-thread create (SLOW, once)
                if (h && !clap_init_plugin(h)) { delete h; h = nullptr; }
                s->clap_juce_pinned.store(true, std::memory_order_release);
                s->clap_load_cv.notify_all();                                          // release the worker
                done.push_back({ req.track_id, req.is_instrument, req.path, h, std::move(req.state), req.slot });
            } else {
                std::lock_guard<std::mutex> lk(s->clap_load_mtx);
                ensure_clap_worker(s);
                s->clap_bg_reqs.push_back(std::move(req));
                s->clap_load_cv.notify_one();
            }
        }
    }
    // 2) Collect any async (background-worker) completions to bind this frame.
    {
        std::lock_guard<std::mutex> lk(s->clap_load_mtx);
        while (!s->clap_done.empty()) { done.push_back(std::move(s->clap_done.front())); s->clap_done.pop_front(); }
    }
    // 3) Bind everything (the main-thread pin result + async completions) into their tracks/slots.
    for (auto& d : done) {
        ClapHandle* h = d.handle;   // already constructed + init'd (on the main thread, or on the worker)
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
void stop_clap_loader(Session* s) {
    if (s->clap_worker.joinable()) {
        { std::lock_guard<std::mutex> lk(s->clap_load_mtx); s->clap_worker_stop = true; }
        s->clap_load_cv.notify_all();
        s->clap_worker.join();
    }
    for (auto& d : s->clap_done) delete d.handle;   // finished-but-unapplied handles
    s->clap_done.clear();
}

}  // namespace vivid::session
