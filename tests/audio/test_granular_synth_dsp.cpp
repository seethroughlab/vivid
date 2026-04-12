#include "runtime/debug/output_analyzer.h"
#include "runtime/operators/operator_loader.h"
#include "shared/granular_dsp/granular_dsp.h"

#include "test_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kFrames = 256;
constexpr int kBlocks = 48;

void fill_input(std::vector<float>& input, int block) {
    for (uint32_t i = 0; i < input.size(); ++i) {
        const float t = static_cast<float>(block * input.size() + i) / static_cast<float>(kSampleRate);
        input[i] = 0.35f * std::sin(2.0f * 3.14159265f * 110.0f * t)
                 + 0.20f * std::sin(2.0f * 3.14159265f * 440.0f * t)
                 + 0.10f * std::sin(2.0f * 3.14159265f * 1320.0f * t);
    }
}

void run_engine_case(float density,
                     int window,
                     float mix,
                     float pitch,
                     float position,
                     const char* label) {
    vivid::granular_dsp::Engine engine;
    vivid::granular_dsp::ProcessParams params{};
    params.position = position;
    params.pitch = pitch;
    params.density = density;
    params.grain_size_ms = 60.0f;
    params.randomize = 0.0f;
    params.window_type = window;
    params.mix = mix;

    std::vector<float> input(kFrames);
    std::vector<float> output(kFrames);
    double energy = 0.0;
    int max_active = 0;
    bool all_finite = true;
    for (int block = 0; block < kBlocks; ++block) {
        fill_input(input, block);
        engine.process(input.data(), output.data(), kFrames, kSampleRate, params);
        max_active = std::max(max_active, engine.last_stats().active_grains);
        for (float sample : output) {
            all_finite = all_finite && std::isfinite(sample);
            energy += static_cast<double>(sample) * sample;
        }
    }

    vivid::granular_dsp::InspectorSnapshot snap{};
    engine.fill_inspector_snapshot(snap, params.position, params.window_type);
    std::fprintf(stderr,
                 "  %s density=%.1f window=%d mix=%.2f pitch=%.1f energy=%.6f max_active=%d snapshot_active=%d backend=%s\n",
                 label,
                 density,
                 window,
                 mix,
                 pitch,
                 energy,
                 max_active,
                 snap.active_count,
                 vivid::granular_dsp::backend_name(engine.last_stats().backend));

    check(all_finite, "GranularSynth engine output finite");
    check(energy > 1.0e-6, "GranularSynth engine produces non-silent output");
    check(max_active >= 0 && max_active <= vivid::granular_dsp::kMaxGrains,
          "GranularSynth active count bounded");
    check(snap.window_type == window, "GranularSynth snapshot preserves window type");
    check(snap.position_norm >= 0.0f && snap.position_norm <= 1.0f,
          "GranularSynth snapshot position bounded");
}

void run_operator_smoke() {
    vivid::OperatorLoader loader;
    if (!loader.load("./granular_synth.dylib")) {
        std::fprintf(stderr, "  SKIP: could not load granular_synth.dylib\n");
        return;
    }

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "GranularSynth descriptor loaded");
    if (!desc) return;

    int grain_idx = -1;
    int density_idx = -1;
    int position_idx = -1;
    int pitch_idx = -1;
    int randomize_idx = -1;
    int window_idx = -1;
    int mix_idx = -1;
    for (uint32_t p = 0; p < desc->param_count; ++p) {
        if (std::strcmp(desc->params[p].name, "grain_size") == 0) grain_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "density") == 0) density_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "position") == 0) position_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "pitch") == 0) pitch_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "randomize") == 0) randomize_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "window") == 0) window_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "mix") == 0) mix_idx = static_cast<int>(p);
    }
    check(grain_idx >= 0 && density_idx >= 0 && position_idx >= 0 && pitch_idx >= 0 &&
              randomize_idx >= 0 && window_idx >= 0 && mix_idx >= 0,
          "GranularSynth expected params found");
    if (grain_idx < 0 || density_idx < 0 || position_idx < 0 ||
        pitch_idx < 0 || randomize_idx < 0 || window_idx < 0 || mix_idx < 0) {
        return;
    }

    void* inst = loader.create_instance();
    if (!inst) return;

    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; ++p)
        params[p] = desc->params[p].default_value;
    params[grain_idx] = 45.0f;
    params[density_idx] = 60.0f;
    params[position_idx] = 0.4f;
    params[pitch_idx] = 7.0f;
    params[randomize_idx] = 0.0f;
    params[window_idx] = 0.0f;
    params[mix_idx] = 0.8f;

    std::vector<float> input(kFrames);
    std::vector<float> pos_cv(kFrames, 0.05f);
    std::vector<float> pitch_cv(kFrames, -2.0f);
    std::vector<float> density_cv(kFrames, 0.0f);
    std::vector<float> output(kFrames);
    float* inputs[4] = {input.data(), pos_cv.data(), pitch_cv.data(), density_cv.data()};
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
        fill_input(input, block);
        loader.process_audio(inst, &ctx);
        for (float sample : output) {
            all_finite = all_finite && std::isfinite(sample);
            energy += static_cast<double>(sample) * sample;
        }
    }
    check(all_finite, "GranularSynth operator output finite");
    check(energy > 1.0e-6, "GranularSynth operator smoke produces non-silent output");
    loader.destroy_instance(inst);
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== test_granular_synth_dsp ===\n");

    run_engine_case(2.0f, 0, 0.5f, 0.0f, 0.8f, "low_density_hann");
    run_engine_case(20.0f, 1, 1.0f, 7.0f, 0.015f, "mid_density_hamming_pitch");
    run_engine_case(60.0f, 2, 1.0f, -12.0f, 0.02f, "high_density_blackman");
    run_engine_case(60.0f, 3, 0.0f, 0.0f, 0.7f, "dry_triangle");

    run_operator_smoke();

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
