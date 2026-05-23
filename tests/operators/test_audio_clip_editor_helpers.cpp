#include "audio_clip_editor_shared.h"

#include <cmath>
#include <cstdio>
#include <string>

#include "test_helpers.h"

int main() {
    std::fprintf(stderr, "=== Test: AudioClip editor helpers ===\n\n");

    {
        const auto b = audio_clip_ed::effective_loop_bounds(0.25f, 0.75f, 0.0f, 1.0f);
        check(std::fabs(b.start - 0.25f) < 1e-6f, "loop start clamps to clip start");
        check(std::fabs(b.end - 0.75f) < 1e-6f, "loop end clamps to clip end");
    }

    {
        const float d = audio_clip_ed::pixel_delta_to_norm(100.0f, 0.25f, 1000.0f);
        check(std::fabs(d - 0.025f) < 1e-6f, "pixel delta respects zoomed viewport size");
    }

    {
        const float v = audio_clip_ed::drag_clip_start(0.2f, 0.6f, 0.7f);
        check(std::fabs(v - 0.69f) < 1e-6f, "clip start keeps minimum span before clip end");
    }

    {
        const auto b = audio_clip_ed::drag_loop_body(0.3f, 0.5f, 0.5f, 0.25f, 0.75f);
        check(std::fabs(b.start - 0.55f) < 1e-6f, "loop body clamps to clip end");
        check(std::fabs(b.end - 0.75f) < 1e-6f, "loop body preserves length at right edge");
    }

    {
        const auto b = audio_clip_ed::drag_loop_body(0.25f, 0.75f, -0.5f, 0.4f, 0.6f);
        check(b.start >= 0.4f - 1e-6f, "oversized loop body start clamps to clip start");
        check(b.end <= 0.6f + 1e-6f, "oversized loop body end clamps to clip end");
        check(b.end > b.start, "oversized loop body remains non-inverted");
    }

    {
        auto pts = audio_clip_ed::parse_warp_points("0:0 48000:1.0 96000:2.0");
        check(pts.size() == 3, "legacy warp shorthand parses");
        check(pts[1].source_sample == 48000 && std::fabs(pts[1].beat - 1.0) < 1e-9,
              "legacy warp shorthand stores sample and beat");
        auto compiled = audio_clip_ed::compile_warp_points(pts, 0, 96000, 2.0);
        const double src = audio_clip_ed::source_for_warp_beat(compiled, 1.5);
        check(std::fabs(src - 72000.0) < 1e-6, "warp beat maps to interpolated source");
    }

    {
        auto pts = audio_clip_ed::parse_warp_points(
            R"([{"source_sample":0,"beat":0.0},{"source_sample":100,"beat":2.0}])");
        check(pts.size() == 2, "warp JSON parses");
        check(std::fabs(audio_clip_ed::warp_total_beats(pts) - 2.0) < 1e-9,
              "warp total beats uses endpoints");
    }

    {
        auto pts = audio_clip_ed::parse_warp_points(
            R"([{"source_sample":100,"beat":2.0},{"source_sample":50,"beat":3.0},{"source_sample":150,"beat":1.0}])");
        check(pts.size() == 3, "warp sanitizer preserves sortable markers");
        check(pts[0].source_sample == 50 && pts[1].source_sample == 100,
              "warp sanitizer sorts by source sample");
        check(pts[2].beat >= pts[1].beat,
              "warp sanitizer clamps non-monotonic beats instead of dropping markers");
        const auto serialized = audio_clip_ed::serialize_warp_points(pts);
        check(serialized.find("source_sample") != std::string::npos,
              "warp serialization writes canonical JSON objects");
    }

    {
        const double f0 = audio_clip_ed::source_for_normalized_phase(0.0, 10, 20, false);
        const double f1 = audio_clip_ed::source_for_normalized_phase(1.0, 10, 20, false);
        const double r0 = audio_clip_ed::source_for_normalized_phase(0.0, 10, 20, true);
        const double r1 = audio_clip_ed::source_for_normalized_phase(1.0, 10, 20, true);
        check(std::fabs(f0 - 10.0) < 1e-9 && std::fabs(f1 - 19.0) < 1e-9,
              "normalized phase maps forward endpoints inside the clip");
        check(std::fabs(r0 - 19.0) < 1e-9 && std::fabs(r1 - 10.0) < 1e-9,
              "normalized phase maps reverse endpoints without underflow");
    }

    {
        check(std::fabs(audio_clip_ed::equal_power_fade_in(0.0f)) < 1e-6f,
              "fade in starts silent");
        check(std::fabs(audio_clip_ed::equal_power_fade_out(1.0f)) < 1e-5f,
              "fade out ends silent");
        check(std::fabs(audio_clip_ed::reverse_source_position(2.0, 0, 9) - 7.0) < 1e-6,
              "reverse source position mirrors inside region");
    }

    {
        check(std::fabs(audio_clip_ed::next_quantized_beat(1.25, 4, 1) - 2.0) < 1e-9,
              "beat quantize targets next beat");
        check(std::fabs(audio_clip_ed::next_quantized_beat(5.0, 4, 2) - 8.0) < 1e-9,
              "bar quantize targets next bar");
    }

    {
        std::vector<float> l(256, 0.0f), r(256, 0.0f);
        l[20] = r[20] = 1.0f;
        l[160] = r[160] = 0.8f;
        const auto tr = audio_clip_ed::detect_transients(l, r, 1000, 1.0f);
        check(tr.size() >= 2, "transient detector finds separated impulses");
        const auto slices = audio_clip_ed::compile_slices(1, tr, {}, 0, 255);
        check(slices.size() >= 2, "transient slice compiler creates regions");
    }

    {
        const auto manual = audio_clip_ed::parse_sample_points(R"([0,64,128])");
        const auto slices = audio_clip_ed::compile_slices(2, {}, manual, 0, 256);
        check(slices.size() == 3, "manual slice compiler creates expected regions");
        const auto even = audio_clip_ed::compile_slices(3, {}, {}, 0, 160);
        check(even.size() == 16, "even16 slice compiler creates sixteen slices");
        check(audio_clip_ed::serialize_sample_points({128, 0, 64, 64}) == "[0,64,128]",
              "sample point serialization sorts and deduplicates");
    }

    return 0;
}
