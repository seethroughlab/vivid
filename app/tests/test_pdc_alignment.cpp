// ADR-0032 Phase E1.3: end-to-end PDC verification in a real session. Two proofs:
//   A. master_mix ACTUALLY applies the per-track delay ring — a clip-driven TestTone track's master
//      output, rendered with PDC on + a manual N-sample track delay, is the PDC-off output shifted by
//      exactly N samples (nothing else moves).
//   B. the CLASSIFICATION reads a real plugin's reported latency — the in-tree CLAP fixture, built to
//      advertise CLAP_EXT_LATENCY=N (via VIVID_TEST_CLAP_LATENCY) AND delay its audio by N, loaded on a
//      track, is read by Phase B as N, and pdc_recompute delays the OTHER track by N to align them.
// Together with test_pdc_ring (the ring math) and test_pdc_classify (the alignment math), this closes
// the loop: reported latency -> classification -> per-track delay -> a real shifted master mix.
// macOS/app-ON tier (the engine reaches CoreFoundation to load the .clap bundle).
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"
#include "audio/pdc.h"
#include "gpu/op_runtime.h"
#include "midi/midi_clip.h"   // ClipNote
#include "test_helpers.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

using namespace vivid::session;
using namespace vivid::test;

#ifndef VIVID_TEST_CLAP_PATH
#error "VIVID_TEST_CLAP_PATH must point at the built .clap bundle"
#endif
static constexpr int kFmtCLAP = 1;   // PluginFormat::kFmtCLAP

// Render `blocks` of `frames` into one contiguous interleaved master buffer.
static std::vector<float> render_master(Session* s, uint32_t sr, uint32_t frames, int blocks) {
    std::vector<float> all(static_cast<size_t>(frames) * blocks * 2, 0.f);
    double beats = 0.0;
    const double bps = 120.0 / 60.0;
    std::vector<float> blk(static_cast<size_t>(frames) * 2, 0.f);
    for (int b = 0; b < blocks; ++b) {
        std::fill(blk.begin(), blk.end(), 0.f);
        session_process(s, blk.data(), frames, sr, 120.0, beats, 4, /*playing*/true, false);
        std::copy(blk.begin(), blk.end(), all.begin() + static_cast<size_t>(b) * frames * 2);
        beats += frames * bps / sr;
    }
    return all;
}

// First frame index whose |L| exceeds a small threshold (the signal onset).
static int onset(const std::vector<float>& interleaved) {
    const size_t n = interleaved.size() / 2;
    for (size_t i = 0; i < n; ++i) if (std::fabs(interleaved[i * 2]) > 1e-4f) return static_cast<int>(i);
    return -1;
}

static Session* clip_tone_session(vivid::OpRegistry& reg, uint32_t sr, int& t) {
    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    t = session_add_graph_track(s, "T");
    session_set_track_audio_instrument(s, t, "TestTone");
    ClipNote n{}; n.pitch = 60; n.start = 0.0; n.dur = 100.0; n.vel = 0.9f;   // sustained from beat 0
    session_set_clip(s, t, 0, &n, 1, 100.0);
    session_launch_scene(s, 0);
    return s;
}

// Load the fixture as an effect on track t and poll the async loader until it binds.
static int load_fixture_fx(Session* s, int t) {
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

int main() {
    // Part B loads the fixture in LATENCY mode; set the env before anything loads it (Part A uses only
    // TestTone, so it is unaffected).
    const int N = 512;
    ::setenv("VIVID_TEST_CLAP_LATENCY", "512", 1);

    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000;
    const uint32_t frames = 256;
    const int blocks = 8;   // 2048 samples > N so the shifted onset is well inside the capture

    // --- A. master_mix applies the ring: PDC-off vs PDC-on + a manual N-sample delay. ---------------
    int t0 = -1;
    Session* off = clip_tone_session(reg, sr, t0);
    const std::vector<float> mix_off = render_master(off, sr, frames, blocks);
    const int on_off = onset(mix_off);
    CHECK(on_off >= 0);                     // the clip note produced audible output
    session_destroy(off);

    int t1 = -1;
    Session* on = clip_tone_session(reg, sr, t1);
    session_set_pdc_enabled(on, true);      // recompute (native track => delay 0), then override:
    session_pdc_set_track_delay(on, t1, N); // manual N-sample delay on the single track (no edits after)
    const std::vector<float> mix_on = render_master(on, sr, frames, blocks);
    const int on_on = onset(mix_on);
    CHECK(on_on >= 0);
    CHECK(on_on - on_off == N);             // the entire track output is delayed by exactly N
    // And the delayed mix equals the un-delayed mix shifted by N, sample-for-sample.
    {
        bool match = true;
        const size_t total = static_cast<size_t>(frames) * blocks;
        for (size_t i = N; i < total; ++i)
            if (std::fabs(mix_on[i * 2] - mix_off[(i - N) * 2]) > 1e-5f) { match = false; break; }
        CHECK(match);
        // The first N samples of the delayed mix are the ring's initial silence.
        bool head_silent = true;
        for (int i = 0; i < N; ++i) if (std::fabs(mix_on[i * 2]) > 1e-6f) { head_silent = false; break; }
        CHECK(head_silent);
    }
    session_destroy(on);

    // --- B. classification reads the plugin's reported latency and aligns the other track. ----------
    int b0 = -1;
    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    b0 = session_add_graph_track(s, "A");
    const int b1 = session_add_graph_track(s, "B");
    CHECK(b0 >= 0 && b1 >= 0);

    const int nid = load_fixture_fx(s, b0);
    CHECK(nid >= 0);                        // the latency fixture loaded + bound
    if (nid >= 0) {
        // A couple of blocks so the plugin is active and its latency has been read.
        (void)render_master(s, sr, frames, 2);
        CHECK(session_track_latency_samples(s, b0) == N);   // Phase B read the fixture's reported latency
        CHECK(session_track_latency_samples(s, b1) == 0);   // the native track reports none

        session_set_pdc_enabled(s, true);
        CHECK(session_pdc_applied_delay(s) == N);           // L_max over the compensable set
        CHECK(session_pdc_track_delay(s, b0) == 0);         // the latent track is the reference (no delay)
        CHECK(session_pdc_track_delay(s, b1) == N);         // the other track is pulled back N to align
        CHECK(session_pdc_tracks_compensated(s) == 2);      // both tracks are compensable
        CHECK(session_pdc_tracks_live(s) == 0);
        CHECK(session_pdc_clamped(s) == 0);

        session_set_pdc_enabled(s, false);                  // off => all delays cleared
        CHECK(session_pdc_applied_delay(s) == 0);
        CHECK(session_pdc_track_delay(s, b1) == 0);
    }
    session_destroy(s);

    return summary("test_pdc_alignment");
}
