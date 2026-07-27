// ADR-0034: control-edge modulation reaches a CLAP plugin param, and (Phase 3) restores on disconnect
// and composes with a frame-bridge mapping. Builds a real audio path — TestTone instrument -> the
// in-tree CLAP fixture (as an effect) -> output — wires a fast LFO onto the fixture's "gain", and
// checks through the real CLAP host path that:
//   (1) the plugin's live value (resolved) SWINGS under modulation while the authored base holds,
//   (2) disconnecting the edge snaps the plugin back to the authored base (restore-on-disconnect),
//   (3) a bridge mapping + a control edge on the same param compose — modulation swings around the
//       BRIDGE value, not the authored base.
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"
#include "gpu/op_runtime.h"
#include "midi/midi_clip.h"
#include "test_helpers.h"

#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <cmath>
#include <algorithm>

using namespace vivid::session;
using namespace vivid::test;

#ifndef VIVID_TEST_CLAP_PATH
#error "VIVID_TEST_CLAP_PATH must point at the built .clap bundle"
#endif
static constexpr int kFmtCLAP = 1;

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
static int param_by_name(Session* s, int t, int nid, const char* name) {
    const int pc = session_audio_graph_node_param_count(s, t, nid);
    for (int p = 0; p < pc; ++p)
        if (std::string(session_audio_graph_node_param_name(s, t, nid, p)) == name) return p;
    return -1;
}

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000, frames = 256;

    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    const int t = session_add_graph_track(s, "T");
    // A real audio path so the effect always renders: TestTone -> CLAP effect -> output.
    session_set_track_audio_instrument(s, t, "TestTone");
    ClipNote n{}; n.pitch = 57; n.start = 0.0; n.dur = 100000.0; n.vel = 1.0f;
    session_set_clip(s, t, 0, &n, 1, 100000.0);
    session_launch_scene(s, 0);

    const int nid = load_fixture(s, t);
    CHECK(nid >= 0);
    if (nid < 0) { session_destroy(s); return summary("clap_modulation"); }
    const int gp = param_by_name(s, t, nid, "gain");
    CHECK(gp >= 0);

    const int lfo = session_audio_graph_add_mod_op(s, t, "LFO");
    CHECK(lfo >= 0);
    if (const int rate = param_by_name(s, t, lfo, "rate"); rate >= 0)
        session_audio_graph_node_param_set(s, t, lfo, rate, 12.0f);   // fast, so full cycles fit the window

    std::vector<float> out(static_cast<size_t>(frames) * 2, 0.f);
    double beats = 0.0; const double per = static_cast<double>(frames) / sr * 2.0;
    // Render `blocks` blocks; report the resolved (plugin-live gain) span + whether the authored base held.
    auto sweep = [&](int blocks, float& lo, float& hi, bool& base_held) {
        lo = 1e9f; hi = -1e9f; base_held = true;
        for (int b = 0; b < blocks; ++b) {
            std::fill(out.begin(), out.end(), 0.f);
            session_process(s, out.data(), frames, sr, 120.0, beats, 4, true, false);
            beats += per;
            const float r = session_audio_graph_node_param_resolved(s, t, nid, gp);
            lo = std::min(lo, r); hi = std::max(hi, r);
            if (std::fabs(session_audio_graph_node_param_get(s, t, nid, gp) - 0.5f) > 1e-4f) base_held = false;
        }
    };

    // (1) Modulation swings the plugin's gain around the captured base 0.5, base unmoved.
    CHECK(session_audio_graph_connect_control(s, t, lfo, nid, gp, 0.3f, 0.f, 0, 0) == 1);   // capture-on-wire -> base 0.5
    CHECK(session_audio_graph_node_param_wired(s, t, nid, gp) == 1);
    CHECK_NEAR(session_audio_graph_node_param_get(s, t, nid, gp), 0.5f, 1e-4);
    float rmin, rmax; bool base_held;
    sweep(96, rmin, rmax, base_held);
    CHECK(base_held);
    CHECK(rmax - rmin > 0.15f);         // genuinely swinging
    CHECK(rmin >= 0.5f - 1e-2f);        // around base 0.5 = base + 0.3*s (no saturation), not the authored-only value
    CHECK(rmax <= 0.8f + 2e-2f);

    // (2) Restore-on-disconnect: removing the edge snaps the plugin back to base (0.5), not stuck high.
    session_audio_graph_disconnect_control(s, t, lfo, nid, gp);
    float dlo, dhi; bool dheld;
    sweep(16, dlo, dhi, dheld);
    CHECK_NEAR(dhi, 0.5f, 1e-2f);
    CHECK_NEAR(dlo, 0.5f, 1e-2f);

    // (3) Precedence: a frame-bridge delivers 0.2 (the effective base) every frame — as the real
    //     apply_audio_param_mappings does — and the LFO modulates ON TOP, so the plugin swings around
    //     0.2 (~[0.2,0.5]), NOT around the authored base 0.5.
    CHECK(session_audio_graph_connect_control(s, t, lfo, nid, gp, 0.3f, 0.f, 0, 0) == 1);
    float blo = 1e9f, bhi = -1e9f;
    for (int b = 0; b < 96; ++b) {
        session_audio_graph_node_param_deliver(s, t, nid, gp, 0.2f);   // the bridge, re-applied each frame
        std::fill(out.begin(), out.end(), 0.f);
        session_process(s, out.data(), frames, sr, 120.0, beats, 4, true, false);
        beats += per;
        const float r = session_audio_graph_node_param_resolved(s, t, nid, gp);
        blo = std::min(blo, r); bhi = std::max(bhi, r);
    }
    CHECK(bhi - blo > 0.1f);            // still swinging
    CHECK(bhi < 0.55f);                 // anchored at the BRIDGE value 0.2, not the authored 0.5
    CHECK(blo >= 0.2f - 1e-2f);
    CHECK_NEAR(session_audio_graph_node_param_get(s, t, nid, gp), 0.5f, 1e-4);   // authored base untouched throughout

    session_destroy(s);
    return summary("clap_modulation");
}
