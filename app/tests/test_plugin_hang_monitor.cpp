// ADR-0045 Tier 2a — the permanent-HANG monitor, exercised through the real render path.
//
// The over-budget watchdog (test_plugin_watchdog) disables a plugin that RETURNS too slowly. This proves
// the other half: a plugin still INSIDE process() past the hang deadline is caught by the monitor thread —
// which names it, latches it `faulted`, and pushes a Hang report — so it is skipped on the next block. The
// slow CLAP fixture spins 300 ms per process(); with the strike limit set high, the over-budget path can't
// fault it first, so the fault here comes from the monitor.
//
// macOS/app-ON tier (the engine reaches CoreFoundation to load the .clap bundle).
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"
#include "audio/plugin_fault_ring.h"
#include "audio/plugin_hang_monitor.h"
#include "gpu/op_runtime.h"
#include "test_helpers.h"

#include <chrono>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace vivid::session;
using namespace vivid::test;

#ifndef VIVID_TEST_CLAP_PATH
#error "VIVID_TEST_CLAP_PATH must point at the built .clap bundle"
#endif
static constexpr int kFmtCLAP = 1;
static constexpr uint32_t kFrames = 256;

static int load_fixture(Session* s, int t) {
    const int nid = session_audio_graph_add_plugin(s, t, VIVID_TEST_CLAP_PATH, kFmtCLAP, /*is_source*/0, "");
    if (nid < 0) return -1;
    for (int i = 0; i < 2000; ++i) {
        session_poll_plugin_loads(s);
        if (session_audio_graph_node_plugin_ready(s, t, nid) == 1) return nid;
        if (session_audio_graph_node_plugin_failed(s, t, nid)) return -2;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return -3;
}

static long process_block_us(Session* s, uint32_t sr, int b) {
    std::vector<float> out(static_cast<size_t>(kFrames) * 2, 0.f);
    const auto t0 = std::chrono::steady_clock::now();
    session_process(s, out.data(), kFrames, sr, 120.0, b * 0.1, 4, /*playing*/true, false);
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
}

int main() {
    // 300 ms per process() (a "hang" vs the 50 ms deadline); strikes high so the over-budget path stays out
    // of it — the fault must come from the monitor. Set before any config/env is cached.
    setenv("VIVID_TEST_CLAP_SLOW_MS", "300", 1);
    setenv("VIVID_PLUGIN_HANG_MS", "50", 1);
    setenv("VIVID_PLUGIN_STRIKES", "100", 1);

    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000;

    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    const int t = session_add_graph_track(s, "T");
    CHECK(t >= 0);
    CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
    ClipNote note{}; note.pitch = 69; note.start = 0.0; note.dur = 100000.0; note.vel = 1.0f;
    session_set_clip(s, t, 0, &note, 1, 100000.0);
    session_launch_scene(s, 0);

    const int nid = load_fixture(s, t);
    CHECK(nid >= 0);

    vivid::audio::PluginHangMonitor mon;
    mon.start();   // begins polling the in-flight beacon (deadline 50 ms)

    // Block 1 sits ~300 ms inside the slow plugin; the monitor trips the 50 ms deadline mid-block, names
    // it, and latches `faulted`. Block 2 then skips it.
    const long hung_us = process_block_us(s, sr, 0);
    const long next_us = process_block_us(s, sr, 1);
    mon.stop();

    CHECK(hung_us > 200000);   // the plugin really was stuck ~300 ms (proves the render path + slow mode)

    // The monitor emitted a named Hang report for this track.
    vivid::audio::PluginFaultRecord recs[16];
    const int n = vivid::audio::plugin_fault_ring().drain(recs, 16);
    bool named_hang = false;
    for (int i = 0; i < n; ++i)
        if (recs[i].reason == vivid::audio::PluginFaultReason::Hang &&
            recs[i].name && recs[i].name[0] != '\0' && recs[i].track_id == t)
            named_hang = true;
    CHECK(named_hang);

    // After the fault the plugin is skipped — the block no longer stalls.
    CHECK(next_us < 10000);

    session_destroy(s);
    return summary("plugin_hang_monitor");
}
