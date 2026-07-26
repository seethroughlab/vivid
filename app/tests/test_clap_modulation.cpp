// ADR-0034 Phase 1: control-edge modulation reaches a CLAP plugin param. Loads the in-tree CLAP
// fixture as an effect, wires a fast LFO onto its "gain", renders, and proves — through the real CLAP
// host path — that (a) wiring captured the plugin's current value as the authored base, (b) the
// plugin's live value (resolved) SWINGS under modulation, and (c) the authored base never moves, so
// save/undo stay stable while a modulator sweeps a plugin param.
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"
#include "gpu/op_runtime.h"
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
    const int nid = load_fixture(s, t);
    CHECK(nid >= 0);
    if (nid < 0) { session_destroy(s); return summary("clap_modulation"); }
    const int gp = param_by_name(s, t, nid, "gain");
    CHECK(gp >= 0);

    // A fast free-running LFO so a few full cycles fit the render window.
    const int lfo = session_audio_graph_add_mod_op(s, t, "LFO");
    CHECK(lfo >= 0);
    if (const int rate = param_by_name(s, t, lfo, "rate"); rate >= 0)
        session_audio_graph_node_param_set(s, t, lfo, rate, 12.0f);   // 12 Hz

    // Wire LFO -> gain at amount 0.3 (unipolar), so resolved = base + 0.3*s ∈ [0.5, 0.8] — a clean
    // swing with no clamp saturation.
    const int ok = session_audio_graph_connect_control(s, t, lfo, nid, gp, /*amount*/0.3f, /*curve*/0.f, /*invert*/0, /*bipolar*/0);
    CHECK(ok == 1);
    CHECK(session_audio_graph_node_param_wired(s, t, nid, gp) == 1);
    // Capture-on-wire: the plugin's current gain (default 0.5) became the authored base.
    CHECK_NEAR(session_audio_graph_node_param_get(s, t, nid, gp), 0.5f, 1e-4);

    std::vector<float> out(static_cast<size_t>(frames) * 2, 0.f);
    double beats = 0.0; const double per = static_cast<double>(frames) / sr * 2.0;
    float rmin = 1e9f, rmax = -1e9f;
    bool base_held = true;
    for (int b = 0; b < 96; ++b) {
        std::fill(out.begin(), out.end(), 0.f);
        session_process(s, out.data(), frames, sr, 120.0, beats, 4, true, false);
        beats += per;
        const float r = session_audio_graph_node_param_resolved(s, t, nid, gp);
        rmin = std::min(rmin, r); rmax = std::max(rmax, r);
        if (std::fabs(session_audio_graph_node_param_get(s, t, nid, gp) - 0.5f) > 1e-4f) base_held = false;
    }

    CHECK(base_held);                 // the authored base never moved under the sweeping modulator
    CHECK(rmax - rmin > 0.15f);        // the plugin's live gain genuinely SWUNG (modulation delivered)
    CHECK(rmin >= 0.5f - 1e-3f);       // ...within the expected [0.5, 0.8] window (base + 0.3*s), no saturation
    CHECK(rmax <= 0.8f + 1e-3f);

    session_destroy(s);
    return summary("clap_modulation");
}
