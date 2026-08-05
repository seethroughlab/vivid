// ADR-0033 Phase 4: node SOLO / audition through the real session executor (session_process). The
// pure-core test (test_audio_graph) proves the signal-path BFS; this proves the derived audible mask is
// honored on the engine path — soloing a node silences sibling branches (zeroes their output) while its
// own path keeps sounding, and clearing solo restores instantly with no dropped note-on.
//
// Setup: two instruments fan into the track output, split by key range so a single clip note (pitch 60)
// drives exactly ONE of them:
//   - srcHi ranged [72,127]  -> note 60 filtered out -> silent on its own
//   - srcLo ranged [0,71]    -> note 60 passes        -> sounds
// Soloing srcHi (the silent one) mutes its sibling srcLo, so the track goes silent — that silence is the
// proof that solo muted the sibling that WOULD have sounded.
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

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000, frames = 256;
    const int blocks = 200;

    int t = -1;
    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    t = session_add_graph_track(s, "T");
    CHECK(t >= 0);
    const int srcHi = session_audio_graph_add_source(s, t, "TestTone");   // key-split high: gets no note
    const int srcLo = session_audio_graph_add_source(s, t, "TestTone");   // key-split low: gets note 60
    CHECK(srcHi >= 0 && srcLo >= 0);
    session_audio_graph_node_key_range_set(s, t, srcHi, 72, 127);
    session_audio_graph_node_key_range_set(s, t, srcLo, 0, 71);
    ClipNote n = note(60);
    session_set_clip(s, t, 0, &n, 1, 100.0);
    session_launch_scene(s, 0);
    double beats = 0.0;

    // --- 1. Baseline: srcLo sounds (srcHi filtered out). ---
    CHECK(render_span(s, sr, 120.0, beats, blocks, frames) > 0.01f);

    // --- 2. Solo srcHi (the silent one): its sibling srcLo is muted, srcHi makes no sound -> silence.
    //        This is the sibling-mute proof. ---
    CHECK(session_audio_graph_set_node_solo(s, t, srcHi, 1) == 1);
    CHECK(session_audio_graph_node_soloed(s, t, srcHi) == 1);
    CHECK(render_span(s, sr, 120.0, beats, blocks, frames) < 1e-4f);

    // --- 3. Clear solo: srcLo returns immediately (its voice was held — solo only zeroed the output,
    //        it never dropped the note-on). ---
    CHECK(session_audio_graph_set_node_solo(s, t, srcHi, 0) == 1);
    CHECK(session_audio_graph_node_soloed(s, t, srcHi) == 0);
    CHECK(render_span(s, sr, 120.0, beats, blocks, frames) > 0.01f);

    // --- 4. Solo srcLo (the one actually sounding): its own path stays audible. ---
    CHECK(session_audio_graph_set_node_solo(s, t, srcLo, 1) == 1);
    CHECK(render_span(s, sr, 120.0, beats, blocks, frames) > 0.01f);

    // --- 5. Also solo srcHi (both soloed): the union keeps srcLo audible -> still sounds. ---
    CHECK(session_audio_graph_set_node_solo(s, t, srcHi, 1) == 1);
    CHECK(render_span(s, sr, 120.0, beats, blocks, frames) > 0.01f);

    session_destroy(s);
    return vivid::test::summary("test_node_solo");
}
