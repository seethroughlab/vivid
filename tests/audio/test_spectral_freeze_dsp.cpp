#include "runtime/debug/output_analyzer.h"
#include "runtime/operators/operator_loader.h"
#include "shared/spectral_freeze_dsp/spectral_freeze_dsp.h"

#include "test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kFrames = 256;
constexpr int kBlocks = 24;

void fill_input(std::vector<float>& input, int block) {
    for (uint32_t i = 0; i < input.size(); ++i) {
        const float t = static_cast<float>(block * input.size() + i) / static_cast<float>(kSampleRate);
        input[i] = 0.35f * std::sin(2.0f * 3.14159265f * 220.0f * t)
                 + 0.18f * std::sin(2.0f * 3.14159265f * 880.0f * t)
                 + 0.05f * std::sin(2.0f * 3.14159265f * 1870.0f * t);
    }
}

struct DiffStats {
    float rms_a = 0.0f;
    float rms_b = 0.0f;
    float avg_abs_diff = 0.0f;
    float peak_diff = 0.0f;
};

DiffStats compare_buffers(const std::vector<float>& a, const std::vector<float>& b) {
    DiffStats stats{};
    double sum_a = 0.0;
    double sum_b = 0.0;
    double sum_diff = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum_a += static_cast<double>(a[i]) * a[i];
        sum_b += static_cast<double>(b[i]) * b[i];
        const float d = std::fabs(a[i] - b[i]);
        sum_diff += d;
        stats.peak_diff = std::max(stats.peak_diff, d);
    }
    stats.rms_a = std::sqrt(sum_a / static_cast<double>(a.size()));
    stats.rms_b = std::sqrt(sum_b / static_cast<double>(b.size()));
    stats.avg_abs_diff = static_cast<float>(sum_diff / static_cast<double>(a.size()));
    return stats;
}

void run_engine_case(int fft_param, int phase_mode, float freeze, float blend, const char* label) {
    using vivid::spectral_freeze_dsp::Backend;
    using vivid::spectral_freeze_dsp::Engine;

    Engine scalar;
    Engine accelerated;
    std::vector<float> input(kFrames);
    std::vector<float> out_scalar;
    std::vector<float> out_accel;
    std::vector<float> block_scalar(kFrames);
    std::vector<float> block_accel(kFrames);

    for (int block = 0; block < kBlocks; ++block) {
        fill_input(input, block);
        const float freeze_now = block >= 4 ? freeze : 0.0f;
        scalar.process(input.data(), block_scalar.data(), kFrames, kSampleRate,
                       fft_param, freeze_now, blend, 0.35f, phase_mode, Backend::Scalar);
        accelerated.process(input.data(), block_accel.data(), kFrames, kSampleRate,
                            fft_param, freeze_now, blend, 0.35f, phase_mode, Backend::Accelerate);
        out_scalar.insert(out_scalar.end(), block_scalar.begin(), block_scalar.end());
        out_accel.insert(out_accel.end(), block_accel.begin(), block_accel.end());
    }

    const auto stats = compare_buffers(out_scalar, out_accel);
    std::fprintf(stderr,
                 "  %s fft=%d phase=%d backend=%s rms_scalar=%.6f rms_accel=%.6f avg_diff=%.6f peak_diff=%.6f\n",
                 label,
                 vivid::spectral_freeze_dsp::resolve_fft_size(fft_param),
                 phase_mode,
                 vivid::spectral_freeze_dsp::backend_name(accelerated.last_backend()),
                 stats.rms_a,
                 stats.rms_b,
                 stats.avg_abs_diff,
                 stats.peak_diff);

    check(std::isfinite(stats.rms_a) && std::isfinite(stats.rms_b), "SpectralFreeze backend RMS finite");
    check(stats.rms_a > 0.0001f && stats.rms_b > 0.0001f, "SpectralFreeze backends produce audio");
    check(stats.avg_abs_diff < 0.02f, "SpectralFreeze Accelerate average diff within tolerance");
    check(stats.peak_diff < 0.20f, "SpectralFreeze Accelerate peak diff within tolerance");
}

void run_operator_smoke() {
    vivid::OperatorLoader loader;
    if (!loader.load("./spectral_freeze.dylib")) {
        std::fprintf(stderr, "  SKIP: could not load spectral_freeze.dylib\n");
        return;
    }

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "SpectralFreeze descriptor loaded");
    if (!desc) return;

    int fft_idx = -1;
    int phase_idx = -1;
    int freeze_idx = -1;
    int blend_idx = -1;
    for (uint32_t p = 0; p < desc->param_count; ++p) {
        if (std::strcmp(desc->params[p].name, "fft_size") == 0) fft_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "phase_mode") == 0) phase_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "freeze") == 0) freeze_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "blend") == 0) blend_idx = static_cast<int>(p);
    }
    check(fft_idx >= 0 && phase_idx >= 0 && freeze_idx >= 0 && blend_idx >= 0,
          "SpectralFreeze expected params found");
    if (fft_idx < 0 || phase_idx < 0 || freeze_idx < 0 || blend_idx < 0) return;

    for (int fft = 0; fft < 3; ++fft) {
        for (int phase = 0; phase < 3; ++phase) {
            void* inst = loader.create_instance();
            if (!inst) return;

            std::vector<float> params(desc->param_count);
            for (uint32_t p = 0; p < desc->param_count; ++p)
                params[p] = desc->params[p].default_value;
            params[fft_idx] = static_cast<float>(fft);
            params[phase_idx] = static_cast<float>(phase);
            params[freeze_idx] = 1.0f;
            params[blend_idx] = 0.75f;

            std::vector<float> input(kFrames);
            std::vector<float> freeze_cv(kFrames, 1.0f);
            std::vector<float> blend_cv(kFrames, 0.0f);
            std::vector<float> output(kFrames);
            float* inputs[3] = {input.data(), freeze_cv.data(), blend_cv.data()};
            float* outputs[1] = {output.data()};
            VividAudioContext ctx{};
            ctx.sample_rate = kSampleRate;
            ctx.buffer_size = kFrames;
            ctx.input_buffers = inputs;
            ctx.output_buffers = outputs;
            ctx.param_values = params.data();

            double energy = 0.0;
            for (int block = 0; block < kBlocks; ++block) {
                fill_input(input, block);
                loader.process_audio(inst, &ctx);
                for (float sample : output) {
                    check(std::isfinite(sample), "SpectralFreeze operator output finite");
                    energy += static_cast<double>(sample) * sample;
                }
            }
            check(energy > 1.0e-6, "SpectralFreeze operator smoke produces non-silent output");
            loader.destroy_instance(inst);
        }
    }
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== test_spectral_freeze_dsp ===\n");

    run_engine_case(0, 0, 0.0f, 0.0f, "live");
    run_engine_case(1, 0, 1.0f, 0.7f, "frozen_live_phase");
    run_engine_case(2, 1, 1.0f, 0.8f, "frozen_phase");

    run_operator_smoke();

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
