#include "runtime/operators/operator_loader.h"
#include "shared/convolution_reverb_dsp/convolution_reverb_dsp.h"

#include "test_helpers.h"
#include "assets/test_wav_helper.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kFrames = 256;
constexpr int kBlocks = 20;

struct DiffStats {
    float rms_a = 0.0f;
    float rms_b = 0.0f;
    float avg_abs_diff = 0.0f;
    float peak_diff = 0.0f;
};

void fill_signal(std::vector<float>& input, int block) {
    float* l = input.data();
    float* r = input.data() + kFrames;
    for (uint32_t i = 0; i < kFrames; ++i) {
        const float t = static_cast<float>(block * kFrames + i) / static_cast<float>(kSampleRate);
        l[i] = 0.35f * std::sin(2.0f * 3.14159265f * 220.0f * t)
             + 0.08f * std::sin(2.0f * 3.14159265f * 1600.0f * t);
        r[i] = 0.32f * std::sin(2.0f * 3.14159265f * 223.0f * t + 0.4f)
             + 0.07f * std::sin(2.0f * 3.14159265f * 1300.0f * t + 0.2f);
    }
}

DiffStats compare(const std::vector<float>& a, const std::vector<float>& b) {
    DiffStats stats{};
    double sum_a = 0.0;
    double sum_b = 0.0;
    double sum_d = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum_a += static_cast<double>(a[i]) * a[i];
        sum_b += static_cast<double>(b[i]) * b[i];
        const float d = std::fabs(a[i] - b[i]);
        sum_d += d;
        stats.peak_diff = std::max(stats.peak_diff, d);
    }
    stats.rms_a = std::sqrt(sum_a / static_cast<double>(a.size()));
    stats.rms_b = std::sqrt(sum_b / static_cast<double>(b.size()));
    stats.avg_abs_diff = static_cast<float>(sum_d / static_cast<double>(a.size()));
    return stats;
}

void direct_convolve_block(const vivid::convolution_reverb_dsp::ImpulseResponse& ir,
                           const std::vector<float>& all_input_l,
                           const std::vector<float>& all_input_r,
                           uint32_t start,
                           uint32_t frames,
                           float mix,
                           float width,
                           std::vector<float>& out) {
    out.assign(frames * 2u, 0.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const uint32_t n = start + i;
        float wet_l = 0.0f;
        float wet_r = 0.0f;
        const uint32_t count = std::min<uint32_t>(ir.frames(), n + 1u);
        for (uint32_t k = 0; k < count; ++k) {
            const uint32_t src = n - k;
            const float xl = all_input_l[src];
            const float xr = all_input_r[src];
            wet_l += ir.ll[k] * xl + ir.rl[k] * xr;
            wet_r += ir.lr[k] * xl + ir.rr[k] * xr;
        }
        const float mid = 0.5f * (wet_l + wet_r);
        const float side = 0.5f * (wet_l - wet_r) * width;
        wet_l = mid + side;
        wet_r = mid - side;
        out[i] = all_input_l[n] * (1.0f - mix) + wet_l * mix;
        out[frames + i] = all_input_r[n] * (1.0f - mix) + wet_r * mix;
    }
}

void run_reference_case(int preset,
                        vivid::convolution_reverb_dsp::IrLayout expected_layout,
                        const char* label) {
    vivid::convolution_reverb_dsp::ProcessParams params{};
    params.ir_preset = preset;
    params.mix = 0.55f;
    params.width = 1.2f;
    params.tail_seconds = 0.35f;
    params.pre_delay_ms = 6.0f;

    vivid::convolution_reverb_dsp::IrConfig config{};
    config.preset = preset;
    config.sample_rate = kSampleRate;
    config.tail_seconds = params.tail_seconds;
    config.pre_delay_ms = params.pre_delay_ms;
    config.gain_db = params.ir_gain_db;
    auto ir = vivid::convolution_reverb_dsp::build_impulse_response(config);

    vivid::convolution_reverb_dsp::Engine engine;
    std::vector<float> block_in(kFrames * 2u);
    std::vector<float> block_engine(kFrames * 2u);
    std::vector<float> block_ref(kFrames * 2u);
    std::vector<float> out_engine;
    std::vector<float> out_ref;
    std::vector<float> all_l(kFrames * kBlocks);
    std::vector<float> all_r(kFrames * kBlocks);

    for (int block = 0; block < kBlocks; ++block) {
        fill_signal(block_in, block);
        std::copy(block_in.begin(), block_in.begin() + kFrames, all_l.begin() + block * kFrames);
        std::copy(block_in.begin() + kFrames, block_in.end(), all_r.begin() + block * kFrames);
    }

    for (int block = 0; block < kBlocks; ++block) {
        std::copy(all_l.begin() + block * kFrames, all_l.begin() + (block + 1) * kFrames, block_in.begin());
        std::copy(all_r.begin() + block * kFrames, all_r.begin() + (block + 1) * kFrames, block_in.begin() + kFrames);
        engine.process(block_in.data(), block_engine.data(), kFrames, kSampleRate, params,
                       vivid::convolution_reverb_dsp::Backend::Scalar);
        direct_convolve_block(ir, all_l, all_r, block * kFrames, kFrames,
                              params.mix, params.width, block_ref);
        out_engine.insert(out_engine.end(), block_engine.begin(), block_engine.end());
        out_ref.insert(out_ref.end(), block_ref.begin(), block_ref.end());
    }

    const auto stats = compare(out_engine, out_ref);
    const auto engine_stats = engine.last_stats();
    std::fprintf(stderr,
                 "  %s layout=%d rms_engine=%.6f rms_ref=%.6f avg_diff=%.6f peak_diff=%.6f partitions=%u rebuilds=%d\n",
                 label,
                 static_cast<int>(engine_stats.layout),
                 stats.rms_a,
                 stats.rms_b,
                 stats.avg_abs_diff,
                 stats.peak_diff,
                 engine_stats.partition_count,
                 engine_stats.plan_rebuild_count);

    check(engine_stats.layout == expected_layout, "ConvolutionReverb expected IR layout");
    check(engine_stats.plan_rebuild_count == 1, "ConvolutionReverb plan rebuilt once in steady state");
    check(stats.rms_a > 1.0e-5f && stats.rms_b > 1.0e-5f, "ConvolutionReverb reference case non-silent");
    check(stats.avg_abs_diff < 0.0025f, "ConvolutionReverb partitioned average diff near direct reference");
    check(stats.peak_diff < 0.05f, "ConvolutionReverb partitioned peak diff near direct reference");
}

void run_backend_parity_case() {
    vivid::convolution_reverb_dsp::ProcessParams params{};
    params.ir_preset = 2;
    params.mix = 0.5f;
    params.width = 1.0f;
    params.tail_seconds = 0.5f;

    vivid::convolution_reverb_dsp::Engine scalar;
    vivid::convolution_reverb_dsp::Engine accel;
    std::vector<float> input(kFrames * 2u);
    std::vector<float> out_scalar;
    std::vector<float> out_accel;
    std::vector<float> block_scalar(kFrames * 2u);
    std::vector<float> block_accel(kFrames * 2u);

    for (int block = 0; block < kBlocks; ++block) {
        fill_signal(input, block);
        scalar.process(input.data(), block_scalar.data(), kFrames, kSampleRate, params,
                       vivid::convolution_reverb_dsp::Backend::Scalar);
        accel.process(input.data(), block_accel.data(), kFrames, kSampleRate, params,
                      vivid::convolution_reverb_dsp::Backend::Accelerate);
        out_scalar.insert(out_scalar.end(), block_scalar.begin(), block_scalar.end());
        out_accel.insert(out_accel.end(), block_accel.begin(), block_accel.end());
    }

    const auto stats = compare(out_scalar, out_accel);
    std::fprintf(stderr,
                 "  backend parity accel_backend=%s rms_scalar=%.6f rms_accel=%.6f avg_diff=%.6f peak_diff=%.6f\n",
                 vivid::convolution_reverb_dsp::backend_name(accel.last_stats().backend),
                 stats.rms_a,
                 stats.rms_b,
                 stats.avg_abs_diff,
                 stats.peak_diff);
    check(stats.rms_a > 1.0e-5f && stats.rms_b > 1.0e-5f, "ConvolutionReverb backends produce audio");
    check(stats.avg_abs_diff < 0.0035f, "ConvolutionReverb Accelerate average diff within tolerance");
    check(stats.peak_diff < 0.06f, "ConvolutionReverb Accelerate peak diff within tolerance");
}

void run_file_fallback_case() {
    const std::filesystem::path wav = std::filesystem::temp_directory_path() / "vivid_convolution_reverb_ir.wav";
    check(write_test_wav(wav.string(), 2048, kSampleRate, 2), "ConvolutionReverb writes deterministic WAV IR fixture");
    const std::string wav_path = wav.string();

    vivid::convolution_reverb_dsp::ProcessParams file_params{};
    file_params.ir_file = wav_path.c_str();
    file_params.tail_seconds = 0.2f;
    file_params.mix = 0.6f;

    vivid::convolution_reverb_dsp::Engine file_engine;
    std::vector<float> input(kFrames * 2u);
    std::vector<float> output(kFrames * 2u);
    fill_signal(input, 0);
    file_engine.process(input.data(), output.data(), kFrames, kSampleRate, file_params);
    check(file_engine.last_stats().layout == vivid::convolution_reverb_dsp::IrLayout::Stereo,
          "ConvolutionReverb stereo WAV IR layout");

    vivid::convolution_reverb_dsp::ProcessParams bad_params{};
    bad_params.ir_file = "/tmp/does-not-exist-vivid-convolution-reverb.wav";
    bad_params.tail_seconds = 0.2f;
    vivid::convolution_reverb_dsp::Engine fallback_engine;
    fallback_engine.process(input.data(), output.data(), kFrames, kSampleRate, bad_params);
    check(fallback_engine.last_stats().ir_frames > 0, "ConvolutionReverb invalid file falls back to built-in IR");
}

void run_operator_smoke() {
    vivid::OperatorLoader loader;
    if (!loader.load("./convolution_reverb.dylib")) {
        std::fprintf(stderr, "  SKIP: could not load convolution_reverb.dylib\n");
        return;
    }

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "ConvolutionReverb descriptor loaded");
    if (!desc) return;
    check(std::strcmp(desc->name, "ConvolutionReverb") == 0, "ConvolutionReverb descriptor name");
    check(desc->param_count == 7, "ConvolutionReverb param count");

    bool saw_input = false;
    bool saw_output = false;
    for (uint32_t p = 0; p < desc->port_count; ++p) {
        if (std::strcmp(desc->ports[p].name, "input") == 0) {
            saw_input = true;
            check(desc->ports[p].channels == 2, "ConvolutionReverb input is stereo");
        }
        if (std::strcmp(desc->ports[p].name, "output") == 0) {
            saw_output = true;
            check(desc->ports[p].channels == 2, "ConvolutionReverb output is stereo");
        }
    }
    check(saw_input && saw_output, "ConvolutionReverb expected ports found");

    void* inst = loader.create_instance();
    if (!inst) return;

    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; ++p)
        params[p] = desc->params[p].default_value;
    const char* file_params[1] = {""};
    std::vector<float> input(kFrames * 2u);
    std::vector<float> output(kFrames * 2u);
    float* inputs[1] = {input.data()};
    float* outputs[1] = {output.data()};

    VividAudioContext ctx{};
    ctx.sample_rate = kSampleRate;
    ctx.buffer_size = kFrames;
    ctx.input_buffers = inputs;
    ctx.output_buffers = outputs;
    ctx.param_values = params.data();
    ctx.file_param_values = file_params;
    ctx.file_param_count = 1;

    double energy_l = 0.0;
    double energy_r = 0.0;
    bool all_finite = true;
    for (int block = 0; block < kBlocks; ++block) {
        fill_signal(input, block);
        loader.process_audio(inst, &ctx);
        for (uint32_t i = 0; i < kFrames; ++i) {
            all_finite = all_finite && std::isfinite(output[i]) && std::isfinite(output[kFrames + i]);
            energy_l += static_cast<double>(output[i]) * output[i];
            energy_r += static_cast<double>(output[kFrames + i]) * output[kFrames + i];
        }
    }
    check(all_finite, "ConvolutionReverb operator output finite");
    check(energy_l > 1.0e-6 && energy_r > 1.0e-6, "ConvolutionReverb operator stereo output non-silent");
    loader.destroy_instance(inst);
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== test_convolution_reverb_dsp ===\n");
    run_reference_case(0, vivid::convolution_reverb_dsp::IrLayout::TrueStereo, "room_builtin_true_stereo");
    run_reference_case(2, vivid::convolution_reverb_dsp::IrLayout::TrueStereo, "hall_builtin_true_stereo");
    run_backend_parity_case();
    run_file_fallback_case();
    run_operator_smoke();
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
