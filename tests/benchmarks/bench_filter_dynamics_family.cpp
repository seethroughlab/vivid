#include "runtime/operators/operator_loader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifndef VIVID_TEST_PLUGIN_SUFFIX
#define VIVID_TEST_PLUGIN_SUFFIX ".dylib"
#endif

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr int kWarmupBlocks = 32;
constexpr int kMeasureBlocks = 512;
constexpr int kRepeats = 8;
constexpr float kPi = 3.14159265358979323846f;

enum class OperatorKind {
    ParametricEQ,
    Compressor,
    Limiter,
    Filter,
    DualFilter,
};

enum class SignalKind {
    SineMix,
    Gentle,
    OverCeiling,
    Transient,
    Noise,
};

struct ParamOverride {
    const char* name;
    float value;
};

struct Case {
    OperatorKind op;
    const char* name;
    uint32_t frames;
    SignalKind signal = SignalKind::SineMix;
    std::vector<ParamOverride> params;
    bool connect_aux_audio = false;
    bool use_freq_cv = false;
    bool use_threshold_cv = false;
    bool use_filter_cv = false;
    bool use_lane_mod = false;
    int input_count = 1;
    int lane_view_count = 0;
};

struct Measurement {
    double mean_us = 0.0;
    double stddev_us = 0.0;
};

struct LaneStateEntry {
    uint32_t lane_id;
    uint32_t byte_size;
    std::vector<uint8_t> data;
};

std::vector<LaneStateEntry> g_lane_states;
std::filesystem::path g_plugin_dir{"."};

void* bench_lane_state(void*, uint32_t lane_id, uint32_t byte_size) {
    for (auto& entry : g_lane_states) {
        if (entry.lane_id == lane_id && entry.byte_size == byte_size)
            return entry.data.data();
    }
    g_lane_states.push_back({lane_id, byte_size, std::vector<uint8_t>(byte_size, 0)});
    return g_lane_states.back().data.data();
}

const char* op_target_name(OperatorKind op) {
    switch (op) {
        case OperatorKind::ParametricEQ: return "parametric_eq";
        case OperatorKind::Compressor: return "compressor";
        case OperatorKind::Limiter: return "limiter";
        case OperatorKind::Filter: return "filter";
        case OperatorKind::DualFilter: return "dual_filter";
    }
    return "";
}

const char* op_label(OperatorKind op) {
    switch (op) {
        case OperatorKind::ParametricEQ: return "ParametricEQ";
        case OperatorKind::Compressor: return "Compressor";
        case OperatorKind::Limiter: return "Limiter";
        case OperatorKind::Filter: return "Filter";
        case OperatorKind::DualFilter: return "DualFilter";
    }
    return "";
}

int find_param(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->param_count; ++p) {
        if (std::strcmp(desc->params[p].name, name) == 0)
            return static_cast<int>(p);
    }
    return -1;
}

std::vector<float> make_params(const VividOperatorDescriptor* desc,
                               const std::vector<ParamOverride>& overrides) {
    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; ++p)
        params[p] = desc->params[p].default_value;
    for (const auto& ov : overrides) {
        const int idx = find_param(desc, ov.name);
        if (idx >= 0) params[static_cast<size_t>(idx)] = ov.value;
    }
    return params;
}

float base_sample(SignalKind kind, uint64_t n) {
    const float t = static_cast<float>(n) / static_cast<float>(kSampleRate);
    switch (kind) {
        case SignalKind::Gentle:
            return 0.18f * std::sin(2.0f * kPi * 220.0f * t)
                 + 0.07f * std::sin(2.0f * kPi * 660.0f * t);
        case SignalKind::OverCeiling:
            return 1.25f * std::sin(2.0f * kPi * 110.0f * t)
                 + 0.22f * std::sin(2.0f * kPi * 880.0f * t);
        case SignalKind::Transient:
            return (n % 257 == 0) ? 1.4f
                 : 0.24f * std::sin(2.0f * kPi * 180.0f * t);
        case SignalKind::Noise: {
            uint32_t state = static_cast<uint32_t>(n + 1) * 1664525u + 1013904223u;
            state ^= state >> 16;
            const float noise = static_cast<float>(state & 0xffffu) / 32768.0f - 1.0f;
            return 0.32f * noise
                 + 0.12f * std::sin(2.0f * kPi * 520.0f * t);
        }
        case SignalKind::SineMix:
        default:
            return 0.36f * std::sin(2.0f * kPi * 130.0f * t)
                 + 0.22f * std::sin(2.0f * kPi * 720.0f * t)
                 + 0.08f * std::sin(2.0f * kPi * 2400.0f * t);
    }
}

struct PreparedInput {
    std::vector<float> input0;
    std::vector<float> input1;
    std::vector<float> input2;
    std::vector<float> input3;
    std::vector<float> input4;
    std::vector<float> output;
    std::vector<float> lane0;
    std::vector<float> lane1;
    std::vector<float> lane2;
};

PreparedInput prepare_input(const Case& tc) {
    const int total_blocks = kWarmupBlocks + kMeasureBlocks;
    const size_t total_frames = static_cast<size_t>(total_blocks) * tc.frames;
    PreparedInput prepared;
    prepared.input0.resize(total_frames);
    prepared.input1.assign(total_frames, 0.0f);
    prepared.input2.assign(total_frames, 0.0f);
    prepared.input3.assign(total_frames, 0.0f);
    prepared.input4.assign(total_frames, 0.0f);
    prepared.output.assign(tc.frames, 0.0f);
    prepared.lane0 = {0.18f};
    prepared.lane1 = {-0.12f};
    prepared.lane2 = {330.0f};

    for (int block = 0; block < total_blocks; ++block) {
        for (uint32_t i = 0; i < tc.frames; ++i) {
            const size_t idx = static_cast<size_t>(block) * tc.frames + i;
            prepared.input0[idx] = base_sample(tc.signal, static_cast<uint64_t>(idx));
            if (tc.connect_aux_audio) {
                const float t = static_cast<float>(idx) / static_cast<float>(kSampleRate);
                prepared.input1[idx] = 0.55f * std::sin(2.0f * kPi * 92.0f * t)
                                     + ((idx % 191) == 0 ? 0.45f : 0.0f);
            } else if (tc.use_freq_cv) {
                prepared.input1[idx] = 4.0f;
            } else if (tc.use_filter_cv) {
                prepared.input1[idx] = 2.5f;
            }
            if (tc.use_threshold_cv)
                prepared.input2[idx] = -3.0f;
            else if (tc.use_filter_cv)
                prepared.input2[idx] = 0.12f;

            if (tc.op == OperatorKind::DualFilter && tc.use_filter_cv) {
                prepared.input2[idx] = -2.0f;
                prepared.input3[idx] = 0.10f;
                prepared.input4[idx] = -0.08f;
            }
        }
    }

    return prepared;
}

double run_once(vivid::OperatorLoader& loader, const Case& tc) {
    g_lane_states.clear();
    void* inst = loader.create_instance();
    const auto* desc = loader.descriptor();
    auto params = make_params(desc, tc.params);
    auto prepared = prepare_input(tc);

    float* input_bufs[5] = {};
    float* output_bufs[1] = {prepared.output.data()};

    // Value-view input staging (lane-value 7e.2): size to the op's input port
    // count and place the prepared many-float modulation buffers at the operator's
    // MANY input ordinals (cutoff_mod / frequencies), so the op reads them by port.
    uint32_t in_port_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i)
        if (desc->ports[i].direction == VIVID_PORT_INPUT) ++in_port_count;
    std::vector<VividValueView> value_views(in_port_count, VividValueView{});
    {
        const float* mod_data[3] = {prepared.lane0.data(), prepared.lane1.data(), prepared.lane2.data()};
        const uint32_t mod_len[3] = {static_cast<uint32_t>(prepared.lane0.size()),
                                     static_cast<uint32_t>(prepared.lane1.size()),
                                     static_cast<uint32_t>(prepared.lane2.size())};
        int mod_i = 0;
        uint32_t in_ord = 0;
        for (uint32_t i = 0; i < desc->port_count && mod_i < tc.lane_view_count && mod_i < 3; ++i) {
            if (desc->ports[i].direction != VIVID_PORT_INPUT) continue;
            if (desc->ports[i].multiplicity == VIVID_MULTIPLICITY_MANY && in_ord < value_views.size()) {
                value_views[in_ord] = VividValueView{
                    mod_data[mod_i], mod_len[mod_i], VIVID_VALUE_FLOAT,
                    (mod_len[mod_i] > 1) ? VIVID_MULTIPLICITY_MANY : VIVID_MULTIPLICITY_SCALAR,
                    VIVID_IDENTITY_NONE, VIVID_STORAGE_BRIDGE_SLOT, 0};
                ++mod_i;
            }
            ++in_ord;
        }
    }

    VividAudioContext ctx{};
    ctx.sample_rate = kSampleRate;
    ctx.buffer_size = tc.frames;
    ctx.param_values = params.data();
    ctx.input_buffers = input_bufs;
    ctx.output_buffers = output_bufs;
    ctx.values = value_views.empty() ? nullptr : value_views.data();
    ctx.lane_count = 1;
    ctx.lane_index = 0;
    ctx.lane_id = 1;
    ctx.lane_state_fn = bench_lane_state;

    auto set_block_pointers = [&](int block) {
        const size_t offset = static_cast<size_t>(block) * tc.frames;
        input_bufs[0] = prepared.input0.data() + offset;
        input_bufs[1] = tc.input_count > 1 ? prepared.input1.data() + offset : nullptr;
        input_bufs[2] = tc.input_count > 2 ? prepared.input2.data() + offset : nullptr;
        input_bufs[3] = tc.input_count > 3 ? prepared.input3.data() + offset : nullptr;
        input_bufs[4] = tc.input_count > 4 ? prepared.input4.data() + offset : nullptr;
        ctx.frame = static_cast<uint64_t>(offset);
        ctx.time = static_cast<double>(offset) / static_cast<double>(kSampleRate);
    };

    for (int block = 0; block < kWarmupBlocks; ++block) {
        set_block_pointers(block);
        loader.process_audio(inst, &ctx);
    }

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < kMeasureBlocks; ++block) {
        set_block_pointers(block + kWarmupBlocks);
        loader.process_audio(inst, &ctx);
    }
    const auto end = std::chrono::steady_clock::now();

    loader.destroy_instance(inst);
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<double>(ns) / 1000.0 / static_cast<double>(kMeasureBlocks);
}

Measurement run_case(const Case& tc) {
    vivid::OperatorLoader loader;
    const std::filesystem::path path = g_plugin_dir / (std::string(op_target_name(tc.op)) + VIVID_TEST_PLUGIN_SUFFIX);
    if (!loader.load(path.c_str())) {
        std::fprintf(stderr, "Failed to load %s\n", path.c_str());
        std::exit(1);
    }

    std::vector<double> samples;
    samples.reserve(kRepeats);
    double sum = 0.0;
    for (int i = 0; i < kRepeats; ++i) {
        const double us = run_once(loader, tc);
        samples.push_back(us);
        sum += us;
    }

    Measurement m{};
    m.mean_us = sum / static_cast<double>(kRepeats);
    double variance = 0.0;
    for (double sample : samples) {
        const double d = sample - m.mean_us;
        variance += d * d;
    }
    m.stddev_us = std::sqrt(variance / static_cast<double>(kRepeats));
    return m;
}

std::vector<Case> make_cases() {
    std::vector<Case> cases;
    const uint32_t frames_list[] = {256, 1024};

    for (uint32_t frames : frames_list) {
        cases.push_back({OperatorKind::ParametricEQ, "parametric_eq_1band_peak_static", frames,
                         SignalKind::SineMix, {{"band_count", 1.0f}, {"freq_1", 1000.0f},
                                               {"gain_1", 6.0f}, {"q_1", 1.2f}, {"type_1", 0.0f}},
                         false, false, false, false, false, 2});
        cases.push_back({OperatorKind::ParametricEQ, "parametric_eq_4band_static", frames,
                         SignalKind::SineMix, {{"band_count", 4.0f}, {"gain_1", 4.5f},
                                               {"gain_2", -3.0f}, {"gain_3", 5.0f}, {"gain_4", -2.5f}},
                         false, false, false, false, false, 2});
        cases.push_back({OperatorKind::ParametricEQ, "parametric_eq_freq_cv", frames,
                         SignalKind::SineMix, {{"band_count", 1.0f}, {"freq_1", 700.0f},
                                               {"gain_1", 8.0f}, {"q_1", 2.0f}},
                         false, true, false, false, false, 2});
        cases.push_back({OperatorKind::ParametricEQ, "parametric_eq_mixed_types", frames,
                         SignalKind::SineMix, {{"band_count", 4.0f}, {"type_1", 1.0f}, {"gain_1", 5.0f},
                                               {"type_2", 2.0f}, {"gain_2", -4.0f}, {"type_3", 3.0f},
                                               {"type_4", 4.0f}},
                         false, false, false, false, false, 2});

        cases.push_back({OperatorKind::Compressor, "compressor_no_sidechain_hard_knee", frames,
                         SignalKind::OverCeiling, {{"threshold", -24.0f}, {"ratio", 4.0f},
                                                   {"attack", 5.0f}, {"release", 90.0f}, {"knee", 0.0f}},
                         false, false, false, false, false, 1});
        cases.push_back({OperatorKind::Compressor, "compressor_active_sidechain", frames,
                         SignalKind::Gentle, {{"threshold", -28.0f}, {"ratio", 6.0f},
                                              {"attack", 3.0f}, {"release", 120.0f}, {"knee", 6.0f}},
                         true, false, false, false, false, 3});
        cases.push_back({OperatorKind::Compressor, "compressor_soft_knee", frames,
                         SignalKind::OverCeiling, {{"threshold", -22.0f}, {"ratio", 3.0f},
                                                   {"attack", 10.0f}, {"release", 150.0f}, {"knee", 18.0f}},
                         false, false, false, false, false, 1});
        cases.push_back({OperatorKind::Compressor, "compressor_fast_attack_release", frames,
                         SignalKind::Transient, {{"threshold", -30.0f}, {"ratio", 8.0f},
                                                 {"attack", 0.5f}, {"release", 15.0f}, {"knee", 4.0f}},
                         false, false, true, false, false, 3});

        cases.push_back({OperatorKind::Limiter, "limiter_dry_no_limiting", frames,
                         SignalKind::Gentle, {{"ceiling", 0.0f}, {"release", 100.0f}, {"lookahead", 1.0f}},
                         false, false, false, false, false, 1});
        cases.push_back({OperatorKind::Limiter, "limiter_transient_limiting", frames,
                         SignalKind::Transient, {{"ceiling", -3.0f}, {"release", 60.0f}, {"lookahead", 3.0f}},
                         false, false, false, false, false, 1});
        cases.push_back({OperatorKind::Limiter, "limiter_steady_over_ceiling", frames,
                         SignalKind::OverCeiling, {{"ceiling", -6.0f}, {"release", 80.0f}, {"lookahead", 3.0f}},
                         false, false, false, false, false, 1});
        cases.push_back({OperatorKind::Limiter, "limiter_max_lookahead", frames,
                         SignalKind::Transient, {{"ceiling", -4.0f}, {"release", 50.0f}, {"lookahead", 5.0f}},
                         false, false, false, false, false, 1});

        cases.push_back({OperatorKind::Filter, "filter_lp12_static", frames,
                         SignalKind::Noise, {{"mode", 0.0f}, {"cutoff", 1200.0f}, {"resonance", 0.55f}},
                         false, false, false, false, false, 3, 2});
        cases.push_back({OperatorKind::Filter, "filter_hp24_cv", frames,
                         SignalKind::Noise, {{"mode", 8.0f}, {"cutoff", 900.0f}, {"resonance", 0.45f}},
                         false, false, false, true, false, 3, 2});
        cases.push_back({OperatorKind::Filter, "filter_ladder_lane_mod", frames,
                         SignalKind::Noise, {{"mode", 6.0f}, {"cutoff", 1800.0f}, {"resonance", 0.7f},
                                             {"drive", 0.35f}, {"keytrack", 0.5f}},
                         false, false, false, true, true, 3, 2});
        cases.push_back({OperatorKind::Filter, "filter_formant", frames,
                         SignalKind::Noise, {{"mode", 7.0f}, {"cutoff", 1600.0f}, {"resonance", 0.6f}},
                         false, false, false, false, false, 3, 2});
        cases.push_back({OperatorKind::Filter, "filter_diode", frames,
                         SignalKind::Noise, {{"mode", 12.0f}, {"cutoff", 1400.0f}, {"resonance", 0.75f},
                                             {"drive", 0.5f}},
                         false, false, false, false, false, 3, 2});
        cases.push_back({OperatorKind::Filter, "filter_ms20", frames,
                         SignalKind::Noise, {{"mode", 13.0f}, {"cutoff", 1400.0f}, {"resonance", 0.8f},
                                             {"drive", 0.45f}},
                         false, false, false, false, false, 3, 2});

        cases.push_back({OperatorKind::DualFilter, "dual_filter_serial_lp_hp", frames,
                         SignalKind::Noise, {{"routing", 0.0f}, {"a_mode", 0.0f}, {"b_mode", 8.0f},
                                             {"a_cutoff", 1800.0f}, {"b_cutoff", 4000.0f}},
                         false, false, false, true, true, 5, 3});
        cases.push_back({OperatorKind::DualFilter, "dual_filter_parallel_ladder_formant", frames,
                         SignalKind::Noise, {{"routing", 2.0f}, {"a_mode", 6.0f}, {"b_mode", 7.0f},
                                             {"a_resonance", 0.65f}, {"b_resonance", 0.7f},
                                             {"parallel_balance", 0.55f}},
                         false, false, false, true, true, 5, 3});
        cases.push_back({OperatorKind::DualFilter, "dual_filter_split_diode_ms20", frames,
                         SignalKind::Noise, {{"routing", 3.0f}, {"a_mode", 12.0f}, {"b_mode", 13.0f},
                                             {"split_freq", 1200.0f}, {"a_drive", 0.45f}, {"b_drive", 0.45f}},
                         false, false, false, true, true, 5, 3});
    }

    return cases;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 0 && argv && argv[0]) {
        const std::filesystem::path exe_path(argv[0]);
        const auto parent = exe_path.parent_path();
        if (!parent.empty()) g_plugin_dir = parent;
    }

    std::printf("Filter/dynamics family benchmark: sample_rate=%u measure_blocks=%d repeats=%d backend=operator\n",
                kSampleRate,
                kMeasureBlocks,
                kRepeats);

    for (const auto& tc : make_cases()) {
        const auto m = run_case(tc);
        std::printf("frames=%u op=%s case=%s backend=operator mean_us=%.3f±%.3f\n",
                    tc.frames,
                    op_label(tc.op),
                    tc.name,
                    m.mean_us,
                    m.stddev_us);
    }

    return 0;
}
