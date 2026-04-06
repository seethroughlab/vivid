// Tests for modulation audio operators: Flanger, Chorus, Phaser.
// Exercises load, silence, dry pass-through, wet signal, extreme params,
// and DC stability (flanger).

#include "runtime/operators/operator_loader.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>
#include "test_helpers.h"

static float rms(const float* buf, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += static_cast<double>(buf[i]) * buf[i];
    return static_cast<float>(std::sqrt(sum / n));
}

static bool is_finite(const float* buf, int n) {
    for (int i = 0; i < n; i++) {
        if (!std::isfinite(buf[i])) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Test context helper
// ---------------------------------------------------------------------------

struct TestContext {
    static constexpr int kFrames = 512;
    static constexpr uint32_t kSampleRate = 44100;

    float input[kFrames]      = {};
    float rate_cv[kFrames]    = {};
    float beat_phase[kFrames] = {};
    float output[kFrames]     = {};
    float* input_bufs[3]      = {input, rate_cv, beat_phase};
    float* output_bufs[1]     = {output};

    VividAudioContext ctx{};

    TestContext() {
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.input_buffers      = input_bufs;
        ctx.output_buffers     = output_bufs;
        ctx.param_values       = nullptr;
    }

    void fill_sine(float freq, float amp) {
        for (int i = 0; i < kFrames; i++)
            input[i] = amp * std::sin(2.0f * 3.14159265f * freq * i / kSampleRate);
    }

    void fill_silence() {
        std::memset(input, 0, sizeof(input));
    }

    void clear_inputs() {
        std::memset(rate_cv, 0, sizeof(rate_cv));
        std::memset(beat_phase, 0, sizeof(beat_phase));
    }

    void clear_output() {
        std::memset(output, 0, sizeof(output));
    }
};

// ---------------------------------------------------------------------------
// Generic operator test suite
// ---------------------------------------------------------------------------

struct OpInfo {
    const char* name;
    const char* dylib;
    int expected_param_count;
    int expected_port_count;
};

static void test_operator(const std::string& staging, const OpInfo& info) {
    std::fprintf(stderr, "\n--- %s ---\n", info.name);

    // 1. Load test
    vivid::OperatorLoader loader;
    std::string path = staging + "/" + info.dylib;
    check(loader.load(path.c_str()), "load dylib");
    check(loader.is_loaded(), "is_loaded");

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor not null");
    if (!desc) return;

    check(std::strcmp(desc->name, info.name) == 0, "descriptor name matches");
    check(vivid_operator_kind(desc) == VIVID_OP_AUDIO, "env = AUDIO");
    check(desc->has_process_audio == 1, "has_process_audio");
    check(static_cast<int>(desc->param_count) == info.expected_param_count,
          "param_count matches");
    check(static_cast<int>(desc->port_count) == info.expected_port_count,
          "port_count matches");

    // Verify port names: input, output, rate_cv
    bool has_input = false, has_output = false, has_rate_cv = false, has_beat_phase = false;
    for (uint32_t p = 0; p < desc->port_count; p++) {
        if (std::strcmp(desc->ports[p].name, "input") == 0)   has_input = true;
        if (std::strcmp(desc->ports[p].name, "output") == 0)  has_output = true;
        if (std::strcmp(desc->ports[p].name, "rate_cv") == 0) has_rate_cv = true;
        if (std::strcmp(desc->ports[p].name, "beat_phase") == 0) has_beat_phase = true;
    }
    check(has_input, "has input port");
    check(has_output, "has output port");
    check(has_rate_cv, "has rate_cv port");
    check(has_beat_phase, "has beat_phase port");

    // Verify all float params have VIVID_DISPLAY_KNOB hint
    for (uint32_t p = 0; p < desc->param_count; p++) {
        if (desc->params[p].type == VIVID_PARAM_FLOAT) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "param '%s' has KNOB display hint",
                         desc->params[p].name);
            check(desc->params[p].display_hint == VIVID_DISPLAY_KNOB, msg);
        }
    }

    // 2. Silence in → silence out
    {
        void* inst = loader.create_instance();
        check(inst != nullptr, "create_instance");
        if (!inst) return;

        TestContext tc;
        tc.fill_silence();
        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        float r = rms(tc.output, TestContext::kFrames);
        check(r < 0.001f, "silence in -> near-silence out");
        check(is_finite(tc.output, TestContext::kFrames), "silence output finite");
        loader.destroy_instance(inst);
    }

    // 3. Dry pass-through (mix=0)
    {
        void* inst = loader.create_instance();
        TestContext tc;

        // Find mix param index
        int mix_idx = -1;
        for (uint32_t p = 0; p < desc->param_count; p++) {
            if (std::strcmp(desc->params[p].name, "mix") == 0) {
                mix_idx = static_cast<int>(p);
                break;
            }
        }
        check(mix_idx >= 0, "mix param found");

        if (mix_idx >= 0) {
            std::vector<float> param_vals(desc->param_count);
            for (uint32_t p = 0; p < desc->param_count; p++)
                param_vals[p] = desc->params[p].default_value;
            param_vals[mix_idx] = 0.0f;
            tc.ctx.param_values = param_vals.data();

            // Process a few buffers for init to settle
            tc.fill_sine(440.0f, 0.5f);
            for (int b = 0; b < 3; b++) {
                tc.clear_output();
                loader.process_audio(inst, &tc.ctx);
            }

            // Fresh input + process
            tc.fill_sine(440.0f, 0.5f);
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);

            float max_diff = 0.0f;
            for (int i = 0; i < TestContext::kFrames; i++) {
                float diff = std::fabs(tc.output[i] - tc.input[i]);
                if (diff > max_diff) max_diff = diff;
            }
            check(max_diff < 0.01f, "mix=0 dry pass-through");
        }
        loader.destroy_instance(inst);
    }

    // 4. Wet produces signal (default params)
    {
        void* inst = loader.create_instance();
        TestContext tc;

        std::vector<float> param_vals(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].default_value;
        tc.ctx.param_values = param_vals.data();

        for (int b = 0; b < 8; b++) {
            tc.fill_sine(440.0f, 0.5f);
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }

        float r = rms(tc.output, TestContext::kFrames);
        check(r > 0.01f, "wet signal RMS > 0");
        check(is_finite(tc.output, TestContext::kFrames), "wet output finite");
        loader.destroy_instance(inst);
    }

    // 5. Extreme params — min/max values, no NaN/inf/crash
    {
        void* inst = loader.create_instance();
        TestContext tc;
        tc.fill_sine(440.0f, 0.8f);

        std::vector<float> param_vals(desc->param_count);

        // All at minimum
        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].min_value;
        tc.ctx.param_values = param_vals.data();

        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, TestContext::kFrames), "min params -> finite output");

        // All at maximum
        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].max_value;

        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, TestContext::kFrames), "max params -> finite output");
        loader.destroy_instance(inst);
    }
}

static void set_metronome(TestContext& tc, bool enabled, float bpm,
                          uint32_t beats_per_bar, double beats_elapsed,
                          float beat_phase, float bar_phase) {
    tc.ctx.metronome_enabled = enabled ? 1u : 0u;
    tc.ctx.metronome_bpm = bpm;
    tc.ctx.metronome_beats_per_bar = beats_per_bar;
    tc.ctx.metronome_beats_elapsed = beats_elapsed;
    tc.ctx.metronome_beat_phase = beat_phase;
    tc.ctx.metronome_bar_phase = bar_phase;
    tc.ctx.metronome_beat_ms = bpm > 0.0f ? 60000.0f / bpm : 0.0f;
}

static void test_metronome_sync_ignores_free_rate(const std::string& staging,
                                                  const char* dylib_name) {
    vivid::OperatorLoader loader;
    std::string path = staging + "/" + dylib_name;
    check(loader.load(path.c_str()), "load modulation dylib for metronome sync");
    if (!loader.is_loaded()) return;

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor available for metronome test");
    if (!desc) return;

    int rate_idx = -1;
    int rate_mode_idx = -1;
    int sync_div_idx = -1;
    int mix_idx = -1;
    for (uint32_t p = 0; p < desc->param_count; ++p) {
        if (std::strcmp(desc->params[p].name, "rate") == 0) rate_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "rate_mode") == 0) rate_mode_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "sync_division") == 0) sync_div_idx = static_cast<int>(p);
        if (std::strcmp(desc->params[p].name, "mix") == 0) mix_idx = static_cast<int>(p);
    }
    check(rate_idx >= 0 && rate_mode_idx >= 0 && sync_div_idx >= 0 && mix_idx >= 0,
          "metronome sync params are present");
    if (rate_idx < 0 || rate_mode_idx < 0 || sync_div_idx < 0 || mix_idx < 0) return;

    void* a = loader.create_instance();
    void* b = loader.create_instance();
    check(a != nullptr && b != nullptr, "create modulation instances");
    if (!a || !b) return;

    TestContext slow;
    TestContext fast;
    slow.fill_sine(220.0f, 0.5f);
    fast.fill_sine(220.0f, 0.5f);
    slow.clear_inputs();
    fast.clear_inputs();
    set_metronome(slow, true, 120.0f, 4, 0.375, 0.375f, 0.09375f);
    set_metronome(fast, true, 120.0f, 4, 0.375, 0.375f, 0.09375f);

    std::vector<float> slow_params(desc->param_count);
    std::vector<float> fast_params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; ++p) {
        slow_params[p] = desc->params[p].default_value;
        fast_params[p] = desc->params[p].default_value;
    }
    slow_params[rate_mode_idx] = 2.0f;  // metronome
    fast_params[rate_mode_idx] = 2.0f;  // metronome
    slow_params[sync_div_idx] = 2.0f;   // quarter notes
    fast_params[sync_div_idx] = 2.0f;
    slow_params[rate_idx] = desc->params[rate_idx].min_value;
    fast_params[rate_idx] = desc->params[rate_idx].max_value;
    slow_params[mix_idx] = 1.0f;
    fast_params[mix_idx] = 1.0f;
    slow.ctx.param_values = slow_params.data();
    fast.ctx.param_values = fast_params.data();

    loader.process_audio(a, &slow.ctx);
    loader.process_audio(b, &fast.ctx);

    float max_diff = 0.0f;
    for (int i = 0; i < TestContext::kFrames; ++i) {
        max_diff = std::max(max_diff, std::fabs(slow.output[i] - fast.output[i]));
    }
    check(max_diff < 1e-5f, "metronome mode ignores the free-running rate knob");

    loader.destroy_instance(a);
    loader.destroy_instance(b);
}

// ---------------------------------------------------------------------------
// Flanger-specific: DC stability under high feedback
// ---------------------------------------------------------------------------

static void test_flanger_dc_stability(const std::string& staging) {
    std::fprintf(stderr, "\n--- Flanger DC stability ---\n");

    vivid::OperatorLoader loader;
    std::string path = staging + "/flanger.dylib";
    if (!loader.load(path.c_str())) {
        std::fprintf(stderr, "  SKIP: could not load flanger\n");
        return;
    }

    const auto* desc = loader.descriptor();
    if (!desc) return;

    void* inst = loader.create_instance();
    if (!inst) return;

    TestContext tc;
    tc.fill_silence();

    std::vector<float> param_vals(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; p++)
        param_vals[p] = desc->params[p].default_value;

    for (uint32_t p = 0; p < desc->param_count; p++) {
        if (std::strcmp(desc->params[p].name, "feedback") == 0)
            param_vals[p] = 0.9f;
    }
    tc.ctx.param_values = param_vals.data();

    bool bounded = true;
    for (int b = 0; b < 100; b++) {
        tc.fill_silence();
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
        for (int i = 0; i < TestContext::kFrames; i++) {
            if (!std::isfinite(tc.output[i]) || std::fabs(tc.output[i]) > 1.0f) {
                bounded = false;
                break;
            }
        }
        if (!bounded) break;
    }
    check(bounded, "high feedback + silence stays bounded");

    loader.destroy_instance(inst);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::string build_dir = ".";

    std::string staging = build_dir + "/.test_modulation_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    const char* ops[] = {"flanger", "chorus", "phaser"};
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

    std::fprintf(stderr, "\n=== Test: Modulation Operators ===\n");

    test_operator(staging, {"Flanger", "flanger.dylib", 6, 7});
    test_operator(staging, {"Chorus",  "chorus.dylib",  6, 7});
    test_operator(staging, {"Phaser",  "phaser.dylib",  7, 7});
    test_metronome_sync_ignores_free_rate(staging, "flanger.dylib");
    test_metronome_sync_ignores_free_rate(staging, "chorus.dylib");
    test_metronome_sync_ignores_free_rate(staging, "phaser.dylib");
    test_flanger_dc_stability(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
