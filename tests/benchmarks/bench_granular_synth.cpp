#include "shared/granular_dsp/granular_dsp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kFrames = 256;
constexpr int kWarmupBlocks = 32;
constexpr int kMeasureBlocks = 512;
constexpr int kRepeats = 8;

void fill_input(std::vector<float>& input, int block) {
    for (uint32_t i = 0; i < input.size(); ++i) {
        const float t = static_cast<float>(block * input.size() + i) / static_cast<float>(kSampleRate);
        input[i] = 0.4f * std::sin(2.0f * 3.14159265f * 90.0f * t)
                 + 0.2f * std::sin(2.0f * 3.14159265f * 530.0f * t)
                 + 0.08f * std::sin(2.0f * 3.14159265f * 2110.0f * t);
    }
}

struct Case {
    const char* name;
    float density;
    float grain_ms;
    float pitch;
    int window;
};

struct Measurement {
    double mean_us = 0.0;
    double stddev_us = 0.0;
    int max_active = 0;
};

double run_once(const Case& tc, int& max_active) {
    vivid::granular_dsp::Engine engine;
    vivid::granular_dsp::ProcessParams params{};
    params.position = 0.02f;
    params.pitch = tc.pitch;
    params.density = tc.density;
    params.grain_size_ms = tc.grain_ms;
    params.randomize = 0.0f;
    params.window_type = tc.window;
    params.mix = 1.0f;

    std::vector<float> input(kFrames);
    std::vector<float> output(kFrames);

    for (int block = 0; block < kWarmupBlocks; ++block) {
        fill_input(input, block);
        engine.process(input.data(), output.data(), kFrames, kSampleRate, params);
        max_active = std::max(max_active, engine.last_stats().active_grains);
    }

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kMeasureBlocks; ++block) {
        fill_input(input, block + kWarmupBlocks);
        engine.process(input.data(), output.data(), kFrames, kSampleRate, params);
        max_active = std::max(max_active, engine.last_stats().active_grains);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / 1000.0 / static_cast<double>(kMeasureBlocks);
}

Measurement run_case(const Case& tc) {
    std::vector<double> samples;
    samples.reserve(kRepeats);
    Measurement m{};
    double sum = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
        int max_active = 0;
        const double us = run_once(tc, max_active);
        samples.push_back(us);
        sum += us;
        m.max_active = std::max(m.max_active, max_active);
    }

    m.mean_us = sum / static_cast<double>(kRepeats);
    double variance = 0.0;
    for (double v : samples) {
        const double d = v - m.mean_us;
        variance += d * d;
    }
    m.stddev_us = std::sqrt(variance / static_cast<double>(kRepeats));
    return m;
}

} // namespace

int main() {
    const Case cases[] = {
        {"low", 2.0f, 60.0f, 0.0f, 0},
        {"medium", 20.0f, 80.0f, 7.0f, 1},
        {"high", 60.0f, 120.0f, -12.0f, 2},
    };

    std::printf("GranularSynth benchmark: frames=%u sample_rate=%u measure_blocks=%d repeats=%d backend=%s\n",
                kFrames,
                kSampleRate,
                kMeasureBlocks,
                kRepeats,
                vivid::granular_dsp::backend_name(vivid::granular_dsp::preferred_backend()));
    for (const auto& tc : cases) {
        const auto m = run_case(tc);
        std::printf("case=%s density=%.1f grain_ms=%.1f mean_us=%.3f±%.3f max_active=%d\n",
                    tc.name,
                    tc.density,
                    tc.grain_ms,
                    m.mean_us,
                    m.stddev_us,
                    m.max_active);
    }

    return 0;
}
