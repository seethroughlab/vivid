#include "runtime/operators/operator_loader.h"
#include "shared/vocoder_dsp/vocoder_dsp.h"

#include "test_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kFrames = 256;
constexpr int kBlocks = 32;

struct RefBandState {
    float mod_low = 0.0f;
    float mod_band = 0.0f;
    float car_low = 0.0f;
    float car_band = 0.0f;
    float envelope = 0.0f;
};

void fill_input(std::vector<float>& modulator, std::vector<float>& carrier, int block) {
    for (uint32_t i = 0; i < modulator.size(); ++i) {
        const float t = static_cast<float>(block * modulator.size() + i) / static_cast<float>(kSampleRate);
        modulator[i] = 0.35f * std::sin(2.0f * 3.14159265f * 95.0f * t)
                     + 0.22f * std::sin(2.0f * 3.14159265f * 610.0f * t)
                     + 0.10f * std::sin(2.0f * 3.14159265f * 1800.0f * t);
        carrier[i] = 0.45f * (2.0f * (t * 110.0f - std::floor(0.5f + t * 110.0f)))
                   + 0.20f * std::sin(2.0f * 3.14159265f * 330.0f * t);
    }
}

void process_reference(const float* mod_in,
                       const float* car_in,
                       float* out,
                       uint32_t frames,
                       int bands,
                       float speed_ms,
                       float mix,
                       std::array<RefBandState, vivid::vocoder_dsp::kMaxBands>& states) {
    if (speed_ms < 1.0f) speed_ms = 1.0f;
    if (speed_ms > 500.0f) speed_ms = 500.0f;
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;

    const int num_bands = std::max(4, std::min(vivid::vocoder_dsp::kMaxBands, bands));
    const float wet = mix;
    const float dry = 1.0f - wet;
    const float sr = static_cast<float>(kSampleRate);
    const float env_coeff = 1.0f - std::exp(-1.0f / (speed_ms * 0.001f * sr));

    float band_freqs[vivid::vocoder_dsp::kMaxBands];
    for (int b = 0; b < num_bands; ++b) {
        if (num_bands > 1)
            band_freqs[b] = 80.0f * std::pow(12000.0f / 80.0f,
                                             static_cast<float>(b) / static_cast<float>(num_bands - 1));
        else
            band_freqs[b] = 1000.0f;
    }

    float band_f[vivid::vocoder_dsp::kMaxBands];
    float band_q[vivid::vocoder_dsp::kMaxBands];
    for (int b = 0; b < num_bands; ++b) {
        float f = 2.0f * std::sin(static_cast<float>(M_PI) * band_freqs[b] / sr);
        if (f > 0.95f) f = 0.95f;
        band_f[b] = f;
        if (num_bands > 2) {
            const float lo = (b > 0) ? band_freqs[b - 1] : band_freqs[0] * 0.5f;
            const float hi = (b < num_bands - 1) ? band_freqs[b + 1] : band_freqs[num_bands - 1] * 2.0f;
            band_q[b] = 1.0f / (band_freqs[b] / (hi - lo));
        } else {
            band_q[b] = 0.15f;
        }
        if (band_q[b] < 0.05f) band_q[b] = 0.05f;
        if (band_q[b] > 0.5f) band_q[b] = 0.5f;
    }

    const float norm = 1.0f / std::sqrt(static_cast<float>(num_bands));
    for (uint32_t i = 0; i < frames; ++i) {
        const float mod_sample = mod_in[i];
        const float car_sample = car_in[i];
        float band_sum = 0.0f;
        for (int b = 0; b < num_bands; ++b) {
            RefBandState& bs = states[b];
            const float f = band_f[b];
            const float q = band_q[b];

            bs.mod_low += f * bs.mod_band;
            const float mod_high = mod_sample - bs.mod_low - q * bs.mod_band;
            bs.mod_band += f * mod_high;
            const float mod_bp = bs.mod_band;

            const float abs_mod = std::fabs(mod_bp);
            bs.envelope += (abs_mod - bs.envelope) * env_coeff;

            bs.car_low += f * bs.car_band;
            const float car_high = car_sample - bs.car_low - q * bs.car_band;
            bs.car_band += f * car_high;
            const float car_bp = bs.car_band;

            band_sum += car_bp * bs.envelope;
        }
        const float wet_sig = band_sum * norm;
        out[i] = mod_sample * dry + wet_sig * wet;
    }
}

struct DiffStats {
    float rms_engine = 0.0f;
    float rms_reference = 0.0f;
    float avg_abs_diff = 0.0f;
    float peak_diff = 0.0f;
};

DiffStats compare_buffers(const std::vector<float>& engine, const std::vector<float>& reference) {
    DiffStats stats{};
    double sum_engine = 0.0;
    double sum_reference = 0.0;
    double sum_diff = 0.0;
    for (size_t i = 0; i < engine.size(); ++i) {
        sum_engine += static_cast<double>(engine[i]) * engine[i];
        sum_reference += static_cast<double>(reference[i]) * reference[i];
        const float d = std::fabs(engine[i] - reference[i]);
        sum_diff += d;
        stats.peak_diff = std::max(stats.peak_diff, d);
    }
    stats.rms_engine = std::sqrt(sum_engine / static_cast<double>(engine.size()));
    stats.rms_reference = std::sqrt(sum_reference / static_cast<double>(reference.size()));
    stats.avg_abs_diff = static_cast<float>(sum_diff / static_cast<double>(engine.size()));
    return stats;
}

void run_engine_case(int bands, float speed_ms, float mix, float speed_cv, const char* label) {
    vivid::vocoder_dsp::Engine engine;
    vivid::vocoder_dsp::ProcessParams params{};
    params.bands = bands;
    params.envelope_speed_ms = speed_ms + speed_cv;
    params.mix = mix;

    std::array<RefBandState, vivid::vocoder_dsp::kMaxBands> ref_states{};
    std::vector<float> modulator(kFrames);
    std::vector<float> carrier(kFrames);
    std::vector<float> block_engine(kFrames);
    std::vector<float> block_reference(kFrames);
    std::vector<float> out_engine;
    std::vector<float> out_reference;

    for (int block = 0; block < kBlocks; ++block) {
        fill_input(modulator, carrier, block);
        engine.process(modulator.data(), carrier.data(), block_engine.data(), kFrames, kSampleRate, params);
        process_reference(modulator.data(), carrier.data(), block_reference.data(),
                          kFrames, bands, speed_ms + speed_cv, mix, ref_states);
        out_engine.insert(out_engine.end(), block_engine.begin(), block_engine.end());
        out_reference.insert(out_reference.end(), block_reference.begin(), block_reference.end());
    }

    const auto stats = compare_buffers(out_engine, out_reference);
    std::fprintf(stderr,
                 "  %s bands=%d speed=%.1f mix=%.2f speed_cv=%.1f backend=%s rms_engine=%.6f rms_ref=%.6f avg_diff=%.8f peak_diff=%.8f coeff_rebuilds=%d\n",
                 label,
                 bands,
                 speed_ms,
                 mix,
                 speed_cv,
                 vivid::vocoder_dsp::backend_name(engine.last_stats().backend),
                 stats.rms_engine,
                 stats.rms_reference,
                 stats.avg_abs_diff,
                 stats.peak_diff,
                 engine.total_coefficient_rebuilds());

    check(std::isfinite(stats.rms_engine) && std::isfinite(stats.rms_reference), "Vocoder RMS finite");
    check(stats.rms_engine > 1.0e-6f || mix == 0.0f, "Vocoder engine produces expected output");
    check(stats.avg_abs_diff < 1.0e-6f, "Vocoder engine average diff matches reference");
    check(stats.peak_diff < 1.0e-5f, "Vocoder engine peak diff matches reference");
    check(engine.total_coefficient_rebuilds() == 1, "Vocoder coefficients cached across steady blocks");
}

void run_operator_smoke() {
    vivid::OperatorLoader loader;
    if (!loader.load("./vocoder.dylib")) {
        std::fprintf(stderr, "  SKIP: could not load vocoder.dylib\n");
        return;
    }

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "Vocoder descriptor loaded");
    if (!desc) return;

    int bands_idx = -1;
    int speed_idx = -1;
    int mix_idx = -1;
    for (uint32_t p = 0; p < desc->param_count; ++p) {
        if (std::strcmp(desc->params[p].name, "bands") == 0) bands_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "envelope_speed") == 0) speed_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "mix") == 0) mix_idx = static_cast<int>(p);
    }
    check(bands_idx >= 0 && speed_idx >= 0 && mix_idx >= 0, "Vocoder expected params found");

    int modulator_idx = -1;
    int carrier_idx = -1;
    int output_idx = -1;
    int speed_cv_idx = -1;
    for (uint32_t p = 0; p < desc->port_count; ++p) {
        if (std::strcmp(desc->ports[p].name, "modulator") == 0) modulator_idx = static_cast<int>(p);
        if (std::strcmp(desc->ports[p].name, "carrier") == 0) carrier_idx = static_cast<int>(p);
        if (std::strcmp(desc->ports[p].name, "output") == 0) output_idx = static_cast<int>(p);
        if (std::strcmp(desc->ports[p].name, "speed_cv") == 0) speed_cv_idx = static_cast<int>(p);
    }
    check(modulator_idx >= 0 && carrier_idx >= 0 && output_idx >= 0 && speed_cv_idx >= 0,
          "Vocoder expected ports found");
    if (bands_idx < 0 || speed_idx < 0 || mix_idx < 0 ||
        modulator_idx < 0 || carrier_idx < 0 || output_idx < 0 || speed_cv_idx < 0) {
        return;
    }

    void* inst = loader.create_instance();
    if (!inst) return;

    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; ++p)
        params[p] = desc->params[p].default_value;
    params[bands_idx] = 24.0f;
    params[speed_idx] = 30.0f;
    params[mix_idx] = 1.0f;

    std::vector<float> modulator(kFrames);
    std::vector<float> carrier(kFrames);
    std::vector<float> speed_cv(kFrames, 12.0f);
    std::vector<float> output(kFrames);
    float* inputs[3] = {modulator.data(), carrier.data(), speed_cv.data()};
    float* outputs[1] = {output.data()};

    VividAudioContext ctx{};
    ctx.sample_rate = kSampleRate;
    ctx.buffer_size = kFrames;
    ctx.input_buffers = inputs;
    ctx.output_buffers = outputs;
    ctx.param_values = params.data();

    double energy = 0.0;
    bool all_finite = true;
    for (int block = 0; block < kBlocks; ++block) {
        fill_input(modulator, carrier, block);
        loader.process_audio(inst, &ctx);
        for (float sample : output) {
            all_finite = all_finite && std::isfinite(sample);
            energy += static_cast<double>(sample) * sample;
        }
    }
    check(all_finite, "Vocoder operator output finite");
    check(energy > 1.0e-6, "Vocoder operator smoke produces non-silent output");
    loader.destroy_instance(inst);
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== test_vocoder_dsp ===\n");

    run_engine_case(8, 120.0f, 1.0f, 0.0f, "eight_bands_slow");
    run_engine_case(16, 12.0f, 0.65f, 7.0f, "sixteen_bands_fast_cv");
    run_engine_case(32, 45.0f, 1.0f, 0.0f, "thirty_two_bands_wet");
    run_engine_case(24, 50.0f, 0.0f, 0.0f, "dry_passthrough");

    run_operator_smoke();

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
