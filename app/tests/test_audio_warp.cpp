// Headless test for the pure audio-warp math (A0): warp-marker mapping, transient
// detection, auto-warp/BPM estimation, fades, and slicing. No DSP/stretcher needed.
#include "audio/audio_clip_shared.h"
#include "test_helpers.h"
#include <vector>

namespace ace = audio_clip_ed;

int main() {
    // --- source_for_warp_beat: piecewise-linear beat -> source sample ---
    {
        std::vector<ace::WarpPoint> pts = { {0, 0.0}, {48000, 4.0} };
        CHECK_NEAR(ace::source_for_warp_beat(pts, 0.0), 0.0, 1e-6);
        CHECK_NEAR(ace::source_for_warp_beat(pts, 2.0), 24000.0, 1e-6);   // midpoint
        CHECK_NEAR(ace::source_for_warp_beat(pts, 4.0), 48000.0, 1e-6);
        CHECK_NEAR(ace::source_for_warp_beat(pts, -1.0), 0.0, 1e-6);      // clamp below
        CHECK_NEAR(ace::source_for_warp_beat(pts, 9.0), 48000.0, 1e-6);   // clamp above
        std::vector<ace::WarpPoint> one = { {5000, 0.0} };
        CHECK_NEAR(ace::source_for_warp_beat(one, 3.0), 5000.0, 1e-6);    // single point
        CHECK_NEAR(ace::source_for_warp_beat({}, 1.0), 0.0, 1e-9);        // empty
        CHECK_NEAR(ace::warp_total_beats(pts), 4.0, 1e-9);
    }

    // --- compile_warp_points: inserts clip-start/end sentinels + rebases first beat to 0 ---
    {
        std::vector<ace::WarpPoint> in = { {1000, 1.0}, {2000, 2.0} };
        auto c = ace::compile_warp_points(in, /*clip_start*/0, /*clip_end*/4000, /*fallback*/4.0);
        CHECK(c.size() == 4);
        CHECK(c.front().source_sample == 0u);
        CHECK_NEAR(c.front().beat, 0.0, 1e-9);
        CHECK(c.back().source_sample == 4000u);
        CHECK_NEAR(c.back().beat, 4.0, 1e-9);
        // degenerate clip range -> empty
        CHECK(ace::compile_warp_points(in, 4000, 4000, 4.0).empty());
    }

    // --- parse/serialize round-trip (JSON) + legacy "sample:beat" form ---
    {
        std::vector<ace::WarpPoint> pts = { {0, 0.0}, {12000, 1.0}, {24000, 2.0} };
        auto js = ace::serialize_warp_points(pts);
        auto rt = ace::parse_warp_points(js);
        CHECK(rt.size() == 3);
        CHECK(rt[1].source_sample == 12000u);
        CHECK_NEAR(rt[1].beat, 1.0, 1e-9);
        auto legacy = ace::parse_warp_points("0:0.0 12000:1.0 24000:2.0");   // whitespace form
        CHECK(legacy.size() == 3);
        CHECK(legacy[2].source_sample == 24000u);
        CHECK_NEAR(legacy[2].beat, 2.0, 1e-9);
    }

    // --- detect_transients on a synthetic 120-BPM click track ---
    const uint32_t SR = 48000;
    std::vector<float> L(SR * 2, 0.f), R(SR * 2, 0.f);   // 2 seconds
    const uint32_t period = SR / 2;                       // 0.5 s = 120 BPM quarter notes
    for (uint32_t onset = 0; onset < L.size(); onset += period)
        for (uint32_t k = 0; k < 8 && onset + k < L.size(); ++k) { L[onset + k] = 1.f; R[onset + k] = 1.f; }
    auto tr = ace::detect_transients(L, R, SR, /*sensitivity*/0.5f);
    {
        CHECK(tr.size() == 4);                            // clicks at 0, 24000, 48000, 72000
        if (tr.size() == 4) {
            CHECK(tr[0].source_sample < 8u);
            CHECK(tr[1].source_sample >= period && tr[1].source_sample < period + 8u);
            CHECK(tr[3].source_sample >= 3 * period && tr[3].source_sample < 3 * period + 8u);
        }
    }

    // --- estimate_bpm + auto_warp on that click track ---
    {
        const double bpm = ace::estimate_bpm(tr, SR);
        CHECK_NEAR(bpm, 120.0, 1.0);                      // median IOI 0.5s -> 120
        auto wp = ace::auto_warp(tr, static_cast<uint32_t>(L.size()), SR, /*bpm_hint*/0.0);
        CHECK(wp.size() >= 4);
        CHECK(wp.front().source_sample == 0u);
        // beats should land on integers 0,1,2,3
        CHECK_NEAR(wp[1].beat, 1.0, 1e-6);
        CHECK_NEAR(wp.back().beat, 3.0, 1e-6);
    }

    // --- equal-power fades + reverse ---
    {
        CHECK_NEAR(ace::equal_power_fade_in(0.f), 0.0, 1e-5);
        CHECK_NEAR(ace::equal_power_fade_in(1.f), 1.0, 1e-5);
        CHECK_NEAR(ace::equal_power_fade_in(0.5f), 0.70710678, 1e-4);
        CHECK_NEAR(ace::equal_power_fade_out(0.f), 1.0, 1e-5);
        CHECK_NEAR(ace::equal_power_fade_out(1.f), 0.0, 1e-5);
        CHECK_NEAR(ace::reverse_source_position(10.0, 0, 100), 90.0, 1e-9);
    }

    // --- compile_slices: grid mode makes 16 equal slices ---
    {
        auto sl = ace::compile_slices(/*mode grid*/3, {}, {}, /*start*/0, /*end*/16000);
        CHECK(sl.size() == 16);
        CHECK(sl.front().start == 0u);
        CHECK(sl.back().end == 16000u);
        // transient mode uses the detected onsets as slice boundaries
        auto st = ace::compile_slices(/*mode transient*/1, tr, {}, 0, SR * 2);
        CHECK(st.size() >= 4);
    }

    return vivid::test::summary("test_audio_warp");
}
