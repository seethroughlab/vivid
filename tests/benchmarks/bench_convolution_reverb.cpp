#include "shared/convolution_reverb_dsp/convolution_reverb_dsp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr int kWarmupBlocks = 16;
constexpr int kMeasureBlocks = 128;
constexpr int kRepeats = 6;

struct Case {
    const char* name;
    uint32_t frames;
    int preset;
    float tail_seconds;
};

struct Measurement {
    double mean_us = 0.0;
    double stddev_us = 0.0;
    vivid::convolution_reverb_dsp::ProcessStats stats{};
};

void fill_signal(std::vector<float>& input, int block, uint32_t frames) {
    float* l = input.data();
    float* r = input.data() + frames;
    for (uint32_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(block * frames + i) / static_cast<float>(kSampleRate);
        l[i] = 0.35f * std::sin(2.0f * 3.14159265f * 180.0f * t)
             + 0.18f * std::sin(2.0f * 3.14159265f * 720.0f * t);
        r[i] = 0.33f * std::sin(2.0f * 3.14159265f * 183.0f * t + 0.2f)
             + 0.15f * std::sin(2.0f * 3.14159265f * 690.0f * t + 0.4f);
    }
}

double run_once(const Case& tc,
                vivid::convolution_reverb_dsp::Backend backend,
                vivid::convolution_reverb_dsp::ProcessStats& stats) {
    vivid::convolution_reverb_dsp::Engine engine;
    vivid::convolution_reverb_dsp::ProcessParams params{};
    params.ir_preset = tc.preset;
    params.mix = 0.5f;
    params.width = 1.15f;
    params.tail_seconds = tc.tail_seconds;

    std::vector<float> input(tc.frames * 2u);
    std::vector<float> output(tc.frames * 2u);
    for (int block = 0; block < kWarmupBlocks; ++block) {
        fill_signal(input, block, tc.frames);
        engine.process(input.data(), output.data(), tc.frames, kSampleRate, params, backend);
    }

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kMeasureBlocks; ++block) {
        fill_signal(input, block + kWarmupBlocks, tc.frames);
        engine.process(input.data(), output.data(), tc.frames, kSampleRate, params, backend);
    }
    const auto end = std::chrono::steady_clock::now();
    stats = engine.last_stats();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / 1000.0 / static_cast<double>(kMeasureBlocks);
}

Measurement run_case(const Case& tc, vivid::convolution_reverb_dsp::Backend backend) {
    std::vector<double> samples;
    samples.reserve(kRepeats);
    Measurement m{};
    double sum = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
        vivid::convolution_reverb_dsp::ProcessStats stats{};
        const double us = run_once(tc, backend, stats);
        samples.push_back(us);
        sum += us;
        m.stats = stats;
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
    using vivid::convolution_reverb_dsp::Backend;
    const Case cases[] = {
        {"room", 256, 0, 1.0f},
        {"hall", 256, 2, 4.0f},
        {"cathedral", 256, 3, 6.0f},
        {"room", 1024, 0, 1.0f},
        {"hall", 1024, 2, 4.0f},
        {"cathedral", 1024, 3, 6.0f},
    };

    std::printf("ConvolutionReverb benchmark: sample_rate=%u measure_blocks=%d repeats=%d preferred=%s\n",
                kSampleRate,
                kMeasureBlocks,
                kRepeats,
                vivid::convolution_reverb_dsp::backend_name(vivid::convolution_reverb_dsp::preferred_backend()));
    for (const auto& tc : cases) {
        const auto scalar = run_case(tc, Backend::Scalar);
        const auto preferred = run_case(tc, vivid::convolution_reverb_dsp::preferred_backend());
        const double speedup = preferred.mean_us > 0.0 ? scalar.mean_us / preferred.mean_us : 0.0;
        std::printf("frames=%u case=%s scalar_us=%.3f±%.3f preferred_backend=%s preferred_us=%.3f±%.3f speedup=%.3fx partitions=%u ir_frames=%u rebuilds=%d\n",
                    tc.frames,
                    tc.name,
                    scalar.mean_us,
                    scalar.stddev_us,
                    vivid::convolution_reverb_dsp::backend_name(preferred.stats.backend),
                    preferred.mean_us,
                    preferred.stddev_us,
                    speedup,
                    preferred.stats.partition_count,
                    preferred.stats.ir_frames,
                    preferred.stats.plan_rebuild_count);
    }
    return 0;
}
