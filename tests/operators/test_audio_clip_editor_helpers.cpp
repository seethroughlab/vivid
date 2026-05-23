#include "audio_clip_editor_shared.h"

#include <cmath>
#include <cstdio>

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

    return 0;
}
