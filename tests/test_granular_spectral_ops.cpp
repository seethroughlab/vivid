// Tests for granular & spectral audio operators: GranularSynth, SpectralFreeze.
// Exercises load, silence, dry pass-through, wet signal, extreme params,
// and operator-specific behavior.

#include "runtime/operator_loader.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

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

    float input[kFrames]  = {};
    float output[kFrames] = {};
    float* input_bufs[2]  = {input, nullptr};
    float* output_bufs[1] = {output};
    float float_values[3] = {0.0f, 0.0f, 0.0f};

    VividAudioContext ctx{};

    TestContext() {
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.input_buffers      = input_bufs;
        ctx.output_buffers     = output_bufs;
        ctx.input_float_values = float_values;
        ctx.param_values       = nullptr;
    }

    void fill_sine(float freq, float amp) {
        for (int i = 0; i < kFrames; i++)
            input[i] = amp * std::sin(2.0f * 3.14159265f * freq * i / kSampleRate);
    }

    void fill_silence() {
        std::memset(input, 0, sizeof(input));
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
    const char* mix_param_name;  // "mix" for granular, nullptr for spectral
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
    check(desc->domain == VIVID_DOMAIN_AUDIO, "domain = AUDIO");
    check(desc->has_process_audio == 1, "has_process_audio");
    check(static_cast<int>(desc->param_count) == info.expected_param_count,
          "param_count matches");
    check(static_cast<int>(desc->port_count) == info.expected_port_count,
          "port_count matches");

    // Verify port names: input, output
    bool has_input = false, has_output = false;
    for (uint32_t p = 0; p < desc->port_count; p++) {
        if (std::strcmp(desc->ports[p].name, "input") == 0)  has_input = true;
        if (std::strcmp(desc->ports[p].name, "output") == 0) has_output = true;
    }
    check(has_input, "has input port");
    check(has_output, "has output port");

    // Verify all float params have VIVID_DISPLAY_KNOB hint
    for (uint32_t p = 0; p < desc->param_count; p++) {
        if (desc->params[p].type == VIVID_PARAM_FLOAT ||
            desc->params[p].type == VIVID_PARAM_INT) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "param '%s' has KNOB display hint",
                         desc->params[p].name);
            check(desc->params[p].display_hint == VIVID_DISPLAY_KNOB, msg);
        }
    }

    // 2. Silence in -> silence out
    {
        void* inst = loader.create_instance();
        check(inst != nullptr, "create_instance");
        if (!inst) return;

        TestContext tc;
        tc.fill_silence();
        for (int b = 0; b < 8; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        float r = rms(tc.output, TestContext::kFrames);
        check(r < 0.001f, "silence in -> near-silence out");
        check(is_finite(tc.output, TestContext::kFrames), "silence output finite");
        loader.destroy_instance(inst);
    }

    // 3. Dry pass-through (mix=0 or blend=0 when not frozen)
    if (info.mix_param_name) {
        void* inst = loader.create_instance();
        TestContext tc;

        int mix_idx = -1;
        for (uint32_t p = 0; p < desc->param_count; p++) {
            if (std::strcmp(desc->params[p].name, info.mix_param_name) == 0) {
                mix_idx = static_cast<int>(p);
                break;
            }
        }
        check(mix_idx >= 0, "mix/blend param found");

        if (mix_idx >= 0) {
            std::vector<float> param_vals(desc->param_count);
            for (uint32_t p = 0; p < desc->param_count; p++)
                param_vals[p] = desc->params[p].default_value;
            param_vals[mix_idx] = 0.0f;
            tc.ctx.param_values = param_vals.data();

            tc.fill_sine(440.0f, 0.5f);
            for (int b = 0; b < 4; b++) {
                tc.clear_output();
                loader.process_audio(inst, &tc.ctx);
            }

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

    // 4. Wet produces signal (default params with input)
    {
        void* inst = loader.create_instance();
        TestContext tc;

        std::vector<float> param_vals(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].default_value;
        tc.ctx.param_values = param_vals.data();

        // Run enough buffers to fill capture buffers (granular needs ~4s of audio)
        for (int b = 0; b < 400; b++) {
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

        for (int b = 0; b < 8; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, TestContext::kFrames), "min params -> finite output");

        // All at maximum
        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].max_value;

        for (int b = 0; b < 8; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, TestContext::kFrames), "max params -> finite output");
        loader.destroy_instance(inst);
    }
}

// ---------------------------------------------------------------------------
// Spectral Freeze: unity gain passthrough when not frozen (blend=0)
// ---------------------------------------------------------------------------

static void test_spectral_passthrough(const std::string& staging) {
    std::fprintf(stderr, "\n--- SpectralFreeze passthrough ---\n");

    vivid::OperatorLoader loader;
    std::string path = staging + "/spectral_freeze.dylib";
    if (!loader.load(path.c_str())) {
        std::fprintf(stderr, "  SKIP: could not load spectral_freeze\n");
        return;
    }

    const auto* desc = loader.descriptor();
    if (!desc) return;

    void* inst = loader.create_instance();
    if (!inst) return;

    TestContext tc;

    // Set freeze=0, blend=0 (passthrough)
    std::vector<float> param_vals(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; p++)
        param_vals[p] = desc->params[p].default_value;
    // freeze defaults to 0, blend defaults to 0 — should pass through
    tc.ctx.param_values = param_vals.data();

    // Run several buffers to let the overlap-add settle
    for (int b = 0; b < 20; b++) {
        tc.fill_sine(440.0f, 0.5f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
    }

    // Check output has signal (overlap-add of unfrozen input)
    float r = rms(tc.output, TestContext::kFrames);
    check(r > 0.01f, "unfrozen passthrough has signal");
    check(is_finite(tc.output, TestContext::kFrames), "passthrough output finite");

    loader.destroy_instance(inst);
}

// ---------------------------------------------------------------------------
// Granular: frozen signal with high density should produce audible output
// ---------------------------------------------------------------------------

static void test_granular_wet_signal(const std::string& staging) {
    std::fprintf(stderr, "\n--- GranularSynth wet production ---\n");

    vivid::OperatorLoader loader;
    std::string path = staging + "/granular_synth.dylib";
    if (!loader.load(path.c_str())) {
        std::fprintf(stderr, "  SKIP: could not load granular_synth\n");
        return;
    }

    const auto* desc = loader.descriptor();
    if (!desc) return;

    void* inst = loader.create_instance();
    if (!inst) return;

    TestContext tc;

    std::vector<float> param_vals(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; p++)
        param_vals[p] = desc->params[p].default_value;

    // Set mix=1, density=30, position near write head for reliable output
    for (uint32_t p = 0; p < desc->param_count; p++) {
        if (std::strcmp(desc->params[p].name, "mix") == 0)
            param_vals[p] = 1.0f;
        if (std::strcmp(desc->params[p].name, "density") == 0)
            param_vals[p] = 30.0f;
        if (std::strcmp(desc->params[p].name, "position") == 0)
            param_vals[p] = 0.01f;  // near write head so grains read recent audio
    }
    tc.ctx.param_values = param_vals.data();

    // Feed signal for enough buffers to populate capture buffer
    for (int b = 0; b < 64; b++) {
        tc.fill_sine(440.0f, 0.5f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
    }

    float r = rms(tc.output, TestContext::kFrames);
    check(r > 0.01f, "granular wet signal present with input");
    check(is_finite(tc.output, TestContext::kFrames), "granular wet output finite");

    loader.destroy_instance(inst);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::string build_dir = ".";

    std::string staging = build_dir + "/.test_granular_spectral_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    const char* ops[] = {"granular_synth", "spectral_freeze"};
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

    std::fprintf(stderr, "\n=== Test: Granular & Spectral Operators ===\n");

    // GranularSynth: 7 params, 5 ports (input, output, position_cv, pitch_cv, density_cv)
    test_operator(staging, {"GranularSynth", "granular_synth.dylib", 7, 5, "mix"});

    // SpectralFreeze: 5 params, 4 ports (input, output, freeze_cv, blend_cv)
    test_operator(staging, {"SpectralFreeze", "spectral_freeze.dylib", 5, 4, nullptr});

    test_granular_wet_signal(staging);
    test_spectral_passthrough(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
