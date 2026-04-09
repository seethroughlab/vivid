// DualFilter operator correctness tests.
// Verifies routing modes, stage enable/disable, parallel balance, split crossover,
// and DSP parity with the single-stage Filter operator.

#include "runtime/operators/operator_loader.h"
#include "runtime/debug/output_analyzer.h"
#include "shared/filter_dsp/filter_dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "test_helpers.h"

// ---------------------------------------------------------------------------
// Stub lane-state service: per-lane_id isolated state buffers
// ---------------------------------------------------------------------------
namespace {

struct LaneStateEntry {
    uint32_t lane_id;
    uint32_t byte_size;
    std::vector<uint8_t> data;
};

static std::vector<LaneStateEntry> g_lane_states;

static void* test_lane_state(void* /*service*/, uint32_t lane_id, uint32_t byte_size) {
    for (auto& e : g_lane_states) {
        if (e.lane_id == lane_id && e.byte_size == byte_size)
            return e.data.data();
    }
    g_lane_states.push_back({lane_id, byte_size, std::vector<uint8_t>(byte_size, 0)});
    return g_lane_states.back().data.data();
}

static void reset_lane_states() { g_lane_states.clear(); }

struct DualFilterLaneStateMirror {
    audio_dsp::FilterState filter_a;
    audio_dsp::FilterState filter_b;
    float xover_z1 = 0.0f;
    float xover_z2 = 0.0f;
};

} // namespace

// ---------------------------------------------------------------------------
// Test context
// ---------------------------------------------------------------------------
struct TestContext {
    static constexpr int kFrames = 2048;
    static constexpr uint32_t kSampleRate = 48000;

    float input[kFrames]    = {};
    float cv_buf_1[kFrames] = {};
    float cv_buf_2[kFrames] = {};
    float cv_buf_3[kFrames] = {};
    float cv_buf_4[kFrames] = {};
    float output[kFrames]   = {};
    float* input_bufs[5]    = {input, cv_buf_1, cv_buf_2, cv_buf_3, cv_buf_4};
    float* output_bufs[1]   = {output};

    VividAudioContext ctx{};

    TestContext() {
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.input_buffers      = input_bufs;
        ctx.output_buffers     = output_bufs;
        ctx.param_values       = nullptr;
        ctx.lane_state_fn      = test_lane_state;
        ctx.lane_state_service = nullptr;
        ctx.lane_id            = 1;
    }

    void fill_sine(float freq, float amp) {
        for (int i = 0; i < kFrames; i++)
            input[i] = amp * std::sin(2.0f * 3.14159265f * freq * i / kSampleRate);
    }

    void fill_noise() {
        uint32_t state = 12345;
        for (int i = 0; i < kFrames; i++) {
            state = state * 1664525u + 1013904223u;
            input[i] = (static_cast<float>(state) / 2147483648.0f) - 1.0f;
        }
    }

    void clear_output() { std::memset(output, 0, sizeof(output)); }

    vivid::AudioMetrics analyze_output() const {
        return vivid::analyze_audio(output, kFrames, kSampleRate, 1);
    }

    float rms_output() const {
        double sum = 0.0;
        for (int i = 0; i < kFrames; i++) sum += output[i] * output[i];
        return std::sqrt(static_cast<float>(sum / kFrames));
    }
};

// ---------------------------------------------------------------------------
// Helper: find param index by name
// ---------------------------------------------------------------------------
static int find_param(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->param_count; p++) {
        if (std::strcmp(desc->params[p].name, name) == 0)
            return static_cast<int>(p);
    }
    return -1;
}

static int count_subnormal_samples(const float* data, int count) {
    int total = 0;
    for (int i = 0; i < count; ++i) {
        if (std::fpclassify(data[i]) == FP_SUBNORMAL) total++;
    }
    return total;
}

static int count_subnormal_state_floats(const DualFilterLaneStateMirror& state) {
    const auto* values = reinterpret_cast<const float*>(&state);
    int total = 0;
    for (size_t i = 0; i < sizeof(DualFilterLaneStateMirror) / sizeof(float); ++i) {
        if (std::fpclassify(values[i]) == FP_SUBNORMAL) total++;
    }
    return total;
}

// ---------------------------------------------------------------------------
// Helper: set up default params with specific overrides
// ---------------------------------------------------------------------------
struct ParamOverride {
    const char* name;
    float value;
};

static std::vector<float> make_params(const VividOperatorDescriptor* desc,
                                       std::initializer_list<ParamOverride> overrides = {}) {
    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; p++)
        params[p] = desc->params[p].default_value;
    for (auto& ov : overrides) {
        int idx = find_param(desc, ov.name);
        if (idx >= 0) params[idx] = ov.value;
    }
    return params;
}

// ---------------------------------------------------------------------------
// Test: Serial A→B with B disabled matches single-stage A behavior
// ---------------------------------------------------------------------------
static void test_serial_ab_b_disabled(vivid::OperatorLoader& dual_loader,
                                       vivid::OperatorLoader& filter_loader,
                                       const std::string& /*staging*/) {
    std::fprintf(stderr, "\n--- DualFilter: serial_ab with B disabled ≈ single Filter ---\n");

    const auto* dd = dual_loader.descriptor();
    const auto* fd = filter_loader.descriptor();

    // DualFilter: serial_ab, B disabled, A = LP12 cutoff=1000
    auto dp = make_params(dd, {
        {"routing", 0.0f},      // serial_ab
        {"a_cutoff", 1000.0f},
        {"a_resonance", 0.3f},
        {"a_drive", 0.0f},
        {"a_mode", 0.0f},      // LP12
        {"a_enabled", 1.0f},
        {"b_enabled", 0.0f},
        {"output_gain", 1.0f},
    });

    // Filter: LP12 cutoff=1000
    auto fp = make_params(fd, {
        {"cutoff", 1000.0f},
        {"resonance", 0.3f},
        {"drive", 0.0f},
        {"mode", 0.0f},  // LP12
    });

    reset_lane_states();
    void* dinst = dual_loader.create_instance();
    void* finst = filter_loader.create_instance();

    TestContext dtc, ftc;
    dtc.ctx.param_values = dp.data();
    ftc.ctx.param_values = fp.data();

    // Process several buffers of noise through each
    for (int b = 0; b < 6; b++) {
        dtc.fill_noise();
        std::memcpy(ftc.input, dtc.input, sizeof(dtc.input));
        dtc.clear_output();
        ftc.clear_output();
        dual_loader.process_audio(dinst, &dtc.ctx);
        filter_loader.process_audio(finst, &ftc.ctx);
    }

    auto dm = dtc.analyze_output();
    auto fm = ftc.analyze_output();
    std::fprintf(stderr, "  dual rms=%.4f brightness=%.4f  filter rms=%.4f brightness=%.4f\n",
                 dm.rms, dm.spectral_brightness, fm.rms, fm.spectral_brightness);

    check_float(dm.rms, fm.rms, 0.02f, "DualFilter(A-only) RMS matches Filter");
    check_float(dm.spectral_brightness, fm.spectral_brightness, 0.05f,
                "DualFilter(A-only) brightness matches Filter");

    dual_loader.destroy_instance(dinst);
    filter_loader.destroy_instance(finst);
}

// ---------------------------------------------------------------------------
// Test: Serial A→B vs Serial B→A produce different output
// ---------------------------------------------------------------------------
static void test_serial_ordering(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- DualFilter: serial_ab ≠ serial_ba ---\n");

    const auto* desc = loader.descriptor();

    // A=LP24@1500 (steep lowpass), B=Ladder@6000 (resonant character)
    auto base_params = make_params(desc, {
        {"a_cutoff", 1500.0f},
        {"a_mode", 1.0f},     // LP24
        {"a_resonance", 0.7f},
        {"b_cutoff", 6000.0f},
        {"b_mode", 6.0f},     // Ladder
        {"b_resonance", 0.6f},
        {"a_enabled", 1.0f},
        {"b_enabled", 1.0f},
        {"output_gain", 1.0f},
    });

    // Run serial_ab
    reset_lane_states();
    auto params_ab = base_params;
    params_ab[find_param(desc, "routing")] = 0.0f;  // serial_ab

    void* inst_ab = loader.create_instance();
    TestContext tc_ab;
    tc_ab.ctx.param_values = params_ab.data();
    for (int b = 0; b < 6; b++) {
        tc_ab.fill_noise();
        tc_ab.clear_output();
        loader.process_audio(inst_ab, &tc_ab.ctx);
    }
    auto m_ab = tc_ab.analyze_output();

    // Run serial_ba
    reset_lane_states();
    auto params_ba = base_params;
    params_ba[find_param(desc, "routing")] = 1.0f;  // serial_ba

    void* inst_ba = loader.create_instance();
    TestContext tc_ba;
    tc_ba.ctx.param_values = params_ba.data();
    tc_ba.ctx.lane_id = 2;
    for (int b = 0; b < 6; b++) {
        tc_ba.fill_noise();
        tc_ba.clear_output();
        loader.process_audio(inst_ba, &tc_ba.ctx);
    }
    auto m_ba = tc_ba.analyze_output();

    std::fprintf(stderr, "  ab: rms=%.4f centroid=%.1fHz  ba: rms=%.4f centroid=%.1fHz\n",
                 m_ab.rms, m_ab.spectral_centroid_hz, m_ba.rms, m_ba.spectral_centroid_hz);

    // The spectral centroid should differ meaningfully
    float centroid_diff = std::fabs(m_ab.spectral_centroid_hz - m_ba.spectral_centroid_hz);
    check(centroid_diff > 50.0f, "serial_ab vs serial_ba produce different spectral centroid");

    loader.destroy_instance(inst_ab);
    loader.destroy_instance(inst_ba);
}

// ---------------------------------------------------------------------------
// Test: Parallel balance — 0=all A, 1=all B
// ---------------------------------------------------------------------------
static void test_parallel_balance(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- DualFilter: parallel balance ---\n");

    const auto* desc = loader.descriptor();

    // A=LP12@500 (dark), B=HP12@500 (bright)
    auto base_params = make_params(desc, {
        {"routing", 2.0f},    // parallel
        {"a_cutoff", 500.0f},
        {"a_mode", 0.0f},    // LP12
        {"b_cutoff", 500.0f},
        {"b_mode", 2.0f},    // HP12
        {"a_enabled", 1.0f},
        {"b_enabled", 1.0f},
        {"output_gain", 1.0f},
    });

    int bal_idx = find_param(desc, "parallel_balance");

    // Balance = 0 (all A — LP, low brightness)
    reset_lane_states();
    auto params_a = base_params;
    params_a[bal_idx] = 0.0f;
    void* inst_a = loader.create_instance();
    TestContext tc_a;
    tc_a.ctx.param_values = params_a.data();
    for (int b = 0; b < 6; b++) {
        tc_a.fill_noise();
        tc_a.clear_output();
        loader.process_audio(inst_a, &tc_a.ctx);
    }
    auto m_a = tc_a.analyze_output();

    // Balance = 1 (all B — HP, high brightness)
    reset_lane_states();
    auto params_b = base_params;
    params_b[bal_idx] = 1.0f;
    void* inst_b = loader.create_instance();
    TestContext tc_b;
    tc_b.ctx.param_values = params_b.data();
    tc_b.ctx.lane_id = 2;
    for (int b = 0; b < 6; b++) {
        tc_b.fill_noise();
        tc_b.clear_output();
        loader.process_audio(inst_b, &tc_b.ctx);
    }
    auto m_b = tc_b.analyze_output();

    std::fprintf(stderr, "  bal=0 brightness=%.4f  bal=1 brightness=%.4f\n",
                 m_a.spectral_brightness, m_b.spectral_brightness);

    check(m_b.spectral_brightness > m_a.spectral_brightness,
          "parallel: bal=1 (HP) brighter than bal=0 (LP)");

    loader.destroy_instance(inst_a);
    loader.destroy_instance(inst_b);
}

// ---------------------------------------------------------------------------
// Test: Split mode routes low frequencies through A, high through B
// ---------------------------------------------------------------------------
static void test_split_routing(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- DualFilter: split crossover routing ---\n");

    const auto* desc = loader.descriptor();

    // Split at 2000Hz, both filters in LP12 with different cutoffs
    // A: LP12@20000 (passthrough-ish), B: LP12@20000 (passthrough-ish)
    // Feed 200Hz sine (below split) → should come mostly from A path
    // Feed 8000Hz sine (above split) → should come mostly from B path

    // To test: disable B, only A active — low sine should pass, high sine should be silent
    auto params_a_only = make_params(desc, {
        {"routing", 3.0f},       // split
        {"split_freq", 2000.0f},
        {"a_cutoff", 20000.0f},
        {"a_mode", 0.0f},       // LP12 (near passthrough at 20k)
        {"a_enabled", 1.0f},
        {"b_enabled", 0.0f},    // B disabled → silence from B path
        {"output_gain", 1.0f},
    });

    // Low sine (200Hz) → below split → through A → should have signal
    reset_lane_states();
    void* inst_low = loader.create_instance();
    TestContext tc_low;
    tc_low.ctx.param_values = params_a_only.data();
    for (int b = 0; b < 6; b++) {
        tc_low.fill_sine(200.0f, 0.8f);
        tc_low.clear_output();
        loader.process_audio(inst_low, &tc_low.ctx);
    }
    float rms_low_a = tc_low.rms_output();

    // High sine (8000Hz) → above split → through B (disabled=silence) → mostly silent
    reset_lane_states();
    void* inst_high = loader.create_instance();
    TestContext tc_high;
    tc_high.ctx.param_values = params_a_only.data();
    tc_high.ctx.lane_id = 2;
    for (int b = 0; b < 6; b++) {
        tc_high.fill_sine(8000.0f, 0.8f);
        tc_high.clear_output();
        loader.process_audio(inst_high, &tc_high.ctx);
    }
    float rms_high_a = tc_high.rms_output();

    std::fprintf(stderr, "  A-only: low_rms=%.4f high_rms=%.4f\n", rms_low_a, rms_high_a);
    check(rms_low_a > 0.1f, "split: low sine passes through A path");
    check(rms_high_a < rms_low_a * 0.3f, "split: high sine mostly silent when B disabled");

    loader.destroy_instance(inst_low);
    loader.destroy_instance(inst_high);
}

// ---------------------------------------------------------------------------
// Test: split_freq meaningfully shifts energy between low and high branches
// ---------------------------------------------------------------------------
static void test_split_freq_mapping(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- DualFilter: split_freq mapping ---\n");

    const auto* desc = loader.descriptor();
    constexpr float kMidFreq = 1500.0f;  // Above 500 Hz, below 4000 Hz

    auto params_low_branch = [&](float split_hz) {
        return make_params(desc, {
            {"routing", 3.0f},
            {"split_freq", split_hz},
            {"a_cutoff", 20000.0f},
            {"a_mode", 0.0f},
            {"a_enabled", 1.0f},
            {"b_enabled", 0.0f},
            {"output_gain", 1.0f},
        });
    };

    auto params_high_branch = [&](float split_hz) {
        return make_params(desc, {
            {"routing", 3.0f},
            {"split_freq", split_hz},
            {"b_cutoff", 20000.0f},
            {"b_mode", 0.0f},
            {"a_enabled", 0.0f},
            {"b_enabled", 1.0f},
            {"output_gain", 1.0f},
        });
    };

    auto run_rms = [&](std::vector<float>& params, uint32_t lane_id) {
        reset_lane_states();
        void* inst = loader.create_instance();
        TestContext tc;
        tc.ctx.param_values = params.data();
        tc.ctx.lane_id = lane_id;
        for (int b = 0; b < 6; b++) {
            tc.fill_sine(kMidFreq, 0.8f);
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        float rms = tc.rms_output();
        loader.destroy_instance(inst);
        return rms;
    };

    auto low_split_low_branch = params_low_branch(500.0f);
    auto high_split_low_branch = params_low_branch(4000.0f);
    float rms_low_branch_500 = run_rms(low_split_low_branch, 1);
    float rms_low_branch_4000 = run_rms(high_split_low_branch, 2);

    auto low_split_high_branch = params_high_branch(500.0f);
    auto high_split_high_branch = params_high_branch(4000.0f);
    float rms_high_branch_500 = run_rms(low_split_high_branch, 3);
    float rms_high_branch_4000 = run_rms(high_split_high_branch, 4);

    std::fprintf(stderr,
                 "  low-branch rms: split=500 -> %.4f, split=4000 -> %.4f\n"
                 "  high-branch rms: split=500 -> %.4f, split=4000 -> %.4f\n",
                 rms_low_branch_500, rms_low_branch_4000,
                 rms_high_branch_500, rms_high_branch_4000);

    check(rms_low_branch_4000 > rms_low_branch_500 * 1.5f,
          "raising split_freq increases low-branch energy for a mid-band sine");
    check(rms_high_branch_500 > rms_high_branch_4000 * 1.5f,
          "raising split_freq decreases high-branch energy for a mid-band sine");
}

// ---------------------------------------------------------------------------
// Test: Stage disable in serial = passthrough
// ---------------------------------------------------------------------------
static void test_serial_disable_passthrough(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- DualFilter: disabled stage = passthrough in serial ---\n");

    const auto* desc = loader.descriptor();

    // Both disabled in serial → pure passthrough
    auto params = make_params(desc, {
        {"routing", 0.0f},
        {"a_enabled", 0.0f},
        {"b_enabled", 0.0f},
        {"output_gain", 1.0f},
    });

    reset_lane_states();
    void* inst = loader.create_instance();
    TestContext tc;
    tc.ctx.param_values = params.data();
    tc.fill_sine(440.0f, 0.5f);
    tc.clear_output();
    loader.process_audio(inst, &tc.ctx);

    // Output should match input
    float max_diff = 0.0f;
    for (int i = 0; i < TestContext::kFrames; i++) {
        float diff = std::fabs(tc.output[i] - tc.input[i]);
        if (diff > max_diff) max_diff = diff;
    }

    std::fprintf(stderr, "  max_diff from passthrough=%.6f\n", max_diff);
    check(max_diff < 1e-5f, "serial: both disabled = exact passthrough");

    loader.destroy_instance(inst);
}

// ---------------------------------------------------------------------------
// Test: Output gain scales correctly
// ---------------------------------------------------------------------------
static void test_output_gain(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- DualFilter: output gain ---\n");

    const auto* desc = loader.descriptor();

    // Both disabled (passthrough), output_gain=0.5 → half amplitude
    auto params = make_params(desc, {
        {"routing", 0.0f},
        {"a_enabled", 0.0f},
        {"b_enabled", 0.0f},
        {"output_gain", 0.5f},
    });

    reset_lane_states();
    void* inst = loader.create_instance();
    TestContext tc;
    tc.ctx.param_values = params.data();
    tc.fill_sine(440.0f, 0.8f);
    tc.clear_output();
    loader.process_audio(inst, &tc.ctx);

    float in_rms = 0.0f, out_rms = 0.0f;
    for (int i = 0; i < TestContext::kFrames; i++) {
        in_rms += tc.input[i] * tc.input[i];
        out_rms += tc.output[i] * tc.output[i];
    }
    in_rms = std::sqrt(in_rms / TestContext::kFrames);
    out_rms = std::sqrt(out_rms / TestContext::kFrames);

    std::fprintf(stderr, "  in_rms=%.4f out_rms=%.4f expected=%.4f\n",
                 in_rms, out_rms, in_rms * 0.5f);
    check_float(out_rms, in_rms * 0.5f, 0.01f, "output_gain=0.5 halves amplitude");

    loader.destroy_instance(inst);
}

// ---------------------------------------------------------------------------
// Test: Parallel with one stage disabled contributes silence
// ---------------------------------------------------------------------------
static void test_parallel_disable_silence(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- DualFilter: disabled stage = silence in parallel ---\n");

    const auto* desc = loader.descriptor();

    // Parallel, B disabled, balance=1 (all B) → should be silent
    auto params = make_params(desc, {
        {"routing", 2.0f},
        {"a_enabled", 1.0f},
        {"b_enabled", 0.0f},
        {"parallel_balance", 1.0f},  // all B
        {"output_gain", 1.0f},
    });

    reset_lane_states();
    void* inst = loader.create_instance();
    TestContext tc;
    tc.ctx.param_values = params.data();
    tc.fill_sine(440.0f, 0.8f);
    tc.clear_output();

    // Process a few buffers for stabilization
    for (int b = 0; b < 4; b++) {
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
    }

    float rms = tc.rms_output();
    std::fprintf(stderr, "  parallel bal=1 B-disabled rms=%.6f\n", rms);
    check(rms < 0.001f, "parallel: disabled B at bal=1 produces silence");

    loader.destroy_instance(inst);
}

// ---------------------------------------------------------------------------
// Test: long decays do not leave subnormal filter state behind
// ---------------------------------------------------------------------------
static void test_denormal_tail_recovery(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- DualFilter: long tails flush denormals ---\n");

    const auto* desc = loader.descriptor();
    auto params = make_params(desc, {
        {"routing", 3.0f},
        {"split_freq", 1200.0f},
        {"a_mode", 6.0f},         // Ladder
        {"a_cutoff", 900.0f},
        {"a_resonance", 0.94f},
        {"b_mode", 13.0f},        // MS-20
        {"b_cutoff", 2600.0f},
        {"b_resonance", 0.92f},
        {"a_enabled", 1.0f},
        {"b_enabled", 1.0f},
        {"output_gain", 1.0f},
    });

    reset_lane_states();
    void* inst = loader.create_instance();
    TestContext tc;
    tc.ctx.param_values = params.data();

    for (int b = 0; b < 8; ++b) {
        tc.fill_noise();
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
    }

    std::memset(tc.input, 0, sizeof(tc.input));
    int output_subnormals = 0;
    for (int b = 0; b < 320; ++b) {
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
        output_subnormals += count_subnormal_samples(tc.output, TestContext::kFrames);
    }

    int state_subnormals = 0;
    if (!g_lane_states.empty() &&
        g_lane_states.front().byte_size == sizeof(DualFilterLaneStateMirror)) {
        const auto* state = reinterpret_cast<const DualFilterLaneStateMirror*>(
            g_lane_states.front().data.data());
        state_subnormals = count_subnormal_state_floats(*state);
    }

    std::fprintf(stderr, "  output_subnormals=%d state_subnormals=%d\n",
                 output_subnormals, state_subnormals);
    check(output_subnormals == 0, "long filter tails do not emit subnormal output samples");
    check(state_subnormals == 0, "long filter tails flush subnormal recursive state");

    loader.destroy_instance(inst);
}

// ---------------------------------------------------------------------------
// Test: Descriptor has expected params and ports
// ---------------------------------------------------------------------------
static void test_descriptor(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- DualFilter: descriptor ---\n");

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor loaded");
    if (!desc) return;

    check(std::strcmp(desc->name, "DualFilter") == 0, "name = DualFilter");

    // Check key params exist
    check(find_param(desc, "a_enabled") >= 0, "has a_enabled param");
    check(find_param(desc, "a_mode") >= 0, "has a_mode param");
    check(find_param(desc, "a_cutoff") >= 0, "has a_cutoff param");
    check(find_param(desc, "a_resonance") >= 0, "has a_resonance param");
    check(find_param(desc, "a_drive") >= 0, "has a_drive param");
    check(find_param(desc, "a_keytrack") >= 0, "has a_keytrack param");
    check(find_param(desc, "b_enabled") >= 0, "has b_enabled param");
    check(find_param(desc, "b_cutoff") >= 0, "has b_cutoff param");
    check(find_param(desc, "routing") >= 0, "has routing param");
    check(find_param(desc, "parallel_balance") >= 0, "has parallel_balance param");
    check(find_param(desc, "split_freq") >= 0, "has split_freq param");
    check(find_param(desc, "output_gain") >= 0, "has output_gain param");

    // Check port count (input, output, 4 CV, 3 lane-array, + analysis ports)
    check(desc->port_count >= 8, "at least 8 ports (audio + CV + lane)");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::string build_dir = ".";

    std::string staging = build_dir + "/.test_dual_filter_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    const char* ops[] = {"dual_filter", "filter"};
    for (const char* op : ops) {
        std::string src = build_dir + "/" + op + ".dylib";
        std::string dst = staging + "/" + op + ".dylib";
        if (std::filesystem::exists(src)) {
            std::filesystem::copy_file(src, dst,
                std::filesystem::copy_options::overwrite_existing);
        } else {
            std::fprintf(stderr, "  WARN: %s not found, tests may fail\n", src.c_str());
        }
    }

    vivid::OperatorLoader dual_loader;
    if (!dual_loader.load((staging + "/dual_filter.dylib").c_str())) {
        std::fprintf(stderr, "FATAL: could not load dual_filter.dylib\n");
        return 1;
    }

    vivid::OperatorLoader filter_loader;
    if (!filter_loader.load((staging + "/filter.dylib").c_str())) {
        std::fprintf(stderr, "WARN: could not load filter.dylib (parity tests will fail)\n");
    }

    std::fprintf(stderr, "\n=== Test: DualFilter Correctness ===\n");

    test_descriptor(dual_loader);
    if (filter_loader.descriptor())
        test_serial_ab_b_disabled(dual_loader, filter_loader, staging);
    test_serial_ordering(dual_loader);
    test_parallel_balance(dual_loader);
    test_split_routing(dual_loader);
    test_split_freq_mapping(dual_loader);
    test_serial_disable_passthrough(dual_loader);
    test_output_gain(dual_loader);
    test_parallel_disable_silence(dual_loader);
    test_denormal_tail_recovery(dual_loader);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
