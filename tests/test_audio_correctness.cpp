// Audio output correctness tests.
// Verifies that core audio operators produce expected spectral and amplitude
// properties using analyze_audio() — no golden files, property-based only.

#include "runtime/operator_loader.h"
#include "runtime/output_analyzer.h"
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

static void check_float(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (expected %.4f, got %.4f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%.4f)\n", msg, actual);
    }
}

// ---------------------------------------------------------------------------
// Test context — extended for spectral analysis (2048 frames for FFT)
// ---------------------------------------------------------------------------

struct TestContext {
    static constexpr int kFrames = 2048;
    static constexpr uint32_t kSampleRate = 48000;

    float input[kFrames]  = {};
    float output[kFrames] = {};
    float* input_bufs[2]  = {input, nullptr};
    float* output_bufs[1] = {output};
    float float_values[4] = {};

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

    void fill_noise() {
        // Simple deterministic pseudo-noise for reproducibility
        uint32_t state = 12345;
        for (int i = 0; i < kFrames; i++) {
            state = state * 1664525u + 1013904223u;
            input[i] = (static_cast<float>(state) / 2147483648.0f) - 1.0f;
        }
    }

    void fill_silence() { std::memset(input, 0, sizeof(input)); }
    void clear_output() { std::memset(output, 0, sizeof(output)); }

    vivid::AudioMetrics analyze_output() const {
        return vivid::analyze_audio(output, kFrames, kSampleRate, 1);
    }
};

// ---------------------------------------------------------------------------
// Noise: white noise should have high spectral flatness
// ---------------------------------------------------------------------------

static void test_noise(const std::string& staging) {
    std::fprintf(stderr, "\n--- Noise: spectral properties ---\n");

    vivid::OperatorLoader loader;
    if (!loader.load((staging + "/audio_noise.dylib").c_str())) {
        std::fprintf(stderr, "  SKIP: could not load audio_noise.dylib\n");
        return;
    }

    const auto* desc = loader.descriptor();
    if (!desc) return;

    void* inst = loader.create_instance();
    if (!inst) return;

    // Use default params (white noise, amplitude=0.5)
    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; p++)
        params[p] = desc->params[p].default_value;

    TestContext tc;
    tc.ctx.param_values = params.data();

    // Process a few buffers for stabilization
    for (int b = 0; b < 4; b++) {
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
    }

    auto m = tc.analyze_output();
    std::fprintf(stderr, "  rms=%.4f peak=%.4f flatness=%.4f centroid=%.1fHz brightness=%.4f\n",
                 m.rms, m.peak, m.spectral_flatness, m.spectral_centroid_hz, m.spectral_brightness);

    check(m.rms > 0.05f, "noise RMS > 0.05 (non-silent)");
    check(m.spectral_flatness > 0.3f, "white noise spectral flatness > 0.3");
    check(m.spectral_brightness > 0.1f, "white noise has high-frequency content");

    loader.destroy_instance(inst);
}

// ---------------------------------------------------------------------------
// FmSynth: carrier at 440Hz with no modulation → near-pure tone
// ---------------------------------------------------------------------------

static void test_fm_synth(const std::string& staging) {
    std::fprintf(stderr, "\n--- FmSynth: spectral correctness ---\n");

    vivid::OperatorLoader loader;
    if (!loader.load((staging + "/fm_synth.dylib").c_str())) {
        std::fprintf(stderr, "  SKIP: could not load fm_synth.dylib\n");
        return;
    }

    const auto* desc = loader.descriptor();
    if (!desc) return;

    // Find param indices
    int mod_index_idx = -1;
    for (uint32_t p = 0; p < desc->param_count; p++) {
        if (std::strcmp(desc->params[p].name, "mod_index") == 0)
            mod_index_idx = static_cast<int>(p);
    }

    // --- Test A: mod_index=0, pure carrier at 440Hz ---
    {
        std::fprintf(stderr, "\n  [A] mod_index=0 (pure carrier)\n");
        void* inst = loader.create_instance();
        if (!inst) return;

        std::vector<float> params(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            params[p] = desc->params[p].default_value;
        if (mod_index_idx >= 0) params[mod_index_idx] = 0.0f;

        TestContext tc;
        tc.ctx.param_values = params.data();
        // gate_cv is float_values[2] (3rd signal input)
        tc.float_values[2] = 1.0f;  // gate ON

        // Process several buffers for envelope attack to settle
        for (int b = 0; b < 8; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
            tc.ctx.time += static_cast<double>(TestContext::kFrames) / TestContext::kSampleRate;
            tc.ctx.frame++;
        }

        auto m = tc.analyze_output();
        std::fprintf(stderr, "    rms=%.4f centroid=%.1fHz flatness=%.4f brightness=%.4f\n",
                     m.rms, m.spectral_centroid_hz, m.spectral_flatness, m.spectral_brightness);

        check(m.rms > 0.05f, "gated FM synth produces signal");
        check_float(m.spectral_centroid_hz, 440.0f, 60.0f,
                    "centroid near 440Hz with mod_index=0");
        check(m.spectral_flatness < 0.2f, "pure carrier is tonal (low flatness)");

        float brightness_low = m.spectral_brightness;
        loader.destroy_instance(inst);

        // --- Test B: high mod_index → brighter spectrum ---
        std::fprintf(stderr, "\n  [B] mod_index=8 (rich harmonics)\n");
        inst = loader.create_instance();
        if (!inst) return;

        for (uint32_t p = 0; p < desc->param_count; p++)
            params[p] = desc->params[p].default_value;
        if (mod_index_idx >= 0) params[mod_index_idx] = 8.0f;
        tc.ctx.param_values = params.data();
        tc.float_values[2] = 1.0f;
        tc.ctx.time = 0.0;
        tc.ctx.frame = 0;

        for (int b = 0; b < 8; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
            tc.ctx.time += static_cast<double>(TestContext::kFrames) / TestContext::kSampleRate;
            tc.ctx.frame++;
        }

        auto m2 = tc.analyze_output();
        std::fprintf(stderr, "    rms=%.4f centroid=%.1fHz flatness=%.4f brightness=%.4f\n",
                     m2.rms, m2.spectral_centroid_hz, m2.spectral_flatness, m2.spectral_brightness);

        check(m2.spectral_brightness > brightness_low,
              "higher mod_index → higher spectral brightness");

        loader.destroy_instance(inst);
    }
}

// ---------------------------------------------------------------------------
// Filter: low-pass should attenuate high frequencies
// ---------------------------------------------------------------------------

static void test_filter(const std::string& staging) {
    std::fprintf(stderr, "\n--- Filter: low-pass attenuates highs ---\n");

    vivid::OperatorLoader loader;
    if (!loader.load((staging + "/filter.dylib").c_str())) {
        std::fprintf(stderr, "  SKIP: could not load filter.dylib\n");
        return;
    }

    const auto* desc = loader.descriptor();
    if (!desc) return;

    int cutoff_idx = -1;
    for (uint32_t p = 0; p < desc->param_count; p++) {
        if (std::strcmp(desc->params[p].name, "cutoff") == 0)
            cutoff_idx = static_cast<int>(p);
    }
    check(cutoff_idx >= 0, "cutoff param found");
    if (cutoff_idx < 0) return;

    // --- Test A: cutoff=500Hz, feed noise → low brightness ---
    float brightness_low, brightness_high;
    {
        std::fprintf(stderr, "\n  [A] cutoff=500Hz\n");
        void* inst = loader.create_instance();
        if (!inst) return;

        std::vector<float> params(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            params[p] = desc->params[p].default_value;
        params[cutoff_idx] = 500.0f;

        TestContext tc;
        tc.ctx.param_values = params.data();

        for (int b = 0; b < 6; b++) {
            tc.fill_noise();
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }

        auto m = tc.analyze_output();
        std::fprintf(stderr, "    rms=%.4f brightness=%.4f centroid=%.1fHz\n",
                     m.rms, m.spectral_brightness, m.spectral_centroid_hz);

        check(m.spectral_brightness < 0.15f,
              "LP at 500Hz: little energy above 4kHz");
        brightness_low = m.spectral_brightness;
        loader.destroy_instance(inst);
    }

    // --- Test B: cutoff=18000Hz → more brightness ---
    {
        std::fprintf(stderr, "\n  [B] cutoff=18000Hz\n");
        void* inst = loader.create_instance();
        if (!inst) return;

        std::vector<float> params(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            params[p] = desc->params[p].default_value;
        params[cutoff_idx] = 18000.0f;

        TestContext tc;
        tc.ctx.param_values = params.data();

        for (int b = 0; b < 6; b++) {
            tc.fill_noise();
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }

        auto m = tc.analyze_output();
        std::fprintf(stderr, "    rms=%.4f brightness=%.4f centroid=%.1fHz\n",
                     m.rms, m.spectral_brightness, m.spectral_centroid_hz);

        brightness_high = m.spectral_brightness;
        check(brightness_high > brightness_low,
              "higher cutoff → more high-frequency energy");
        loader.destroy_instance(inst);
    }
}

// ---------------------------------------------------------------------------
// Gain: output RMS should scale linearly with gain parameter
// ---------------------------------------------------------------------------

static void test_gain(const std::string& staging) {
    std::fprintf(stderr, "\n--- Gain: amplitude scaling ---\n");

    vivid::OperatorLoader loader;
    if (!loader.load((staging + "/gain.dylib").c_str())) {
        std::fprintf(stderr, "  SKIP: could not load gain.dylib\n");
        return;
    }

    const auto* desc = loader.descriptor();
    if (!desc) return;

    int gain_idx = -1;
    for (uint32_t p = 0; p < desc->param_count; p++) {
        if (std::strcmp(desc->params[p].name, "gain") == 0)
            gain_idx = static_cast<int>(p);
    }
    check(gain_idx >= 0, "gain param found");
    if (gain_idx < 0) return;

    // Gain=1.0: output RMS should match input RMS
    {
        void* inst = loader.create_instance();
        if (!inst) return;

        std::vector<float> params(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            params[p] = desc->params[p].default_value;
        params[gain_idx] = 1.0f;

        TestContext tc;
        tc.ctx.param_values = params.data();
        tc.float_values[0] = 1.0f;  // amplitude_cv default
        tc.fill_sine(440.0f, 0.5f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);

        auto m_in  = vivid::analyze_audio(tc.input, TestContext::kFrames, TestContext::kSampleRate, 1);
        auto m_out = tc.analyze_output();
        std::fprintf(stderr, "  gain=1.0: input_rms=%.4f output_rms=%.4f\n", m_in.rms, m_out.rms);
        check_float(m_out.rms, m_in.rms, 0.05f, "gain=1.0 preserves amplitude");

        loader.destroy_instance(inst);
    }

    // Gain=0.5: output RMS should be half
    {
        void* inst = loader.create_instance();
        if (!inst) return;

        std::vector<float> params(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            params[p] = desc->params[p].default_value;
        params[gain_idx] = 0.5f;

        TestContext tc;
        tc.ctx.param_values = params.data();
        tc.float_values[0] = 1.0f;  // amplitude_cv default
        tc.fill_sine(440.0f, 0.5f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);

        auto m_in  = vivid::analyze_audio(tc.input, TestContext::kFrames, TestContext::kSampleRate, 1);
        auto m_out = tc.analyze_output();
        float expected_rms = m_in.rms * 0.5f;
        std::fprintf(stderr, "  gain=0.5: expected_rms=%.4f output_rms=%.4f\n",
                     expected_rms, m_out.rms);
        check_float(m_out.rms, expected_rms, 0.05f, "gain=0.5 halves amplitude");

        loader.destroy_instance(inst);
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::string build_dir = ".";

    std::string staging = build_dir + "/.test_audio_correctness_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    const char* ops[] = {"audio_noise", "fm_synth", "filter", "gain"};
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

    std::fprintf(stderr, "\n=== Test: Audio Output Correctness ===\n");

    test_noise(staging);
    test_fm_synth(staging);
    test_filter(staging);
    test_gain(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
