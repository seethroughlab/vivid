#include "shared/reverb_dsp/reverb_dsp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr int kWarmupBlocks = 64;
constexpr int kMeasureBlocks = 512;
constexpr int kRepeats = 8;

void fill_signal(std::vector<float>& input, int block, uint32_t frames, bool impulse) {
    std::fill(input.begin(), input.end(), 0.0f);
    if (impulse) {
        if (block == 0)
            input[0] = 1.0f;
        return;
    }

    for (uint32_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(block * frames + i) / static_cast<float>(kSampleRate);
        input[i] = 0.35f * std::sin(2.0f * 3.14159265f * 180.0f * t)
                 + 0.18f * std::sin(2.0f * 3.14159265f * 720.0f * t)
                 + 0.06f * std::sin(2.0f * 3.14159265f * 2200.0f * t);
    }
}

struct Case {
    const char* name;
    uint32_t frames;
    float room_size;
    float damping;
    float mix;
    bool impulse;
};

struct Measurement {
    double mean_us = 0.0;
    double stddev_us = 0.0;
    int initialization_count = 0;
};

double run_once(const Case& tc, int& initialization_count) {
    vivid::reverb_dsp::Engine engine;
    vivid::reverb_dsp::ProcessParams params{};
    params.room_size = tc.room_size;
    params.damping = tc.damping;
    params.mix = tc.mix;

    std::vector<float> input(tc.frames);
    std::vector<float> output(tc.frames);

    for (int block = 0; block < kWarmupBlocks; ++block) {
        fill_signal(input, block, tc.frames, tc.impulse);
        engine.process(input.data(), output.data(), tc.frames, kSampleRate, params);
    }

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kMeasureBlocks; ++block) {
        fill_signal(input, block + kWarmupBlocks, tc.frames, tc.impulse);
        engine.process(input.data(), output.data(), tc.frames, kSampleRate, params);
    }
    const auto end = std::chrono::steady_clock::now();
    initialization_count = engine.last_stats().initialization_count;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / 1000.0 / static_cast<double>(kMeasureBlocks);
}

Measurement run_case(const Case& tc) {
    std::vector<double> samples;
    samples.reserve(kRepeats);
    Measurement m{};
    double sum = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
        int init_count = 0;
        const double us = run_once(tc, init_count);
        samples.push_back(us);
        sum += us;
        m.initialization_count = std::max(m.initialization_count, init_count);
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
        {"small_room", 256, 0.25f, 0.7f, 0.35f, false},
        {"large_hall", 256, 0.85f, 0.3f, 0.45f, false},
        {"plate", 256, 0.6f, 0.15f, 0.5f, false},
        {"cathedral", 256, 0.95f, 0.1f, 0.6f, false},
        {"tight_slap", 256, 0.1f, 0.9f, 0.25f, false},
        {"dry_passthrough", 256, 0.5f, 0.5f, 0.0f, false},
        {"impulse_tail", 256, 0.95f, 0.1f, 0.6f, true},

        {"small_room", 1024, 0.25f, 0.7f, 0.35f, false},
        {"large_hall", 1024, 0.85f, 0.3f, 0.45f, false},
        {"plate", 1024, 0.6f, 0.15f, 0.5f, false},
        {"cathedral", 1024, 0.95f, 0.1f, 0.6f, false},
        {"tight_slap", 1024, 0.1f, 0.9f, 0.25f, false},
        {"dry_passthrough", 1024, 0.5f, 0.5f, 0.0f, false},
        {"impulse_tail", 1024, 0.95f, 0.1f, 0.6f, true},
    };

    std::printf("Reverb benchmark: sample_rate=%u measure_blocks=%d repeats=%d backend=%s\n",
                kSampleRate,
                kMeasureBlocks,
                kRepeats,
                vivid::reverb_dsp::backend_name(vivid::reverb_dsp::preferred_backend()));
    for (const auto& tc : cases) {
        const auto m = run_case(tc);
        std::printf("frames=%u case=%s room=%.2f damping=%.2f mix=%.2f impulse=%d mean_us=%.3f±%.3f init_count=%d\n",
                    tc.frames,
                    tc.name,
                    tc.room_size,
                    tc.damping,
                    tc.mix,
                    tc.impulse ? 1 : 0,
                    m.mean_us,
                    m.stddev_us,
                    m.initialization_count);
    }

    return 0;
}
