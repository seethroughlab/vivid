#include "shared/filter_dsp/filter_dsp.h"

#include "test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr float kPi = 3.14159265358979323846f;

const char* mode_name(int mode) {
    switch (mode) {
        case audio_dsp::FILTER_LP12: return "LP12";
        case audio_dsp::FILTER_LP24: return "LP24";
        case audio_dsp::FILTER_HP12: return "HP12";
        case audio_dsp::FILTER_BP: return "BP";
        case audio_dsp::FILTER_NOTCH: return "Notch";
        case audio_dsp::FILTER_COMB: return "Comb";
        case audio_dsp::FILTER_LADDER: return "Ladder";
        case audio_dsp::FILTER_FORMANT: return "Formant";
        case audio_dsp::FILTER_HP24: return "HP24";
        case audio_dsp::FILTER_PEAK: return "Peak";
        case audio_dsp::FILTER_ALLPASS: return "Allpass";
        case audio_dsp::FILTER_BP24: return "BP24";
        case audio_dsp::FILTER_DIODE: return "Diode";
        case audio_dsp::FILTER_MS20: return "MS-20";
        default: return "Unknown";
    }
}

void fill_input(std::vector<float>& input, int seed) {
    uint32_t noise = static_cast<uint32_t>(seed + 1) * 747796405u + 2891336453u;
    for (uint32_t i = 0; i < input.size(); ++i) {
        noise = noise * 1664525u + 1013904223u;
        const float n = static_cast<float>((noise >> 8) & 0xffffu) / 32768.0f - 1.0f;
        const float t = static_cast<float>(i + seed * 97) / static_cast<float>(kSampleRate);
        input[i] = 0.32f * std::sin(2.0f * kPi * 170.0f * t)
                 + 0.21f * std::sin(2.0f * kPi * 790.0f * t)
                 + 0.09f * n;
    }
}

struct DiffStats {
    float avg_abs_diff = 0.0f;
    float peak_diff = 0.0f;
    float rms = 0.0f;
};

DiffStats compare_buffers(const std::vector<float>& a, const std::vector<float>& b) {
    DiffStats stats{};
    double sum_diff = 0.0;
    double sum_rms = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        const float diff = std::fabs(a[i] - b[i]);
        sum_diff += diff;
        sum_rms += static_cast<double>(b[i]) * b[i];
        stats.peak_diff = std::max(stats.peak_diff, diff);
    }
    stats.avg_abs_diff = static_cast<float>(sum_diff / static_cast<double>(a.size()));
    stats.rms = std::sqrt(sum_rms / static_cast<double>(a.size()));
    return stats;
}

void run_case(int mode, uint32_t frames, float drive, float cutoff, float resonance) {
    audio_dsp::FilterParams params{};
    params.type = mode;
    params.cutoff_hz = cutoff;
    params.resonance = resonance;
    params.drive = drive;
    params.sample_rate = static_cast<float>(kSampleRate);
    const auto plan = audio_dsp::prepare_filter_plan(params);

    std::vector<float> input(frames);
    std::vector<float> samplewise(frames);
    std::vector<float> block(frames);
    std::vector<float> in_place(frames);
    fill_input(input, mode + static_cast<int>(frames) + static_cast<int>(drive * 100.0f));
    in_place = input;

    audio_dsp::FilterState sample_state;
    audio_dsp::FilterState block_state;
    audio_dsp::FilterState in_place_state;

    for (uint32_t i = 0; i < frames; ++i) {
        samplewise[i] = sample_state.process(input[i], params.cutoff_hz, params.resonance,
                                             params.drive, params.type, params.sample_rate);
    }
    audio_dsp::process_filter_block(block_state, plan, input.data(), block.data(), frames);
    audio_dsp::process_filter_block(in_place_state, plan, in_place.data(), in_place.data(), frames);

    const auto block_stats = compare_buffers(samplewise, block);
    const auto in_place_stats = compare_buffers(samplewise, in_place);
    std::fprintf(stderr,
                 "  mode=%s frames=%u drive=%.2f cutoff=%.1f res=%.2f rms=%.6f block_avg=%.8f block_peak=%.8f inplace_avg=%.8f inplace_peak=%.8f\n",
                 mode_name(mode),
                 frames,
                 drive,
                 cutoff,
                 resonance,
                 block_stats.rms,
                 block_stats.avg_abs_diff,
                 block_stats.peak_diff,
                 in_place_stats.avg_abs_diff,
                 in_place_stats.peak_diff);

    check(std::isfinite(block_stats.rms), "prepared block RMS finite");
    check(block_stats.peak_diff < 1.0e-6f, "prepared block matches sample API");
    check(in_place_stats.peak_diff < 1.0e-6f, "prepared in-place block matches sample API");
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== Test: Filter DSP prepared block path ===\n");

    const int modes[] = {
        audio_dsp::FILTER_LP12,
        audio_dsp::FILTER_LP24,
        audio_dsp::FILTER_HP12,
        audio_dsp::FILTER_BP,
        audio_dsp::FILTER_NOTCH,
        audio_dsp::FILTER_COMB,
        audio_dsp::FILTER_LADDER,
        audio_dsp::FILTER_FORMANT,
        audio_dsp::FILTER_HP24,
        audio_dsp::FILTER_PEAK,
        audio_dsp::FILTER_ALLPASS,
        audio_dsp::FILTER_BP24,
        audio_dsp::FILTER_DIODE,
        audio_dsp::FILTER_MS20,
    };

    for (int mode : modes) {
        run_case(mode, 256, 0.0f, 1200.0f, 0.45f);
        run_case(mode, 1024, 0.35f, 2800.0f, 0.75f);
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
