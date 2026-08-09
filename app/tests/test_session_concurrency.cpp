// ADR-0029 (phase 2): the concurrent audio↔UI harness. `test_session_executor` drives session_process on
// ONE thread, so ThreadSanitizer finds nothing there. This races the two roles the whole gen-counter +
// try_lock + SPSC machinery exists to keep safe: a RENDER thread looping session_process, and a single
// UI/MUTATOR thread that adds/removes tracks, edits clips, mutates the graph, sets params, delivers and
// clears frame-bridge param overrides (ADR-0030 P2), launches scenes, and reads the published snapshots.
//
// ADR-0031 §1 widened the mutator to also churn graph EDGES — connect/disconnect audio + control edges and
// remove nodes — the xaudio/xctl/gmtx handoff path the earlier switch never touched, and where slice 2's
// RT handoff-skip counter lives. OUT OF SCOPE here (deliberately): undo/redo and audio↔visual mappings —
// both are App/UI-layer state (undo_manager, ui/NodeGraph MappingRegistry), not the `session_*` C API this
// harness drives; they race the EDIT model, not session_process. The mapping mechanism that DOES reach the
// render — the frame-bridge override channel mappings write through — is already raced (cases 12/13/15).
// Under TSan (the macOS AUDIO_THREAD leg) this proves the audio↔UI
// channels are race-clean; the assertions (finite output, sane snapshots) are secondary to TSan observing
// the races. Model per app/docs/thread-safety.md: all mutation on one UI thread; the audio thread only
// reads (via gen/try_lock/SPSC) and never touches s->tracks (removed tracks park in tracks_retired, freed
// at session_destroy, so a Track* live in tracks_view outlives the erase).
//
// macOS-only (the engine reaches CoreFoundation): built in the full-app configure, like test_session_executor.
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"   // register_builtin_audio_ops
#include "audio/audio_budgets.h"       // ADR-0031 §6: stress duration + allowed-skip budget
#include "audio/audio_health.h"        // ADR-0031 §3: RtScope + g_handoff_skips
#include "gpu/op_runtime.h"            // vivid::OpRegistry
#include "midi/midi_clip.h"            // ClipNote
#include "test_helpers.h"

#include <atomic>
#include <cmath>
#include <thread>
#include <chrono>
#include <string>
#include <vector>

using namespace vivid::session;

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000, frames = 256;

    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    // Seed one playing track so the render thread has real signal + notes to churn from the first block.
    const int t0 = session_add_graph_track(s, "T0");
    CHECK(t0 >= 0);
    session_set_track_audio_instrument(s, t0, "TestTone");
    ClipNote n0{}; n0.pitch = 60; n0.start = 0.0; n0.dur = 1000.0; n0.vel = 0.9f;
    session_set_clip(s, t0, 0, &n0, 1, 1000.0);
    session_launch_scene(s, 0);

    // ADR-0034: a real CLAP plugin (the in-tree fixture) on t0, modulated by an LFO, so the render
    // thread reads the plugin base mirror (ClapHandle::abase, via resolve_clap_control) while the
    // mutator authors it below — racing that channel under TSan. Loaded here, before the threads
    // start; skipped gracefully if the fixture is unavailable.
    int plug_nid = -1, plug_gp = -1;
#ifdef VIVID_TEST_CLAP_PATH
    plug_nid = session_audio_graph_add_plugin(s, t0, VIVID_TEST_CLAP_PATH, /*kFmtCLAP*/1, /*is_source*/0, "");
    for (int i = 0; i < 2000 && plug_nid >= 0; ++i) {
        session_poll_plugin_loads(s);
        if (session_audio_graph_node_plugin_ready(s, t0, plug_nid) == 1) break;
        if (session_audio_graph_node_plugin_failed(s, t0, plug_nid)) { plug_nid = -1; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (plug_nid >= 0) {
        for (int p = 0, pc = session_audio_graph_node_param_count(s, t0, plug_nid); p < pc; ++p)
            if (std::string(session_audio_graph_node_param_name(s, t0, plug_nid, p)) == "gain") { plug_gp = p; break; }
        if (const int lfo = session_audio_graph_add_mod_op(s, t0, "LFO"); lfo >= 0 && plug_gp >= 0)
            session_audio_graph_connect_control(s, t0, lfo, plug_nid, plug_gp, 0.3f, 0.f, 0, 0);   // capture-on-wire
    }
#endif

    std::atomic<bool> stop{false};
    std::atomic<bool> ui_done{false};
    std::atomic<bool> finite_ok{true};   // set false by the render thread on any NaN/Inf (CHECK'd after join —
                                         // CHECK's static counter isn't thread-safe, so only the main thread uses it)

    // --- RENDER thread: the audio-callback role -----------------------------------------------------
    std::thread render([&] {
        // ADR-0031 §3: this thread plays the RT audio callback, so mark the RT scope — session_process's
        // try_lock handoff-skip counter (vivid::audio::health::g_handoff_skips) is gated on it. Under this
        // harness's deliberate contention, skips WILL accrue; that is the metric slice 4 can bound.
        vivid::audio::health::RtScope rt_scope;
        std::vector<float> out(static_cast<size_t>(frames) * 2, 0.f);
        double beats = 0.0;
        const double per_block = static_cast<double>(frames) / sr * (120.0 / 60.0);
        while (!stop.load(std::memory_order_relaxed)) {
            std::fill(out.begin(), out.end(), 0.f);
            session_process(s, out.data(), frames, sr, 120.0, beats, /*beats_per_bar*/4, /*playing*/true, false);
            for (float v : out) if (!std::isfinite(v)) finite_ok.store(false, std::memory_order_relaxed);
            beats += per_block;
        }
    });

    // --- UI/MUTATOR thread: the frame-thread role (single mutator; also reads the published snapshots) --
    std::thread ui([&] {
        constexpr int kMaxTracks = 4;
        ActiveNote held[64];
        float scope[128], spec[1024];
        // ADR-0031 §6: budget-driven run length. VIVID_AUDIO_STRESS_MS>0 runs the mutator for that many
        // wall-clock ms (a longer soak on demand); the default 0 keeps the fixed 3000-iteration loop.
        const uint32_t stress_ms = vivid::audio::audio_budgets().stress_ms;
        const auto t_start = std::chrono::steady_clock::now();
        auto more = [&](int k) {
            if (stress_ms > 0)
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - t_start).count() < static_cast<long long>(stress_ms);
            return k < 3000;
        };
        for (int k = 0; more(k); ++k) {
            const int nt = session_track_count(s);
            const int tr = nt > 0 ? (k % nt) : -1;
            switch (k % 20) {
                case 0: if (nt < kMaxTracks) session_add_graph_track(s, "Tx"); break;   // tracks_gen
                case 1: if (nt > 1) session_remove_track(s, nt - 1); break;             // tracks_gen + tracks_retired
                case 2: if (tr >= 0) session_set_track_audio_instrument(s, tr, "TestTone"); break;  // op_fx_gen + ggen
                case 3: if (tr >= 0) { ClipNote c{}; c.pitch = 48 + (k % 24); c.start = 0.0; c.dur = 500.0; c.vel = 0.8f;
                                       session_set_clip(s, tr, 0, &c, 1, 500.0); } break;           // edit_gen (+ held)
                case 4: if (tr >= 0) session_audio_graph_add_op(s, tr, "Bitcrush"); break;          // gmtx + ggen
                case 5: if (tr >= 0) session_audio_graph_add_mod_op(s, tr, "LFO"); break;           // gmtx + ggen
                case 6: if (tr >= 0) { const int nn = session_track_audio_graph_node_count(s, tr);        // param_q / native-op
                                       if (nn > 0) session_audio_graph_node_param_set(s, tr, session_track_audio_graph_node_id(s, tr, nn - 1), 0, static_cast<float>(k % 100) / 100.f); } break;
                case 7: if (tr >= 0) session_set_track_node_analyze_mask(s, tr, 0x1u); break;        // mask + node_an capture
                case 8: session_launch_scene(s, k % 3); break;                                       // queued (relaxed)
                case 9: if (tr >= 0) (void)session_track_active_notes(s, tr, held, 64); break;        // read held set
                case 10: if (tr >= 0) { const int nn = session_track_audio_graph_node_count(s, tr);         // read node scope
                                        if (nn > 0) (void)session_track_audio_graph_node_scope(s, tr, 0, scope, 128); } break;
                case 11: if (tr >= 0) (void)session_track_analysis_copy(s, tr, spec, 1024); break;    // read spectrum ring
                // ADR-0030 P2: race the non-destructive frame-bridge override channel (AudioOp::fovr,
                // read on the render thread in audio_op_process / resolve_control_inputs) against the
                // UI writing it — deliver a value, then clear it, exactly as apply_audio_param_mappings does.
                case 12: if (tr >= 0) { const int nn = session_track_audio_graph_node_count(s, tr);
                                        if (nn > 0) session_audio_graph_node_param_deliver(s, tr, session_track_audio_graph_node_id(s, tr, nn - 1), 0, static_cast<float>(k % 100) / 100.f); } break;
                case 13: if (tr >= 0) { const int nn = session_track_audio_graph_node_count(s, tr);
                                        if (nn > 0) session_audio_graph_node_param_override_clear(s, tr, session_track_audio_graph_node_id(s, tr, nn - 1), 0); } break;
                // ADR-0034: author the modulated plugin's base (base_author → ClapHandle::abase write)
                // while the render thread resolves modulation off it — the race the atomic base guards.
                case 14: if (plug_nid >= 0 && plug_gp >= 0)
                             session_audio_graph_node_param_set(s, t0, plug_nid, plug_gp, static_cast<float>(k % 100) / 100.f);
                         break;
                // ADR-0034 P3: deliver/clear a frame-bridge override on the modulated plugin param
                // (bridge_set/bridge_clear → abr_on/abridge writes) while the render resolves modulation
                // off the effective base (aeff_load reads) — races the new bridge-compose channel.
                case 15: if (plug_nid >= 0 && plug_gp >= 0) {
                             if (k & 1) session_audio_graph_node_param_deliver(s, t0, plug_nid, plug_gp, static_cast<float>(k % 100) / 100.f);
                             else       session_audio_graph_node_param_override_clear(s, t0, plug_nid, plug_gp);
                         }
                         break;
                // ADR-0031 §1: race graph EDGE churn — connect/disconnect audio + control edges and remove
                // nodes while the render thread swaps the compiled plan. This is the xaudio/xctl/gmtx handoff
                // path (where slice 2's g_handoff_skips counter lives), previously unexercised by the harness.
                case 16: if (tr >= 0) { const int nn = session_track_audio_graph_node_count(s, tr);          // audio edge connect
                                        if (nn >= 2) session_audio_graph_connect(s, tr, session_track_audio_graph_node_id(s, tr, 0),
                                                                                 session_track_audio_graph_node_id(s, tr, nn - 1)); } break;
                case 17: if (tr >= 0) { const int nn = session_track_audio_graph_node_count(s, tr);          // audio edge disconnect
                                        if (nn >= 2) session_audio_graph_disconnect(s, tr, session_track_audio_graph_node_id(s, tr, 0),
                                                                                    session_track_audio_graph_node_id(s, tr, nn - 1)); } break;
                case 18: if (tr >= 0) { const int nn = session_track_audio_graph_node_count(s, tr);          // control edge connect/disconnect
                                        if (nn >= 2) { const int a = session_track_audio_graph_node_id(s, tr, nn - 1),
                                                                 b = session_track_audio_graph_node_id(s, tr, 0);
                                                       if (k & 1) session_audio_graph_connect_control(s, tr, a, b, 0, 0.25f, 0.f, 0, 0);
                                                       else       session_audio_graph_disconnect_control(s, tr, a, b, 0); } } break;
                case 19: if (tr >= 0) { const int nn = session_track_audio_graph_node_count(s, tr);          // remove a node (effects only)
                                        if (nn >= 2) session_audio_graph_remove_node(s, tr, session_track_audio_graph_node_id(s, tr, nn - 1)); } break;
            }
            if ((k & 63) == 0) std::this_thread::yield();
        }
        ui_done.store(true, std::memory_order_relaxed);
    });

    ui.join();
    stop.store(true, std::memory_order_relaxed);
    render.join();

    CHECK(ui_done.load(std::memory_order_relaxed));           // both threads finished
    CHECK(finite_ok.load(std::memory_order_relaxed));         // no NaN/Inf under any concurrent mutation

    // ADR-0031 §6: opt-in handoff-skip ceiling. Skips are EXPECTED under this deliberate contention, so the
    // default (VIVID_AUDIO_ALLOWED_SKIPS=0) disables the check — the real assurance is TSan seeing no race.
    // Set it >0 to bound skips over a soak run. The counter only moved because the render thread ran in RtScope.
    const uint32_t allowed = vivid::audio::audio_budgets().allowed_skips;
    const uint64_t skips = vivid::audio::health::g_handoff_skips.load(std::memory_order_relaxed);
    if (allowed > 0) CHECK(skips <= allowed);

    session_destroy(s);
    std::printf("test_session_concurrency: OK (handoff skips observed: %llu)\n",
                static_cast<unsigned long long>(skips));
    return 0;
}
