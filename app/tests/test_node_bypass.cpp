// ADR-0033 Phase 3: node BYPASS through the real session executor (session_process). The pure-core
// test (test_audio_graph) proves the compile+run semantics numerically; this proves the same flag is
// honored on the engine path (run_track_graph / process_step) that the app actually renders through:
//   - a bypassed EFFECT passes its input through (audio keeps flowing — not silence);
//   - a bypassed SOURCE/instrument gates to silence;
//   - set_node_bypass flips the persisted state and recompiles + republishes live.
//
// Each audio assertion renders from its own session right after a fresh scene launch: TestTone is a
// decaying voice and a hard bypass legitimately drops a held note-on, so measuring one bypass STATE
// per clean launch is the stable shape (exact restore-after-toggle is the pure-core test's job). The
// track setup mirrors test_session_executor's working case (set_track_audio_instrument + add effect).
//
// macOS-only (full-app configure), like test_session_executor: the engine reaches CoreFoundation.
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"   // register_builtin_audio_ops
#include "gpu/op_runtime.h"            // vivid::OpRegistry
#include "midi/midi_clip.h"            // ClipNote
#include "test_helpers.h"

#include <vector>
#include <cmath>
#include <algorithm>

using namespace vivid::session;

static float render_span(Session* s, uint32_t sr, double bpm, double& beats, int blocks, uint32_t frames) {
    std::vector<float> out(static_cast<size_t>(frames) * 2, 0.f);
    const double beats_per_block = static_cast<double>(frames) / sr * (bpm / 60.0);
    float peak = 0.f;
    for (int b = 0; b < blocks; ++b) {
        std::fill(out.begin(), out.end(), 0.f);
        session_process(s, out.data(), frames, sr, bpm, beats, /*beats_per_bar*/4, /*playing*/true, false);
        for (float v : out) peak = std::max(peak, std::fabs(v));
        beats += beats_per_block;
    }
    return peak;
}

static ClipNote note(int pitch, double dur = 100.0, float vel = 0.9f) {
    ClipNote n{}; n.pitch = pitch; n.start = 0.0; n.dur = dur; n.vel = vel; return n;
}

// First graph-node id of the given kind (0 instrument / 1 effect), or -1.
static int node_id_of_kind(Session* s, int t, int kind) {
    for (int i = 0, n = session_track_audio_graph_node_count(s, t); i < n; ++i)
        if (session_track_audio_graph_node_kind(s, t, i) == kind)
            return session_track_audio_graph_node_id(s, t, i);
    return -1;
}

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000, frames = 256;
    const int blocks = 200;   // matches test_session_executor's audible window for TestTone -> Bitcrush

    // --- 1. EFFECT bypass = passthrough. Two fresh TestTone -> Bitcrush -> Output tracks driven by the
    //        same clip: one with the effect LIVE, one BYPASSED. Both must render audio (a bypassed
    //        effect skips the op, not the signal). set_node_bypass also flips the queryable state. ---
    for (int by = 0; by <= 1; ++by) {
        int t = -1;
        Session* s = session_create(sr);
        session_set_op_registry(s, &reg);
        t = session_add_graph_track(s, "T");
        CHECK(t >= 0);
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        CHECK(session_add_audio_effect(s, t, "Bitcrush") >= 0);   // returns a CHAIN index, not a node id
        const int fx = node_id_of_kind(s, t, 1);                  // the effect's graph NODE id (what bypass takes)
        CHECK(fx >= 0);
        ClipNote n = note(60);
        session_set_clip(s, t, 0, &n, 1, 100.0);
        if (by) {
            CHECK(session_audio_graph_set_node_bypass(s, t, fx, 1) == 1);
            CHECK(session_audio_graph_node_bypassed(s, t, fx) == 1);
        } else {
            CHECK(session_audio_graph_node_bypassed(s, t, fx) == 0);
        }
        session_launch_scene(s, 0);
        double beats = 0.0;
        CHECK(render_span(s, sr, 120.0, beats, blocks, frames) > 0.01f);   // audio flows either way
        session_destroy(s);
    }

    // --- 2. SOURCE bypass = silence. The instrument is bypassed BEFORE launch, so the note-on lands
    //        while it is gated: no audio, no voice. Un-bypassed, the same setup sounds (case 1, by=0). ---
    {
        int t = -1;
        Session* s = session_create(sr);
        session_set_op_registry(s, &reg);
        t = session_add_graph_track(s, "T");
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        const int inst = node_id_of_kind(s, t, 0);   // the instrument node
        CHECK(inst >= 0);
        ClipNote n = note(60);
        session_set_clip(s, t, 0, &n, 1, 100.0);
        CHECK(session_audio_graph_set_node_bypass(s, t, inst, 1) == 1);
        CHECK(session_audio_graph_node_bypassed(s, t, inst) == 1);
        session_launch_scene(s, 0);
        double beats = 0.0;
        CHECK(render_span(s, sr, 120.0, beats, blocks, frames) < 1e-4f);   // bypassed source -> silence

        // Un-bypass flips the state back (queryable); a fresh launch would sound again (case 1, by=0).
        CHECK(session_audio_graph_set_node_bypass(s, t, inst, 0) == 1);
        CHECK(session_audio_graph_node_bypassed(s, t, inst) == 0);
        session_destroy(s);
    }

    return vivid::test::summary("test_node_bypass");
}
