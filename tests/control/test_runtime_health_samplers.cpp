#include "runtime/core/runtime_health_samplers.h"

#include <cstdio>
#include <optional>
#include "test_helpers.h"

using vivid::RuntimeHealthSamplers;

int main() {
    {
        std::fprintf(stderr, "\n=== Test 1: Empty sampler → all queries inactive ===\n");
        RuntimeHealthSamplers s;
        check(!s.audio_silence_active(0.0), "empty: silence inactive");
        check(!s.visual_black_active(0.0), "empty: black inactive");
        check(s.audio_window_seconds(0.0) == 0.0, "empty: window=0");
        check(s.visual_window_seconds(0.0) == 0.0, "empty: visual window=0");
    }

    {
        std::fprintf(stderr, "\n=== Test 2: Below min-samples threshold → silence inactive ===\n");
        RuntimeHealthSamplers s;
        for (int i = 0; i < 10; ++i) {
            s.sample(i * 0.016, 0.0f, std::nullopt);
        }
        check(!s.audio_silence_active(10 * 0.016),
              "10 silent samples (< kMinSamples) → inactive");
    }

    {
        std::fprintf(stderr, "\n=== Test 3: 30 silent samples → silence active ===\n");
        RuntimeHealthSamplers s;
        const int n = 30;
        for (int i = 0; i < n; ++i) {
            s.sample(i * 0.016, 0.0f, std::nullopt);
        }
        const double now = (n - 1) * 0.016;
        check(s.audio_silence_active(now), "30 silent samples → active");
        check(s.audio_window_seconds(now) > 0.4 &&
              s.audio_window_seconds(now) < 0.6,
              "window_seconds ≈ 0.5");
    }

    {
        std::fprintf(stderr, "\n=== Test 4: 30 silent + 1 loud → silence inactive ===\n");
        RuntimeHealthSamplers s;
        for (int i = 0; i < 30; ++i) s.sample(i * 0.016, 0.0f, std::nullopt);
        s.sample(30 * 0.016, 0.5f, std::nullopt);
        check(!s.audio_silence_active(30 * 0.016),
              "any sample above threshold breaks silence");
    }

    {
        std::fprintf(stderr, "\n=== Test 5: All samples out of window → silence inactive ===\n");
        RuntimeHealthSamplers s;
        for (int i = 0; i < 30; ++i) s.sample(i * 0.016, 0.0f, std::nullopt);
        // Now query at far-future time so all samples are outside the window.
        check(!s.audio_silence_active(/*now=*/100.0, /*window=*/5.0),
              "samples outside window → inactive");
    }

    {
        std::fprintf(stderr, "\n=== Test 6: Visual: never sampled → black inactive ===\n");
        RuntimeHealthSamplers s;
        for (int i = 0; i < 100; ++i) s.sample(i * 0.016, 0.0f, std::nullopt);
        check(!s.visual_black_active(100 * 0.016),
              "control-only graph (never sampled visual) → not_applicable");
        check(s.visual_window_seconds(100 * 0.016) == 0.0,
              "visual window remains 0 when never sampled");
    }

    {
        std::fprintf(stderr, "\n=== Test 7: Visual: 30 black samples → black active ===\n");
        RuntimeHealthSamplers s;
        for (int i = 0; i < 30; ++i) {
            s.sample(i * 0.016, 0.5f, std::optional<float>(2.0f));
        }
        check(s.visual_black_active(29 * 0.016),
              "30 dim samples (< 4 brightness) → black active");
    }

    {
        std::fprintf(stderr, "\n=== Test 8: Visual: bright sample breaks black ===\n");
        RuntimeHealthSamplers s;
        for (int i = 0; i < 30; ++i) {
            s.sample(i * 0.016, 0.0f, std::optional<float>(1.0f));
        }
        s.sample(30 * 0.016, 0.0f, std::optional<float>(50.0f));
        check(!s.visual_black_active(30 * 0.016),
              "any bright sample breaks black");
    }

    {
        std::fprintf(stderr, "\n=== Test 9: Ring wraparound — only recent samples count ===\n");
        RuntimeHealthSamplers s;
        const int n = static_cast<int>(RuntimeHealthSamplers::kCapacity) + 50;
        // First half loud, second half silent.
        for (int i = 0; i < n; ++i) {
            float peak = (i < n / 2) ? 1.0f : 0.0f;
            s.sample(i * 0.016, peak, std::nullopt);
        }
        // After wraparound, only the last kCapacity samples remain — and the
        // last 50% (about half of capacity) are silent. Query at the latest
        // timestamp with a window that covers only the silent tail.
        const double now = (n - 1) * 0.016;
        check(s.audio_silence_active(now, /*window=*/0.5),
              "wrapped ring: recent silent tail → silence active");
    }

    {
        std::fprintf(stderr, "\n=== Test 10: clear() resets state ===\n");
        RuntimeHealthSamplers s;
        for (int i = 0; i < 30; ++i) s.sample(i * 0.016, 0.0f, std::optional<float>(1.0f));
        check(s.audio_silence_active(29 * 0.016), "active before clear");
        s.clear();
        check(!s.audio_silence_active(29 * 0.016), "inactive after clear");
        check(!s.visual_black_active(29 * 0.016), "visual inactive after clear");
        check(s.audio_window_seconds(29 * 0.016) == 0.0, "window=0 after clear");
    }

    if (failures == 0) {
        std::fprintf(stderr, "\nAll tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "\n%d failure(s).\n", failures);
    return 1;
}
