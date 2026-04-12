#include "shared/audio_kernels/audio_buffer_kernels.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr int kMeasureBlocks = 4096;
constexpr int kRepeats = 12;
volatile double g_sink = 0.0;

enum class Kernel {
    Clear,
    Scale,
    Mix4,
    StereoPanWidth,
};

struct Case {
    const char* name;
    Kernel kernel;
    uint32_t frames;
    float a;
    float b;
    float c;
    float d;
    float e;
    int connected;
};

struct Measurement {
    double mean_us = 0.0;
    double stddev_us = 0.0;
    double checksum = 0.0;
};

void fill_buffer(std::vector<float>& buf, float phase, float scale) {
    for (uint32_t i = 0; i < buf.size(); ++i) {
        const float t = static_cast<float>(i) * 0.017f + phase;
        buf[i] = scale * (std::sin(t) + 0.35f * std::sin(t * 3.7f));
    }
}

double checksum(const std::vector<float>& a, const std::vector<float>& b = {}) {
    double sum = 0.0;
    for (float v : a)
        sum += v;
    for (float v : b)
        sum += v;
    return sum;
}

double run_once(const Case& tc, double& out_checksum) {
    std::vector<float> in1(tc.frames);
    std::vector<float> in2(tc.frames);
    std::vector<float> in3(tc.frames);
    std::vector<float> in4(tc.frames);
    std::vector<float> left(tc.frames);
    std::vector<float> right(tc.frames);
    std::vector<float> out(tc.frames);
    std::vector<float> out_r(tc.frames);

    fill_buffer(in1, 0.0f, 0.8f);
    fill_buffer(in2, 0.3f, 0.5f);
    fill_buffer(in3, 0.7f, 0.35f);
    fill_buffer(in4, 1.1f, 0.2f);
    fill_buffer(left, 0.2f, 0.9f);
    fill_buffer(right, 0.9f, 0.65f);

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kMeasureBlocks; ++block) {
        switch (tc.kernel) {
            case Kernel::Clear:
                vivid::audio_kernels::clear(out.data(), tc.frames);
                break;
            case Kernel::Scale:
                vivid::audio_kernels::scale(in1.data(), out.data(), tc.frames, tc.a);
                break;
            case Kernel::Mix4:
                vivid::audio_kernels::mix4(tc.connected >= 1 ? in1.data() : nullptr,
                                           tc.connected >= 2 ? in2.data() : nullptr,
                                           tc.connected >= 3 ? in3.data() : nullptr,
                                           tc.connected >= 4 ? in4.data() : nullptr,
                                           out.data(),
                                           tc.frames,
                                           tc.a,
                                           tc.b,
                                           tc.c,
                                           tc.d);
                break;
            case Kernel::StereoPanWidth:
                vivid::audio_kernels::stereo_pan_width(left.data(), right.data(),
                                                       out.data(), out_r.data(), tc.frames,
                                                       tc.a, tc.b, tc.c, tc.d, tc.e);
                break;
        }
    }
    const auto end = std::chrono::steady_clock::now();
    out_checksum = checksum(out, out_r);
    g_sink += out_checksum;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / 1000.0 / static_cast<double>(kMeasureBlocks);
}

Measurement run_case(const Case& tc) {
    std::vector<double> samples;
    samples.reserve(kRepeats);
    Measurement m{};
    double sum = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
        double cs = 0.0;
        const double us = run_once(tc, cs);
        samples.push_back(us);
        sum += us;
        m.checksum = cs;
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
        {"clear", Kernel::Clear, 256, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0},
        {"scale_unity", Kernel::Scale, 256, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1},
        {"scale_atten", Kernel::Scale, 256, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1},
        {"scale_silence", Kernel::Scale, 256, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1},
        {"mix4_one_input", Kernel::Mix4, 256, 0.8f, 0.6f, 0.4f, 0.2f, 0.0f, 1},
        {"mix4_two_inputs", Kernel::Mix4, 256, 0.8f, 0.6f, 0.4f, 0.2f, 0.0f, 2},
        {"mix4_four_inputs", Kernel::Mix4, 256, 0.8f, 0.6f, 0.4f, 0.2f, 0.0f, 4},
        {"stereo_identity", Kernel::StereoPanWidth, 256, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2},
        {"stereo_mono", Kernel::StereoPanWidth, 256, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 2},
        {"stereo_wide", Kernel::StereoPanWidth, 256, 1.0f, 1.0f, 2.0f, 1.0f, 1.0f, 2},
        {"stereo_hard_left", Kernel::StereoPanWidth, 256, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 2},
        {"stereo_hard_right", Kernel::StereoPanWidth, 256, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 2},

        {"clear", Kernel::Clear, 1024, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0},
        {"scale_unity", Kernel::Scale, 1024, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1},
        {"scale_atten", Kernel::Scale, 1024, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1},
        {"scale_silence", Kernel::Scale, 1024, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1},
        {"mix4_one_input", Kernel::Mix4, 1024, 0.8f, 0.6f, 0.4f, 0.2f, 0.0f, 1},
        {"mix4_two_inputs", Kernel::Mix4, 1024, 0.8f, 0.6f, 0.4f, 0.2f, 0.0f, 2},
        {"mix4_four_inputs", Kernel::Mix4, 1024, 0.8f, 0.6f, 0.4f, 0.2f, 0.0f, 4},
        {"stereo_identity", Kernel::StereoPanWidth, 1024, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2},
        {"stereo_mono", Kernel::StereoPanWidth, 1024, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 2},
        {"stereo_wide", Kernel::StereoPanWidth, 1024, 1.0f, 1.0f, 2.0f, 1.0f, 1.0f, 2},
        {"stereo_hard_left", Kernel::StereoPanWidth, 1024, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 2},
        {"stereo_hard_right", Kernel::StereoPanWidth, 1024, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 2},
    };

    std::printf("Audio kernel benchmark: measure_blocks=%d repeats=%d backend=%s\n",
                kMeasureBlocks,
                kRepeats,
                vivid::audio_kernels::backend_name());
    for (const auto& tc : cases) {
        const auto m = run_case(tc);
        std::printf("frames=%u kernel=%s mean_us=%.4f stddev_us=%.4f checksum=%.6f\n",
                    tc.frames,
                    tc.name,
                    m.mean_us,
                    m.stddev_us,
                    m.checksum);
    }

    return g_sink == 123456789.0 ? 1 : 0;
}
