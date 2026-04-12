#include "shared/spectral_freeze_dsp/spectral_freeze_dsp.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kFrames = 256;
constexpr int kWarmupBlocks = 32;
constexpr int kMeasureBlocks = 256;
constexpr int kRepeats = 8;

void fill_input(std::vector<float>& input, int block) {
    for (uint32_t i = 0; i < input.size(); ++i) {
        const float t = static_cast<float>(block * input.size() + i) / static_cast<float>(kSampleRate);
        input[i] = 0.4f * std::sin(2.0f * 3.14159265f * 110.0f * t)
                 + 0.2f * std::sin(2.0f * 3.14159265f * 770.0f * t)
                 + 0.1f * std::sin(2.0f * 3.14159265f * 3300.0f * t);
    }
}

double run_backend_once(vivid::spectral_freeze_dsp::Backend backend, int fft_param) {
    vivid::spectral_freeze_dsp::Engine engine;
    std::vector<float> input(kFrames);
    std::vector<float> output(kFrames);

    for (int block = 0; block < kWarmupBlocks; ++block) {
        fill_input(input, block);
        engine.process(input.data(), output.data(), kFrames, kSampleRate,
                       fft_param, block >= 4 ? 1.0f : 0.0f, 0.75f, 0.35f, 0, backend);
    }

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kMeasureBlocks; ++block) {
        fill_input(input, block + kWarmupBlocks);
        engine.process(input.data(), output.data(), kFrames, kSampleRate,
                       fft_param, 1.0f, 0.75f, 0.35f, 0, backend);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / 1000.0 / static_cast<double>(kMeasureBlocks);
}

struct Measurement {
    double mean_us = 0.0;
    double stddev_us = 0.0;
};

Measurement run_backend(vivid::spectral_freeze_dsp::Backend backend, int fft_param) {
    std::vector<double> samples;
    samples.reserve(kRepeats);
    double sum = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
        const double v = run_backend_once(backend, fft_param);
        samples.push_back(v);
        sum += v;
    }

    Measurement m{};
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
    std::printf("SpectralFreeze benchmark: frames=%u sample_rate=%u measure_blocks=%d repeats=%d\n",
                kFrames, kSampleRate, kMeasureBlocks, kRepeats);
    for (int fft = 0; fft < 3; ++fft) {
        const auto scalar = run_backend(vivid::spectral_freeze_dsp::Backend::Scalar, fft);
        const auto preferred = run_backend(vivid::spectral_freeze_dsp::preferred_backend(), fft);
        const double speedup = preferred.mean_us > 0.0 ? scalar.mean_us / preferred.mean_us : 0.0;
        std::printf("fft=%d scalar_us=%.3f±%.3f preferred_backend=%s preferred_us=%.3f±%.3f speedup=%.3fx\n",
                    vivid::spectral_freeze_dsp::resolve_fft_size(fft),
                    scalar.mean_us,
                    scalar.stddev_us,
                    vivid::spectral_freeze_dsp::backend_name(vivid::spectral_freeze_dsp::preferred_backend()),
                    preferred.mean_us,
                    preferred.stddev_us,
                    speedup);
    }
    return 0;
}
