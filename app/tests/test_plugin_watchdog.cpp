// ADR-0045 Tier 2a — the realtime plugin WATCHDOG, exercised through the REAL render path.
//
// The tier-1 attribution test (test_plugin_crash_attrib.cpp) fork+raises to reproduce the guard/handler
// by hand; it never drives a fault through session_process. This one does: it loads the in-tree CLAP
// fixture in a deliberately-slow mode (a busy-wait inside process(), VIVID_TEST_CLAP_SLOW_MS), pumps
// session_process, and proves the watchdog (1) counts over-budget strikes, (2) latches the plugin
// `faulted` and emits ONE named report on the fault ring, and (3) then SKIPS the offender so a block no
// longer stalls — i.e. it actually bounds the RT cost, not just observes it.
//
// macOS/app-ON tier (the engine reaches CoreFoundation to load the .clap bundle).
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"
#include "audio/plugin_fault_ring.h"
#include "gpu/op_runtime.h"
#include "test_helpers.h"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace vivid::session;
using namespace vivid::test;

#ifndef VIVID_TEST_CLAP_PATH
#error "VIVID_TEST_CLAP_PATH must point at the built .clap bundle"
#endif
static constexpr int kFmtCLAP = 1;   // PluginFormat::kFmtCLAP (plugin_catalog.h)

static constexpr uint32_t kFrames = 256;   // ~5.3 ms per block @ 48k — the slow mode's 50 ms dwarfs it

// Add the fixture as an effect node and pump the async CLAP loader until it binds (mirrors the roundtrip test).
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


// Render one block; return its wall-clock duration in microseconds (the thing the watchdog bounds).
static long process_block_us(Session* s, uint32_t sr, int b) {
    std::vector<float> out(static_cast<size_t>(kFrames) * 2, 0.f);
    const auto t0 = std::chrono::steady_clock::now();
    session_process(s, out.data(), kFrames, sr, 120.0, b * 0.1, 4, /*playing*/true, false);
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
}

int main() {
    // Configure the watchdog + fixture BEFORE any config/env is read (both cache on first use): a 50 ms
    // busy-wait per process() (≈9× the block budget → a strike every block), faulting after 3 strikes.
    setenv("VIVID_TEST_CLAP_SLOW_MS", "50", 1);
    setenv("VIVID_PLUGIN_STRIKES", "3", 1);
    setenv("VIVID_PLUGIN_BUDGET_MULT", "1.0", 1);

    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000;

    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    const int t = session_add_graph_track(s, "T");
    CHECK(t >= 0);

    // Build an ACTIVE render path so the plugin's process() actually runs. A track with no instrument is
    // skipped entirely by the executor, so give it TestTone + a long held note; add_plugin(is_source=0)
    // auto-splices the slow effect before Output (splice_before_output), so it is already in the plan.
    CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
    ClipNote note{}; note.pitch = 69; note.start = 0.0; note.dur = 100000.0; note.vel = 1.0f;
    session_set_clip(s, t, 0, &note, 1, 100000.0);
    session_launch_scene(s, 0);

    const int nid = load_fixture(s, t);
    CHECK(nid >= 0);   // the fixture bound as an effect, spliced before Output

    // Pump blocks. The slow plugin blows its budget every block; after 3 strikes it faults, a report is
    // emitted, and thereafter it is skipped. Capture the slow (pre-fault) and fast (post-fault) timings.
    long slow_us = 0, fast_us = 0;
    for (int b = 0; b < 10; ++b) {
        const long us = process_block_us(s, sr, b);
        if (b == 0) slow_us = us;      // first block: the plugin runs, ~50 ms
        fast_us = us;                  // last block: the plugin should be disabled, ~0
    }

    // (1) A pre-fault block actually ran the slow plugin (proves the fixture's slow mode + the render path).
    CHECK(slow_us > 40000);            // ~50 ms; comfortably above the 40 ms floor

    // (2) Exactly one named over-budget report reached the ring.
    vivid::audio::PluginFaultRecord recs[16];
    const int n = vivid::audio::plugin_fault_ring().drain(recs, 16);
    CHECK(n >= 1);
    bool named_overbudget = false;
    for (int i = 0; i < n; ++i)
        if (recs[i].reason == vivid::audio::PluginFaultReason::OverBudget &&
            recs[i].name && recs[i].name[0] != '\0' && recs[i].track_id == t)
            named_overbudget = true;
    CHECK(named_overbudget);

    // (3) After the fault the offender is SKIPPED — the block no longer stalls (the whole point of Tier 2a).
    CHECK(fast_us < 10000);            // well under the 50 ms the plugin would take if still run

    session_destroy(s);
    return summary("plugin_watchdog");
}
