#include "runtime/operators/operator_loader.h"
#include "shared/reverb_dsp/reverb_dsp.h"

#include "test_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t kFrames = 256;
constexpr int kBlocks = 48;
constexpr int kCombLengths[vivid::reverb_dsp::kCombCount] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int kAllPassLengths[vivid::reverb_dsp::kAllPassCount] = {556, 441, 341, 225};

struct RefCombFilter {
    std::vector<float> buffer;
    int size = 0;
    int idx = 0;
    float filterstore = 0.0f;

    void init(int len) {
        size = len;
        idx = 0;
        filterstore = 0.0f;
        if (static_cast<int>(buffer.size()) < size)
            buffer.assign(size, 0.0f);
        else
            std::fill_n(buffer.data(), size, 0.0f);
    }

    float process(float input, float feedback, float damp1, float damp2) {
        const float out = buffer[idx];
        filterstore = out * damp2 + filterstore * damp1;
        buffer[idx] = input + filterstore * feedback;
        if (++idx >= size) idx = 0;
        return out;
    }
};

struct RefAllPassDelay {
    std::vector<float> buffer;
    int size = 0;
    int idx = 0;

    void init(int len) {
        size = len;
        idx = 0;
        if (static_cast<int>(buffer.size()) < size)
            buffer.assign(size, 0.0f);
        else
            std::fill_n(buffer.data(), size, 0.0f);
    }

    float process(float input) {
        const float bufout = buffer[idx];
        const float out = bufout - input;
        buffer[idx] = input + bufout * 0.5f;
        if (++idx >= size) idx = 0;
        return out;
    }
};

struct RefReverb {
    std::array<RefCombFilter, vivid::reverb_dsp::kCombCount> combs;
    std::array<RefAllPassDelay, vivid::reverb_dsp::kAllPassCount> allpasses;
    bool initialized = false;
    uint32_t init_rate = 0;

    void lazy_init(uint32_t sr) {
        if (initialized && init_rate == sr) return;
        const double scale = static_cast<double>(sr) / 44100.0;
        for (int i = 0; i < vivid::reverb_dsp::kCombCount; ++i)
            combs[i].init(static_cast<int>(kCombLengths[i] * scale));
        for (int i = 0; i < vivid::reverb_dsp::kAllPassCount; ++i)
            allpasses[i].init(static_cast<int>(kAllPassLengths[i] * scale));
        initialized = true;
        init_rate = sr;
    }

    void process(const float* in, float* out, uint32_t frames, uint32_t sample_rate,
                 float room_size, float damping, float mix) {
        lazy_init(sample_rate);
        const float fb = room_size * 0.28f + 0.7f;
        const float damp1 = damping;
        const float damp2 = 1.0f - damp1;
        const float wet = mix;
        const float dry = 1.0f - wet;

        for (uint32_t i = 0; i < frames; ++i) {
            const float inp = in[i] * 0.125f;
            float sum = 0.0f;

            for (int c = 0; c < vivid::reverb_dsp::kCombCount; ++c)
                sum += combs[c].process(inp, fb, damp1, damp2);

            for (int a = 0; a < vivid::reverb_dsp::kAllPassCount; ++a)
                sum = allpasses[a].process(sum);

            out[i] = in[i] * dry + sum * wet;
        }
    }
};

struct DiffStats {
    float rms_engine = 0.0f;
    float rms_reference = 0.0f;
    float avg_abs_diff = 0.0f;
    float peak_diff = 0.0f;
};

void fill_signal(std::vector<float>& input, int block, uint32_t sample_rate, bool impulse) {
    std::fill(input.begin(), input.end(), 0.0f);
    if (impulse) {
        if (block == 0)
            input[0] = 1.0f;
        return;
    }

    for (uint32_t i = 0; i < input.size(); ++i) {
        const float t = static_cast<float>(block * input.size() + i) / static_cast<float>(sample_rate);
        input[i] = 0.35f * std::sin(2.0f * 3.14159265f * 220.0f * t)
                 + 0.12f * std::sin(2.0f * 3.14159265f * 880.0f * t)
                 + 0.05f * std::sin(2.0f * 3.14159265f * 1700.0f * t);
    }
}

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

void run_engine_case(float room_size,
                     float damping,
                     float mix,
                     uint32_t sample_rate,
                     bool impulse,
                     const char* label) {
    vivid::reverb_dsp::Engine engine;
    vivid::reverb_dsp::ProcessParams params{};
    params.room_size = room_size;
    params.damping = damping;
    params.mix = mix;
    RefReverb reference;

    std::vector<float> input(kFrames);
    std::vector<float> block_engine(kFrames);
    std::vector<float> block_reference(kFrames);
    std::vector<float> out_engine;
    std::vector<float> out_reference;

    for (int block = 0; block < kBlocks; ++block) {
        fill_signal(input, block, sample_rate, impulse);
        engine.process(input.data(), block_engine.data(), kFrames, sample_rate, params);
        reference.process(input.data(), block_reference.data(), kFrames, sample_rate,
                          room_size, damping, mix);
        out_engine.insert(out_engine.end(), block_engine.begin(), block_engine.end());
        out_reference.insert(out_reference.end(), block_reference.begin(), block_reference.end());
    }

    const auto stats = compare_buffers(out_engine, out_reference);
    const auto engine_stats = engine.last_stats();
    std::fprintf(stderr,
                 "  %s sr=%u room=%.2f damping=%.2f mix=%.2f impulse=%d backend=%s rms_engine=%.6f rms_ref=%.6f avg_diff=%.8f peak_diff=%.8f inits=%d comb0=%d ap0=%d\n",
                 label,
                 sample_rate,
                 room_size,
                 damping,
                 mix,
                 impulse ? 1 : 0,
                 vivid::reverb_dsp::backend_name(engine_stats.backend),
                 stats.rms_engine,
                 stats.rms_reference,
                 stats.avg_abs_diff,
                 stats.peak_diff,
                 engine_stats.initialization_count,
                 engine_stats.comb_sizes[0],
                 engine_stats.allpass_sizes[0]);

    check(std::isfinite(stats.rms_engine) && std::isfinite(stats.rms_reference), "Reverb RMS finite");
    check(stats.rms_engine > 1.0e-8f || mix == 0.0f, "Reverb engine produces expected output");
    check(stats.avg_abs_diff < 1.0e-7f, "Reverb engine average diff matches reference");
    check(stats.peak_diff < 1.0e-6f, "Reverb engine peak diff matches reference");
    check(engine_stats.initialization_count == 1, "Reverb initializes once for steady sample rate");
}

void run_reinit_case() {
    vivid::reverb_dsp::Engine engine;
    vivid::reverb_dsp::ProcessParams params{};
    std::vector<float> input(kFrames);
    std::vector<float> output(kFrames);

    fill_signal(input, 0, 44100, false);
    engine.process(input.data(), output.data(), kFrames, 44100, params);
    const auto first = engine.last_stats();
    engine.process(input.data(), output.data(), kFrames, 48000, params);
    const auto second = engine.last_stats();

    check(first.initialization_count == 1, "Reverb first sample-rate init counted");
    check(second.initialization_count == 2, "Reverb sample-rate change reinitializes");
    check(first.comb_sizes[0] == 1116, "Reverb 44100 comb size preserved");
    check(second.comb_sizes[0] == static_cast<int>(1116.0 * 48000.0 / 44100.0),
          "Reverb scaled comb size at 48000");
}

void run_operator_smoke() {
    vivid::OperatorLoader loader;
    if (!loader.load("./reverb.dylib")) {
        std::fprintf(stderr, "  SKIP: could not load reverb.dylib\n");
        return;
    }

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "Reverb descriptor loaded");
    if (!desc) return;

    int room_idx = -1;
    int damping_idx = -1;
    int mix_idx = -1;
    for (uint32_t p = 0; p < desc->param_count; ++p) {
        if (std::strcmp(desc->params[p].name, "room_size") == 0) room_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "damping") == 0) damping_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "mix") == 0) mix_idx = static_cast<int>(p);
    }
    check(room_idx >= 0 && damping_idx >= 0 && mix_idx >= 0, "Reverb expected params found");

    int input_idx = -1;
    int output_idx = -1;
    for (uint32_t p = 0; p < desc->port_count; ++p) {
        if (std::strcmp(desc->ports[p].name, "input") == 0) input_idx = static_cast<int>(p);
        if (std::strcmp(desc->ports[p].name, "output") == 0) output_idx = static_cast<int>(p);
    }
    check(input_idx >= 0 && output_idx >= 0, "Reverb expected ports found");
    if (room_idx < 0 || damping_idx < 0 || mix_idx < 0 || input_idx < 0 || output_idx < 0)
        return;

    void* inst = loader.create_instance();
    if (!inst) return;

    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; ++p)
        params[p] = desc->params[p].default_value;
    params[room_idx] = 0.85f;
    params[damping_idx] = 0.3f;
    params[mix_idx] = 0.45f;

    std::vector<float> input(kFrames);
    std::vector<float> output(kFrames);
    float* inputs[1] = {input.data()};
    float* outputs[1] = {output.data()};

    VividAudioContext ctx{};
    ctx.sample_rate = 48000;
    ctx.buffer_size = kFrames;
    ctx.input_buffers = inputs;
    ctx.output_buffers = outputs;
    ctx.param_values = params.data();

    double energy = 0.0;
    bool all_finite = true;
    for (int block = 0; block < kBlocks; ++block) {
        fill_signal(input, block, ctx.sample_rate, false);
        loader.process_audio(inst, &ctx);
        for (float sample : output) {
            all_finite = all_finite && std::isfinite(sample);
            energy += static_cast<double>(sample) * sample;
        }
    }
    check(all_finite, "Reverb operator output finite");
    check(energy > 1.0e-6, "Reverb operator smoke produces non-silent output");
    loader.destroy_instance(inst);
}

} // namespace

int main() {
    std::fprintf(stderr, "\n=== test_reverb_dsp ===\n");

    run_engine_case(0.25f, 0.7f, 0.35f, 48000, false, "small_room");
    run_engine_case(0.85f, 0.3f, 0.45f, 48000, false, "large_hall");
    run_engine_case(0.6f, 0.15f, 0.5f, 48000, false, "plate_low_damping");
    run_engine_case(0.1f, 0.9f, 0.25f, 48000, false, "tight_high_damping");
    run_engine_case(0.5f, 0.5f, 0.0f, 48000, false, "dry_passthrough");
    run_engine_case(0.95f, 0.1f, 0.6f, 48000, true, "impulse_tail");
    run_engine_case(0.85f, 0.3f, 0.45f, 44100, false, "large_hall_44100");

    run_reinit_case();
    run_operator_smoke();

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
