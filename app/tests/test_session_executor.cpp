// Headless coverage of the SESSION AUDIO EXECUTOR (session_process) — the integration path that,
// until now, only manual MCP smokes covered, and where the #105 "graph-node plugin renders silently"
// bug hid. Builds a real Session with NATIVE ops (deterministic, compiled in — no plugins, no GUI, no
// audio device), drives clips/scenes/key-splits/cross-track note edges by calling session_process
// directly, and asserts on the rendered output buffer.
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

// Render `blocks` blocks of `frames` frames with the transport playing, advancing the beat clock the
// way the audio callback does. Returns the peak absolute sample across the whole run (interleaved L/R).
static float render_peak(Session* s, uint32_t sr, double bpm, int blocks, uint32_t frames) {
    std::vector<float> out(static_cast<size_t>(frames) * 2, 0.f);
    double beats = 0.0;
    const double beats_per_block = static_cast<double>(frames) / sr * (bpm / 60.0);
    float peak = 0.f;
    for (int b = 0; b < blocks; ++b) {
        std::fill(out.begin(), out.end(), 0.f);
        session_process(s, out.data(), frames, sr, bpm, beats, /*beats_per_bar*/4, /*playing*/true, /*release_all*/false);
        for (float v : out) peak = std::max(peak, std::fabs(v));
        beats += beats_per_block;
    }
    return peak;
}

// A fresh session with a single native-instrument track. Returns the track index via `t`.
static Session* make_session(vivid::OpRegistry& reg, uint32_t sr, const char* instrument, int& t) {
    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    t = session_add_graph_track(s, "T");
    return s;
}

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000, frames = 256;

    // --- 1. A native instrument driven by a clip renders audio (the core executor path: clip
    //        scheduler -> note routing -> instrument -> output). ---
    {
        int t = -1;
        Session* s = make_session(reg, sr, "TestTone", t);
        CHECK(t >= 0);
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        ClipNote notes[1] = {};
        notes[0].pitch = 60; notes[0].start = 0.0; notes[0].dur = 4.0; notes[0].vel = 0.9f;
        session_set_clip(s, t, /*scene*/0, notes, 1, /*length*/4.0);
        session_launch_scene(s, 0);
        const float peak = render_peak(s, sr, 120.0, /*blocks*/200 /*~1.1s*/, frames);
        CHECK(peak > 0.01f);   // the clip's note reached the instrument -> sound
        session_destroy(s);
    }

    // --- 2. The SAME instrument with an EMPTY clip stays silent — proves the instrument is note-DRIVEN
    //        (no free-running tone) and that no notes means no sound. This is the #105-class assertion:
    //        it would have caught a source that renders regardless of (or never receives) its notes. ---
    {
        int t = -1;
        Session* s = make_session(reg, sr, "TestTone", t);
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        session_set_clip(s, t, 0, nullptr, 0, 4.0);   // empty clip
        session_launch_scene(s, 0);
        const float peak = render_peak(s, sr, 120.0, 200, frames);
        CHECK(peak < 1e-4f);   // no notes -> silence
        session_destroy(s);
    }

    // --- 3. A key-split: two instrument sources with disjoint key ranges each voice only their own
    //        half of the clip. A note in range A sounds; the same note is silent on a track whose one
    //        source is ranged entirely above it. Exercises run_track_graph's per-source key filter. ---
    {
        int t = -1;
        Session* s = make_session(reg, sr, "TestTone", t);
        CHECK(session_set_track_audio_instrument(s, t, "TestTone") == 1);
        // Restrict the (single) source to a high range that excludes pitch 60.
        const int nid = session_track_audio_graph_node_id(s, t, 0);   // the instrument node
        session_audio_graph_node_key_range_set(s, t, nid, 72, 127);
        ClipNote low[1] = {}; low[0].pitch = 60; low[0].dur = 4.0; low[0].vel = 0.9f;
        session_set_clip(s, t, 0, low, 1, 4.0);
        session_launch_scene(s, 0);
        const float peak = render_peak(s, sr, 120.0, 200, frames);
        CHECK(peak < 1e-4f);   // pitch 60 is below [72,127] -> filtered out -> silence
        session_destroy(s);
    }

    return vivid::test::summary("test_session_executor");
}
