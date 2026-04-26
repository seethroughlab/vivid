// Tape operator smoke tests.
// Verifies bypass at mix=0, no NaN/Inf at extreme params, hiss generator
// runs when input is silent, and the DC blocker is effective on a DC offset.

#include "runtime/operators/operator_loader.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "test_helpers.h"

namespace {

constexpr int kFrames = 2048;
constexpr uint32_t kSampleRate = 48000;

// Stub lane-state service: per-(lane_id, byte_size) buffer.
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

struct Harness {
    float input[kFrames]    = {};
    float output[kFrames]   = {};
    float* input_bufs[1]    = {input};
    float* output_bufs[1]   = {output};

    VividAudioContext ctx{};

    Harness() {
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.input_buffers      = input_bufs;
        ctx.output_buffers     = output_bufs;
        ctx.lane_state_fn      = test_lane_state;
        ctx.lane_state_service = nullptr;
        ctx.lane_id            = 1;
    }

    void fill_sine(float freq, float amp) {
        for (int i = 0; i < kFrames; ++i)
            input[i] = amp * std::sin(2.0f * 3.14159265f * freq * i / kSampleRate);
    }

    void fill_dc(float v) {
        std::fill_n(input, kFrames, v);
    }

    void zero_input() { std::memset(input, 0, sizeof(input)); }
    void zero_output() { std::memset(output, 0, sizeof(output)); }

    float rms(const float* buf, int n) const {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += buf[i] * buf[i];
        return static_cast<float>(std::sqrt(s / n));
    }
};

static int find_param(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->param_count; ++p)
        if (std::strcmp(desc->params[p].name, name) == 0) return static_cast<int>(p);
    return -1;
}

struct ParamOverride { const char* name; float value; };
static std::vector<float> make_params(const VividOperatorDescriptor* desc,
                                      std::initializer_list<ParamOverride> ov = {}) {
    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; ++p)
        params[p] = desc->params[p].default_value;
    for (auto& o : ov) {
        int idx = find_param(desc, o.name);
        if (idx >= 0) params[idx] = o.value;
    }
    return params;
}

} // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string tape_path = build_dir + "/tape.dylib";

    if (!std::filesystem::exists(tape_path)) {
        std::fprintf(stderr, "FATAL: %s not found\n", tape_path.c_str());
        return 1;
    }

    vivid::OperatorLoader loader;
    if (!loader.load(tape_path.c_str())) {
        std::fprintf(stderr, "FATAL: failed to load %s\n", tape_path.c_str());
        return 1;
    }

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "tape descriptor not null");
    if (!desc) return 1;
    check(std::strcmp(desc->name, "Tape") == 0, "operator name is Tape");
    check(desc->param_count == 8, "Tape has 8 params");

    // ------------------------------------------------------------------
    // Test 1: mix=0 → output equals input (true bypass).
    // ------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- Tape: mix=0 is true bypass ---\n");
        reset_lane_states();
        Harness h;
        h.fill_sine(440.0f, 0.5f);
        auto params = make_params(desc, {{"mix", 0.0f}});
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();
        check(inst != nullptr, "instance created");
        loader.process_audio(inst, &h.ctx);

        bool match = true;
        float max_err = 0.0f;
        for (int i = 0; i < kFrames; ++i) {
            float err = std::fabs(h.output[i] - h.input[i]);
            if (err > max_err) max_err = err;
            if (err > 1e-6f) match = false;
        }
        check(match, "output matches input bit-for-bit at mix=0");
        std::fprintf(stderr, "  max error: %.3e\n", max_err);
        loader.destroy_instance(inst);
    }

    // ------------------------------------------------------------------
    // Test 2: extreme params → no NaN/Inf.
    // ------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- Tape: extreme params produce finite output ---\n");
        reset_lane_states();
        Harness h;
        h.fill_sine(220.0f, 0.7f);
        auto params = make_params(desc, {
            {"drive", 1.0f},
            {"wow", 1.0f},
            {"flutter", 1.0f},
            {"tone", 0.0f},
            {"rate_reduction", 1.0f},
            {"hiss", 1.0f},
            {"bias", 1.0f},
            {"mix", 1.0f},
        });
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();
        // Drive 4 blocks (~170 ms at 48 kHz) so transients settle.
        for (int b = 0; b < 4; ++b) loader.process_audio(inst, &h.ctx);

        bool finite = true;
        float max_abs = 0.0f;
        for (int i = 0; i < kFrames; ++i) {
            if (!std::isfinite(h.output[i])) finite = false;
            float a = std::fabs(h.output[i]);
            if (a > max_abs) max_abs = a;
        }
        check(finite, "all samples finite under extreme params");
        check(max_abs < 5.0f, "output magnitude bounded (< 5.0)");
        std::fprintf(stderr, "  max |output|: %.3f\n", max_abs);
        loader.destroy_instance(inst);
    }

    // ------------------------------------------------------------------
    // Test 3: hiss-only — input is silence, hiss=1, mix=1 → audible noise.
    // ------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- Tape: hiss generator runs on silent input ---\n");
        reset_lane_states();
        Harness h;
        h.zero_input();
        auto params = make_params(desc, {
            {"drive", 0.0f},
            {"wow", 0.0f},
            {"flutter", 0.0f},
            {"hiss", 1.0f},
            {"rate_reduction", 0.0f},
            {"tone", 1.0f},
            {"mix", 1.0f},
        });
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();
        // Skip first block (DC blocker transient).
        loader.process_audio(inst, &h.ctx);
        loader.process_audio(inst, &h.ctx);
        float rms = h.rms(h.output, kFrames);
        check(rms > 0.001f, "hiss output RMS > 0.001");
        check(rms < 0.05f, "hiss output RMS < 0.05 (sane noise floor)");
        std::fprintf(stderr, "  hiss RMS: %.4f\n", rms);
        loader.destroy_instance(inst);
    }

    // ------------------------------------------------------------------
    // Test 4: DC blocker effective — feed 0.5 DC, output DC ≈ 0.
    // ------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- Tape: DC blocker rejects DC offset ---\n");
        reset_lane_states();
        Harness h;
        h.fill_dc(0.5f);
        auto params = make_params(desc, {
            {"drive", 0.0f},
            {"wow", 0.0f},
            {"flutter", 0.0f},
            {"hiss", 0.0f},
            {"rate_reduction", 0.0f},
            {"tone", 1.0f},
            {"bias", 0.0f},
            {"mix", 1.0f},
        });
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();
        // Drive several blocks so the high-pass settles.
        for (int b = 0; b < 8; ++b) loader.process_audio(inst, &h.ctx);
        // Mean of the last block should be close to zero.
        double mean = 0.0;
        for (int i = 0; i < kFrames; ++i) mean += h.output[i];
        mean /= kFrames;
        check(std::fabs(mean) < 0.05f, "DC blocker brings mean output to ~0");
        std::fprintf(stderr, "  mean(output) after settling: %.4f\n", mean);
        loader.destroy_instance(inst);
    }

    // ------------------------------------------------------------------
    // Test 5: clean chain at minimum settings — output broadly tracks input
    // (with tone LP and small saturation) and isn't silent.
    // ------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- Tape: light chain preserves signal energy ---\n");
        reset_lane_states();
        Harness h;
        h.fill_sine(330.0f, 0.5f);
        auto params = make_params(desc, {
            {"drive", 0.05f},
            {"wow", 0.0f},
            {"flutter", 0.0f},
            {"tone", 1.0f},
            {"rate_reduction", 0.0f},
            {"hiss", 0.0f},
            {"bias", 0.0f},
            {"mix", 1.0f},
        });
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();
        // Skip a couple of blocks for delay-line warmup.
        for (int b = 0; b < 3; ++b) loader.process_audio(inst, &h.ctx);
        float in_rms  = h.rms(h.input, kFrames);
        float out_rms = h.rms(h.output, kFrames);
        check(out_rms > 0.5f * in_rms, "output RMS preserved (>= 50% of input)");
        check(out_rms < 1.5f * in_rms, "output RMS bounded (< 150% of input)");
        std::fprintf(stderr, "  in RMS: %.3f, out RMS: %.3f\n", in_rms, out_rms);
        loader.destroy_instance(inst);
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
