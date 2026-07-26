// ADR-0029 (phase 2): the concurrent audio↔UI harness. `test_session_executor` drives session_process on
// ONE thread, so ThreadSanitizer finds nothing there. This races the two roles the whole gen-counter +
// try_lock + SPSC machinery exists to keep safe: a RENDER thread looping session_process, and a single
// UI/MUTATOR thread that adds/removes tracks, edits clips, mutates the graph, sets params, launches scenes,
// and reads the published snapshots. Under TSan (the macOS AUDIO_THREAD leg) this proves the audio↔UI
// channels are race-clean; the assertions (finite output, sane snapshots) are secondary to TSan observing
// the races. Model per app/docs/thread-safety.md: all mutation on one UI thread; the audio thread only
// reads (via gen/try_lock/SPSC) and never touches s->tracks (removed tracks park in tracks_retired, freed
// at session_destroy, so a Track* live in tracks_view outlives the erase).
//
// macOS-only (the engine reaches CoreFoundation): built in the full-app configure, like test_session_executor.
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"   // register_builtin_audio_ops
#include "gpu/op_runtime.h"            // vivid::OpRegistry
#include "midi/midi_clip.h"            // ClipNote
#include "test_helpers.h"

#include <atomic>
#include <cmath>
#include <thread>
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

    std::atomic<bool> stop{false};
    std::atomic<bool> ui_done{false};
    std::atomic<bool> finite_ok{true};   // set false by the render thread on any NaN/Inf (CHECK'd after join —
                                         // CHECK's static counter isn't thread-safe, so only the main thread uses it)

    // --- RENDER thread: the audio-callback role -----------------------------------------------------
    std::thread render([&] {
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
        for (int k = 0; k < 3000; ++k) {
            const int nt = session_track_count(s);
            const int tr = nt > 0 ? (k % nt) : -1;
            switch (k % 12) {
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

    session_destroy(s);
    std::puts("test_session_concurrency: OK");
    return 0;
}
