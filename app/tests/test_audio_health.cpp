// ADR-0031 §3 — RT audio-health counters + the RtScope handoff-skip gate.
//
// The load-bearing new mechanic is the thread-local RT scope: note_handoff_skip() must count ONLY on
// the realtime thread (inside RtScope), so the offline bounce — which calls session_process directly,
// off the RT thread — never pollutes the contention metric. This test proves the gate as a unit, unit-
// tests the over-budget accounting, then drives the real session_process render path off-RT to confirm
// the wired-in skip counters stay silent (and that an oversized block still bails to silence).
//
// macOS-only (the engine reaches CoreFoundation), like test_audio_bounce.
#include "audio/audio_health.h"
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"   // register_builtin_audio_ops
#include "gpu/op_runtime.h"            // vivid::OpRegistry
#include "midi/midi_clip.h"            // ClipNote
#include "test_helpers.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace ah = vivid::audio::health;
using namespace vivid::session;

int main() {
    // --- The RtScope gate: note_handoff_skip counts ONLY inside RtScope (on the RT thread). --------
    ah::g_handoff_skips.store(0);
    ah::note_handoff_skip();                          // no scope -> ignored
    ah::note_handoff_skip();
    CHECK(!ah::in_rt());
    CHECK(ah::g_handoff_skips.load() == 0);
    {
        ah::RtScope rt;
        CHECK(ah::in_rt());
        ah::note_handoff_skip();
        ah::note_handoff_skip();
    }
    CHECK(!ah::in_rt());                              // scope exited
    CHECK(ah::g_handoff_skips.load() == 2);
    ah::note_handoff_skip();                          // outside again -> still ignored
    CHECK(ah::g_handoff_skips.load() == 2);

    // --- note_callback_us: over-budget accounting + the last/max gauges. ---------------------------
    ah::g_over_budget.store(0);
    ah::g_max_callback_us.store(0);
    const uint32_t frames = 256;
    const double sr = 48000.0;
    const double block_us = frames / sr * 1e6;        // ~5333 us realtime for the block
    const uint32_t within = static_cast<uint32_t>(block_us * 0.5);
    const uint32_t over   = static_cast<uint32_t>(block_us * 2.0);
    ah::note_callback_us(within, frames, sr, 1.0);
    CHECK(ah::g_over_budget.load() == 0);
    CHECK(ah::g_last_callback_us.load() == within);
    CHECK(ah::g_max_callback_us.load() == within);
    ah::note_callback_us(over, frames, sr, 1.0);
    CHECK(ah::g_over_budget.load() == 1);
    CHECK(ah::g_last_callback_us.load() == over);
    CHECK(ah::g_max_callback_us.load() == over);      // high-water rises
    ah::note_callback_us(10, frames, sr, 1.0);
    CHECK(ah::g_max_callback_us.load() == over);       // ...but never falls

    // --- Integration: the render path off the RT thread never accrues handoff skips (the gate holds
    //     across many real blocks), and an oversized block bails to silence. -----------------------
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    Session* s = session_create(static_cast<uint32_t>(sr));
    session_set_op_registry(s, &reg);
    const int t0 = session_add_graph_track(s, "T0");
    CHECK(t0 >= 0);
    session_set_track_audio_instrument(s, t0, "TestTone");
    ClipNote n0{}; n0.pitch = 60; n0.start = 0.0; n0.dur = 1000.0; n0.vel = 0.9f;
    session_set_clip(s, t0, 0, &n0, 1, 1000.0);
    session_launch_scene(s, 0);

    ah::g_handoff_skips.store(0);
    std::vector<float> outbuf(static_cast<size_t>(frames) * 2, 0.f);
    double beats = 0.0;
    bool finite = true;
    for (int b = 0; b < 200; ++b) {
        session_process(s, outbuf.data(), frames, static_cast<uint32_t>(sr), 120.0, beats, 4, true, false);
        for (float v : outbuf) if (!std::isfinite(v)) finite = false;
        beats += frames / sr * 2.0;
    }
    CHECK(finite);
    CHECK(ah::g_handoff_skips.load() == 0);            // off-RT: never counted, even with real render work

    // An oversized block (> kGraphMaxBlock = 4096) fills silence and returns true — no overflow.
    const uint32_t big = 8192;
    std::vector<float> bigbuf(static_cast<size_t>(big) * 2, 1.f);
    const bool rendered = session_process(s, bigbuf.data(), big, static_cast<uint32_t>(sr), 120.0,
                                          beats, 4, true, false);
    CHECK(rendered);
    CHECK(bigbuf.front() == 0.f);
    CHECK(bigbuf.back() == 0.f);

    session_destroy(s);
    return vivid::test::summary("test_audio_health");
}
