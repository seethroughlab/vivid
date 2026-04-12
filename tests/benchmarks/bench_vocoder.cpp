#include "shared/vocoder_dsp/vocoder_dsp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr int kWarmupBlocks = 32;
constexpr int kMeasureBlocks = 512;
constexpr int kRepeats = 8;

void fill_input(std::vector<float>& modulator, std::vector<float>& carrier, int block, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(block * frames + i) / static_cast<float>(kSampleRate);
        modulator[i] = 0.38f * std::sin(2.0f * 3.14159265f * 92.0f * t)
                     + 0.24f * std::sin(2.0f * 3.14159265f * 530.0f * t)
                     + 0.10f * std::sin(2.0f * 3.14159265f * 2100.0f * t);
        carrier[i] = 0.52f * (2.0f * (t * 110.0f - std::floor(0.5f + t * 110.0f)))
                   + 0.18f * std::sin(2.0f * 3.14159265f * 440.0f * t);
    }
}

struct Case {
    uint32_t frames;
    int bands;
    float speed_ms;
    float mix;
};

struct Measurement {
    double mean_us = 0.0;
    double stddev_us = 0.0;
    int coefficient_rebuilds = 0;
};

double run_once(const Case& tc, int& coefficient_rebuilds) {
    vivid::vocoder_dsp::Engine engine;
    vivid::vocoder_dsp::ProcessParams params{};
    params.bands = tc.bands;
    params.envelope_speed_ms = tc.speed_ms;
    params.mix = tc.mix;

    std::vector<float> modulator(tc.frames);
    std::vector<float> carrier(tc.frames);
    std::vector<float> output(tc.frames);

    for (int block = 0; block < kWarmupBlocks; ++block) {
        fill_input(modulator, carrier, block, tc.frames);
        engine.process(modulator.data(), carrier.data(), output.data(), tc.frames, kSampleRate, params);
    }

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kMeasureBlocks; ++block) {
        fill_input(modulator, carrier, block + kWarmupBlocks, tc.frames);
        engine.process(modulator.data(), carrier.data(), output.data(), tc.frames, kSampleRate, params);
    }
    const auto end = std::chrono::steady_clock::now();
    coefficient_rebuilds = engine.total_coefficient_rebuilds();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / 1000.0 / static_cast<double>(kMeasureBlocks);
}

Measurement run_case(const Case& tc) {
    std::vector<double> samples;
    samples.reserve(kRepeats);
    Measurement m{};
    double sum = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
        int rebuilds = 0;
        const double us = run_once(tc, rebuilds);
        samples.push_back(us);
        sum += us;
        m.coefficient_rebuilds = std::max(m.coefficient_rebuilds, rebuilds);
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
        {256, 8, 80.0f, 1.0f},
        {256, 16, 50.0f, 1.0f},
        {256, 32, 30.0f, 1.0f},
        {1024, 8, 80.0f, 1.0f},
        {1024, 16, 50.0f, 1.0f},
        {1024, 32, 30.0f, 1.0f},
    };

    std::printf("Vocoder benchmark: sample_rate=%u measure_blocks=%d repeats=%d backend=%s\n",
                kSampleRate,
                kMeasureBlocks,
                kRepeats,
                vivid::vocoder_dsp::backend_name(vivid::vocoder_dsp::preferred_backend()));
    for (const auto& tc : cases) {
        const auto m = run_case(tc);
        std::printf("frames=%u bands=%d speed_ms=%.1f mean_us=%.3f±%.3f coeff_rebuilds=%d\n",
                    tc.frames,
                    tc.bands,
                    tc.speed_ms,
                    m.mean_us,
                    m.stddev_us,
                    m.coefficient_rebuilds);
    }

    return 0;
}
