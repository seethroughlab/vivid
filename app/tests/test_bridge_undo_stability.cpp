// ADR-0030 Phase 3: save/undo stay stable under a MOVING frame-bridge override. Persistence and the
// undo projection serialize a graph node's param via session_audio_graph_node_param_get (persist.cpp
// reads exactly that). This proves that getter keeps returning the AUTHORED base while a bridge
// override (deliver) drives the param — even as the override moves — so a save or undo snapshot taken
// mid-automation records the user's value, not the automation. Meanwhile _resolved and the rendered
// audio follow the override, and clearing it returns everything to the base. Session-level, native
// ops only (deterministic; no plugin, no persist link needed). macOS/app-ON tier, like the executor.
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"   // register_builtin_audio_ops
#include "gpu/op_runtime.h"            // vivid::OpRegistry
#include "midi/midi_clip.h"            // ClipNote
#include "test_helpers.h"

#include <vector>
#include <cmath>
#include <algorithm>
#include <string>

using namespace vivid::session;
using namespace vivid::test;

// Peak |sample| over `blocks` blocks, advancing the beat clock like the audio callback.
static float render_peak(Session* s, uint32_t sr, double& beats, int blocks, uint32_t frames) {
    std::vector<float> out(static_cast<size_t>(frames) * 2, 0.f);
    const double per_block = static_cast<double>(frames) / sr * (120.0 / 60.0);
    float peak = 0.f;
    for (int b = 0; b < blocks; ++b) {
        std::fill(out.begin(), out.end(), 0.f);
        session_process(s, out.data(), frames, sr, 120.0, beats, 4, /*playing*/true, false);
        for (float v : out) peak = std::max(peak, std::fabs(v));
        beats += per_block;
    }
    return peak;
}

// The TestTone instrument node's stable id + its "gain" param index (param 0).
static int find_gain_node(Session* s, int t, int& gain_param) {
    const int nn = session_track_audio_graph_node_count(s, t);
    for (int i = 0; i < nn; ++i) {
        const int nid = session_track_audio_graph_node_id(s, t, i);
        const int pc = session_audio_graph_node_param_count(s, t, nid);
        for (int p = 0; p < pc; ++p)
            if (std::string(session_audio_graph_node_param_name(s, t, nid, p)) == "gain") { gain_param = p; return nid; }
    }
    gain_param = -1; return -1;
}

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000, frames = 256;

    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    const int t = session_add_graph_track(s, "T");
    CHECK(t >= 0);
    CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
    ClipNote n{}; n.pitch = 69; n.start = 0.0; n.dur = 100000.0; n.vel = 1.0f;   // long held note
    session_set_clip(s, t, 0, &n, 1, 100000.0);
    session_launch_scene(s, 0);

    int gp = -1;
    const int nid = find_gain_node(s, t, gp);
    CHECK(nid >= 0);
    CHECK(gp >= 0);

    // Author a base gain. This is the value save/undo must keep seeing.
    session_audio_graph_node_param_set(s, t, nid, gp, 0.4f);
    CHECK_NEAR(session_audio_graph_node_param_get(s, t, nid, gp), 0.4f, 1e-6);      // base
    CHECK_NEAR(session_audio_graph_node_param_resolved(s, t, nid, gp), 0.4f, 1e-6); // no override → resolved == base

    double beats = 0.0;
    const float pk_base = render_peak(s, sr, beats, 8, frames);   // amplitude ≈ 0.25 * gain

    // The frame bridge starts driving the param (a mapping just connected).
    session_audio_graph_node_param_deliver(s, t, nid, gp, 0.9f);
    CHECK_NEAR(session_audio_graph_node_param_get(s, t, nid, gp), 0.4f, 1e-6);      // AUTHORED base UNCHANGED
    CHECK_NEAR(session_audio_graph_node_param_resolved(s, t, nid, gp), 0.9f, 1e-6); // resolved follows the override
    const float pk_ovr = render_peak(s, sr, beats, 8, frames);
    CHECK(pk_ovr > pk_base * 1.5f);   // the override is actually heard (louder)

    // The mapping MOVES (a modulator sweeps). Base must still read the authored value at every step —
    // this is what keeps a save / undo snapshot taken mid-automation stable.
    for (int i = 0; i < 16; ++i) {
        session_audio_graph_node_param_deliver(s, t, nid, gp, 0.1f + 0.8f * (i / 15.0f));
        CHECK_NEAR(session_audio_graph_node_param_get(s, t, nid, gp), 0.4f, 1e-6);  // base never moves
    }

    // Mapping disconnects → override cleared → node returns to the authored base.
    session_audio_graph_node_param_override_clear(s, t, nid, gp);
    CHECK_NEAR(session_audio_graph_node_param_get(s, t, nid, gp), 0.4f, 1e-6);
    CHECK_NEAR(session_audio_graph_node_param_resolved(s, t, nid, gp), 0.4f, 1e-6); // resolved back to base
    const float pk_clr = render_peak(s, sr, beats, 8, frames);
    CHECK_NEAR(pk_clr, pk_base, 0.02f);   // audibly back to the base level

    session_destroy(s);
    return summary("bridge_undo_stability");
}
