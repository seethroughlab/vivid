// Tests for synthesis & transformation audio operators:
// RingMod, FmSynth, ParametricEQ.

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

    float input[kFrames]   = {};
    float input2[kFrames]  = {};
    float cv_buf_2[kFrames]= {};   // extra CV buffer (e.g. gate_cv for FmSynth)
    float output[kFrames]  = {};
    // Synth voices_out breakout buffer: kMaxVoices=8 channels for FmSynth
    // (the only synth tested here that uses output_bufs[1]). Other tests
    // ignore this buffer.
    float voices_out[8 * kFrames] = {};
    float* input_bufs[3]   = {input, input2, cv_buf_2};
    float* output_bufs[2]  = {output, voices_out};

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

    void fill_sine2(float freq, float amp) {
        for (int i = 0; i < kFrames; i++)
            input2[i] = amp * std::sin(2.0f * 3.14159265f * freq * i / kSampleRate);
    }

    void fill_silence() {
        std::memset(input, 0, sizeof(input));
        std::memset(input2, 0, sizeof(input2));
    }

    void clear_output() {
        std::memset(output, 0, sizeof(output));
    }

    void clear_floats() {
        std::memset(cv_buf_2, 0, sizeof(cv_buf_2));
    }

    void set_cv(int idx, float val) {
        float* buf = (idx == 0) ? input : (idx == 1) ? input2 : cv_buf_2;
        std::fill(buf, buf + kFrames, val);
    }
};

// ---------------------------------------------------------------------------
// Ring Modulator tests
// ---------------------------------------------------------------------------

static void test_ring_mod(const std::string& staging) {
    std::fprintf(stderr, "\n--- RingMod ---\n");

    vivid::OperatorLoader loader;
    std::string path = staging + "/ring_mod.dylib";
    check(loader.load(path.c_str()), "load dylib");
    if (!loader.is_loaded()) return;

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor not null");
    if (!desc) return;

    check(std::strcmp(desc->name, "RingMod") == 0, "descriptor name");
    check(static_cast<int>(desc->param_count) == 3, "param_count = 3");
    check(static_cast<int>(desc->port_count) == 6, "port_count = 6");

    // Silence -> silence
    {
        void* inst = loader.create_instance();
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

    // Signal production
    {
        void* inst = loader.create_instance();
        TestContext tc;
        for (int b = 0; b < 8; b++) {
            tc.fill_sine(440.0f, 0.5f);
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        float r = rms(tc.output, TestContext::kFrames);
        check(r > 0.01f, "signal RMS > 0");
        check(is_finite(tc.output, TestContext::kFrames), "signal output finite");
        loader.destroy_instance(inst);
    }

    // Extreme params
    {
        void* inst = loader.create_instance();
        TestContext tc;
        tc.fill_sine(440.0f, 0.8f);

        std::vector<float> param_vals(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].max_value;
        tc.ctx.param_values = param_vals.data();

        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, TestContext::kFrames), "max params -> finite");

        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].min_value;
        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, TestContext::kFrames), "min params -> finite");
        loader.destroy_instance(inst);
    }
}

// ---------------------------------------------------------------------------
// FM Synth tests
// ---------------------------------------------------------------------------

static void test_fm_synth(const std::string& staging) {
    std::fprintf(stderr, "\n--- FmSynth ---\n");

    vivid::OperatorLoader loader;
    std::string path = staging + "/fm_synth.dylib";
    check(loader.load(path.c_str()), "load dylib");
    if (!loader.is_loaded()) return;

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor not null");
    if (!desc) return;

    check(std::strcmp(desc->name, "FmSynth") == 0, "descriptor name");
    check(static_cast<int>(desc->param_count) == 10, "param_count = 10");
    // Phase 3 PR3 trimmed gates/notes/velocities lane ports. Now: 4 fixed
    // ports (output, freq_cv, mod_index_cv, gate_cv) + notes_in custom-ref +
    // 5 advanced breakouts (voices_out, voice_ids, voice_gates,
    // voice_velocities, voice_freqs) + 3 analysis (rms, peak, waveform) = 13.
    check(static_cast<int>(desc->port_count) == 13, "port_count = 13");

    // No gate -> silence
    {
        void* inst = loader.create_instance();
        TestContext tc;
        tc.fill_silence();
        tc.clear_floats();
        // gate_cv = 0 (index 2)
        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        float r = rms(tc.output, TestContext::kFrames);
        check(r < 0.001f, "no gate -> near-silence");
        check(is_finite(tc.output, TestContext::kFrames), "no gate output finite");
        loader.destroy_instance(inst);
    }

    // Gate on -> produces output
    {
        void* inst = loader.create_instance();
        TestContext tc;
        tc.fill_silence();
        tc.clear_floats();
        tc.set_cv(2, 1.0f); // gate_cv on

        for (int b = 0; b < 8; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        float r = rms(tc.output, TestContext::kFrames);
        check(r > 0.01f, "gate on -> signal RMS > 0");
        check(is_finite(tc.output, TestContext::kFrames), "gate on output finite");
        loader.destroy_instance(inst);
    }

    // Extreme params
    {
        void* inst = loader.create_instance();
        TestContext tc;
        tc.fill_silence();
        tc.set_cv(2, 1.0f); // gate on

        std::vector<float> param_vals(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].max_value;
        tc.ctx.param_values = param_vals.data();

        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, TestContext::kFrames), "max params -> finite");

        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].min_value;
        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, TestContext::kFrames), "min params -> finite");
        loader.destroy_instance(inst);
    }
}

// ---------------------------------------------------------------------------
// Parametric EQ tests
// ---------------------------------------------------------------------------

static void test_parametric_eq(const std::string& staging) {
    std::fprintf(stderr, "\n--- ParametricEQ ---\n");

    vivid::OperatorLoader loader;
    std::string path = staging + "/parametric_eq.dylib";
    check(loader.load(path.c_str()), "load dylib");
    if (!loader.is_loaded()) return;

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor not null");
    if (!desc) return;

    check(std::strcmp(desc->name, "ParametricEQ") == 0, "descriptor name");
    check(static_cast<int>(desc->param_count) == 17, "param_count = 17");
    check(static_cast<int>(desc->port_count) == 6, "port_count = 6");

    // Silence -> silence (default gains = 0 dB = passthrough)
    {
        void* inst = loader.create_instance();
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

    // Signal passthrough with default (flat) settings
    {
        void* inst = loader.create_instance();
        TestContext tc;
        for (int b = 0; b < 8; b++) {
            tc.fill_sine(440.0f, 0.5f);
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        float r = rms(tc.output, TestContext::kFrames);
        check(r > 0.01f, "signal RMS > 0");
        check(is_finite(tc.output, TestContext::kFrames), "signal output finite");
        loader.destroy_instance(inst);
    }

    // Extreme params
    {
        void* inst = loader.create_instance();
        TestContext tc;
        tc.fill_sine(440.0f, 0.8f);

        std::vector<float> param_vals(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].max_value;
        tc.ctx.param_values = param_vals.data();

        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, TestContext::kFrames), "max params -> finite");

        for (uint32_t p = 0; p < desc->param_count; p++)
            param_vals[p] = desc->params[p].min_value;
        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, TestContext::kFrames), "min params -> finite");
        loader.destroy_instance(inst);
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::string build_dir = ".";

    std::string staging = build_dir + "/.test_synth_transform_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    const char* ops[] = {"ring_mod", "fm_synth", "parametric_eq"};
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

    std::fprintf(stderr, "\n=== Test: Synthesis & Transformation Operators ===\n");

    test_ring_mod(staging);
    test_fm_synth(staging);
    test_parametric_eq(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
