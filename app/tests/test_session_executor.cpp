// Headless coverage of the SESSION AUDIO EXECUTOR (session_process) — the integration path that,
// until now, only manual MCP smokes covered, and where the #105 "graph-node plugin renders silently"
// bug hid. Builds a real Session with NATIVE ops (deterministic, compiled in — no plugins, no GUI, no
// audio device), drives clips/scenes/key-splits/FX/transport by calling session_process directly, and
// asserts on the rendered output buffer.
//
// macOS-only: the audio engine reaches CoreFoundation (VST3 bundle loading via vst3_host_common.h), so
// this is built only in the full-app configure (VIVID_BUILD_APP=ON), not the portable Linux tests tier.
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"   // register_builtin_audio_ops
#include "gpu/op_runtime.h"            // vivid::OpRegistry
#include "midi/midi_clip.h"            // ClipNote
#include "test_helpers.h"

#include <vector>
#include <cmath>
#include <algorithm>

using namespace vivid::session;

// Render `blocks` blocks of `frames` frames, advancing the shared beat clock the way the audio
// callback does. Returns the peak absolute sample across the run (interleaved L/R). `beats` is
// threaded by reference so successive spans are continuous (needed for bar-quantized scene switches).
static float render_span(Session* s, uint32_t sr, double bpm, double& beats, int blocks, uint32_t frames,
                         bool playing = true, bool release_all = false) {
    std::vector<float> out(static_cast<size_t>(frames) * 2, 0.f);
    const double beats_per_block = static_cast<double>(frames) / sr * (bpm / 60.0);
    float peak = 0.f;
    for (int b = 0; b < blocks; ++b) {
        std::fill(out.begin(), out.end(), 0.f);
        session_process(s, out.data(), frames, sr, bpm, beats, /*beats_per_bar*/4, playing, release_all);
        for (float v : out) peak = std::max(peak, std::fabs(v));
        beats += beats_per_block;
    }
    return peak;
}

static Session* make_session(vivid::OpRegistry& reg, uint32_t sr, int& t) {
    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    t = session_add_graph_track(s, "T");
    return s;
}
// A one-shot clip note. `dur`/`length` long enough to sustain across a whole test span (no loop retrig).
static ClipNote note(int pitch, double dur = 100.0, float vel = 0.9f) {
    ClipNote n{}; n.pitch = pitch; n.start = 0.0; n.dur = dur; n.vel = vel; return n;
}

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000, frames = 256;

    // --- 1. A native instrument driven by a clip renders audio (core path: clip scheduler -> note
    //        routing -> instrument -> output). ---
    {
        int t = -1; Session* s = make_session(reg, sr, t);
        CHECK(t >= 0);
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        ClipNote n = note(60); session_set_clip(s, t, 0, &n, 1, 100.0);
        session_launch_scene(s, 0);
        double beats = 0.0;
        CHECK(render_span(s, sr, 120.0, beats, 200, frames) > 0.01f);   // note reached the instrument
        session_destroy(s);
    }

    // --- 2. The SAME instrument with an EMPTY clip stays silent — proves it is note-DRIVEN and that no
    //        notes => no sound. The #105-class assertion (a source that renders regardless of, or never
    //        receives, its notes would fail here). ---
    {
        int t = -1; Session* s = make_session(reg, sr, t);
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        session_set_clip(s, t, 0, nullptr, 0, 100.0);
        session_launch_scene(s, 0);
        double beats = 0.0;
        CHECK(render_span(s, sr, 120.0, beats, 200, frames) < 1e-4f);   // no notes -> silence
        session_destroy(s);
    }

    // --- 3. Key-split: a source ranged [72,127] filters out a pitch-60 clip note -> silence. Exercises
    //        run_track_graph's per-source key filter (filter_notes_by_range). ---
    {
        int t = -1; Session* s = make_session(reg, sr, t);
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        const int nid = session_track_audio_graph_node_id(s, t, 0);
        session_audio_graph_node_key_range_set(s, t, nid, 72, 127);
        ClipNote n = note(60); session_set_clip(s, t, 0, &n, 1, 100.0);
        session_launch_scene(s, 0);
        double beats = 0.0;
        CHECK(render_span(s, sr, 120.0, beats, 200, frames) < 1e-4f);   // 60 is below [72,127]
        session_destroy(s);
    }

    // --- 4. Scene switching: a bar-quantized launch to an EMPTY scene flushes the outgoing clip's held
    //        notes and re-arms the scheduler -> the track goes silent. Pins the scene-switch path (the
    //        scheduler re-arm that #105's debugging confusion first surfaced). ---
    {
        int t = -1; Session* s = make_session(reg, sr, t);
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        ClipNote n = note(60);
        session_set_clip(s, t, 0, &n, 1, 100.0);          // scene 0: a long sustained note
        session_set_clip(s, t, 1, nullptr, 0, 100.0);     // scene 1: empty
        session_launch_scene(s, 0);
        double beats = 0.0;
        CHECK(render_span(s, sr, 120.0, beats, 100, frames) > 0.01f);   // scene 0 sounding
        session_launch_scene(s, 1);
        render_span(s, sr, 120.0, beats, 400, frames);                  // cross a bar -> switch applies
        CHECK(render_span(s, sr, 120.0, beats, 100, frames) < 1e-4f);   // now in the empty scene -> silence
        session_destroy(s);
    }

    // --- 5. A native FX in the chain still passes audio: instrument -> Bitcrush -> output renders sound.
    //        Exercises the executor's effect-node dispatch (NativeFx) + graph rebuild on add_effect. ---
    {
        int t = -1; Session* s = make_session(reg, sr, t);
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        CHECK(session_add_audio_effect(s, t, "Bitcrush") >= 0);
        ClipNote n = note(60); session_set_clip(s, t, 0, &n, 1, 100.0);
        session_launch_scene(s, 0);
        double beats = 0.0;
        CHECK(render_span(s, sr, 120.0, beats, 200, frames) > 0.01f);   // audio flows through the FX
        session_destroy(s);
    }

    // --- 6. Play -> stop: on the stop edge (release_all) the scheduler flushes held notes, so after the
    //        transport stops the track falls silent (no stuck voices). ---
    {
        int t = -1; Session* s = make_session(reg, sr, t);
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        ClipNote n = note(60); session_set_clip(s, t, 0, &n, 1, 100.0);
        session_launch_scene(s, 0);
        double beats = 0.0;
        CHECK(render_span(s, sr, 120.0, beats, 100, frames) > 0.01f);        // playing -> sound
        render_span(s, sr, 120.0, beats, 1, frames, /*playing*/false, /*release_all*/true);  // the stop edge
        CHECK(render_span(s, sr, 120.0, beats, 50, frames, /*playing*/false) < 1e-4f);  // stopped -> silent
        session_destroy(s);
    }

    return vivid::test::summary("test_session_executor");
}
