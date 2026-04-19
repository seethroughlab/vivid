#include "common/perf_trend.h"
#include "test_helpers.h"
#include <cstdint>

int main() {
    // Flat series ⇒ slope ≈ 0, trend is valid once we clear the min-samples gate.
    {
        std::fprintf(stderr, "\n=== Test 1: flat series ===\n");
        constexpr uint32_t N = 64;
        float values[N];
        for (uint32_t i = 0; i < N; ++i) values[i] = 123.0f;
        auto t = vivid::compute_perf_trend(values, N, 0, /*filled=*/true, 1.0f);
        check(t.valid, "flat filled buffer produces valid trend");
        check(t.sample_count == N, "sample_count equals buf_len when filled");
        check_float(t.slope_per_sample, 0.0f, 1e-5f, "flat slope_per_sample ~= 0");
        check_float(t.slope_per_second, 0.0f, 1e-5f, "flat slope_per_second ~= 0");
        check_float(t.intercept, 123.0f, 1e-3f, "flat intercept ~= value");
        check_float(t.y_at_newest, 123.0f, 1e-3f, "flat y_at_newest ~= value");
    }

    // Monotonic ramp: +1 per sample, cadence 1s ⇒ slope_per_second ≈ 1 MB/s.
    {
        std::fprintf(stderr, "\n=== Test 2: monotonic ramp ===\n");
        constexpr uint32_t N = 32;
        float values[N];
        for (uint32_t i = 0; i < N; ++i) values[i] = 100.0f + static_cast<float>(i);
        auto t = vivid::compute_perf_trend(values, N, 0, /*filled=*/true, 1.0f);
        check(t.valid, "ramp trend is valid");
        check_float(t.slope_per_sample, 1.0f, 1e-4f, "ramp slope_per_sample ~= 1");
        check_float(t.slope_per_second, 1.0f, 1e-4f, "ramp slope_per_second ~= 1 at 1s cadence");
        check_float(t.intercept, 100.0f, 1e-3f, "intercept at oldest sample");
        check_float(t.y_at_newest, 131.0f, 1e-3f, "y_at_newest = 100 + 31");
    }

    // Half-second cadence should halve slope_per_second for the same ramp.
    {
        std::fprintf(stderr, "\n=== Test 3: sub-second cadence scaling ===\n");
        constexpr uint32_t N = 32;
        float values[N];
        for (uint32_t i = 0; i < N; ++i) values[i] = static_cast<float>(i);
        auto t = vivid::compute_perf_trend(values, N, 0, /*filled=*/true, 0.5f);
        check_float(t.slope_per_second, 2.0f, 1e-4f,
                    "slope_per_second doubles when each sample spans 0.5s");
    }

    // Too few samples ⇒ invalid, but sample_count still reports raw count.
    {
        std::fprintf(stderr, "\n=== Test 4: too few samples ===\n");
        float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        auto t = vivid::compute_perf_trend(values, /*buf_len=*/128,
                                           /*write_idx=*/4, /*filled=*/false, 1.0f);
        check(!t.valid, "fewer than kPerfTrendMinSamples ⇒ invalid");
        check(t.sample_count == 4, "sample_count still reported for partial buffer");
    }

    // Ring buffer with wrap-around: oldest sample sits after write_idx.
    {
        std::fprintf(stderr, "\n=== Test 5: wrapped ring buffer ===\n");
        constexpr uint32_t N = 16;
        float values[N];
        // Load a ramp but rotate: samples walk 5..20 starting from index 3.
        // i.e. values[3]=5, values[4]=6, ..., values[15]=17, values[0]=18, values[1]=19, values[2]=20
        for (uint32_t i = 0; i < N; ++i) {
            uint32_t logical = (i + (N - 3)) % N;
            values[i] = 5.0f + static_cast<float>(logical);
        }
        auto t = vivid::compute_perf_trend(values, N, /*write_idx=*/3,
                                           /*filled=*/true, 1.0f);
        check(t.valid, "wrapped trend is valid");
        check_float(t.slope_per_sample, 1.0f, 1e-4f,
                    "wrapped ramp walked oldest->newest yields slope 1");
        check_float(t.intercept, 5.0f, 1e-3f,
                    "wrapped intercept is oldest-sample value");
    }

    return failures ? 1 : 0;
}
