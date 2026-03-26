// Tests for spatial audio operators: StereoPanWidth, PingPongDelay.
// Exercises stereo port handling, pan law, width/M-S, delay timing,
// cross-feed, DC stability, and extreme params.

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
// Stereo test context — planar layout: L[0..kFrames-1], R[kFrames..2*kFrames-1]
// ---------------------------------------------------------------------------

struct StereoTestContext {
    static constexpr int kFrames = 512;
    static constexpr uint32_t kSampleRate = 44100;

    float input[kFrames * 2]  = {};   // planar: L then R
    float output[kFrames * 2] = {};
    float* input_bufs[2]  = {input, nullptr};
    float* output_bufs[1] = {output};
    float float_values[2] = {};       // CV inputs

    VividAudioContext ctx{};

    StereoTestContext() {
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.input_buffers      = input_bufs;
        ctx.output_buffers     = output_bufs;
        ctx.input_float_values = float_values;
        ctx.param_values       = nullptr;
    }

    float* L_in()  { return input; }
    float* R_in()  { return input + kFrames; }
    float* L_out() { return output; }
    float* R_out() { return output + kFrames; }

    void fill_stereo_sine(float freq, float amp_l, float amp_r) {
        for (int i = 0; i < kFrames; i++) {
            float s = std::sin(2.0f * 3.14159265f * freq * i / kSampleRate);
            L_in()[i] = amp_l * s;
            R_in()[i] = amp_r * s;
        }
    }

    void fill_silence() {
        std::memset(input, 0, sizeof(input));
    }

    void clear_output() {
        std::memset(output, 0, sizeof(output));
    }

    void clear_cv() {
        float_values[0] = 0.0f;
        float_values[1] = 0.0f;
    }
};

// ---------------------------------------------------------------------------
// StereoPanWidth tests
// ---------------------------------------------------------------------------

static void test_stereo_pan_width(const std::string& staging) {
    std::fprintf(stderr, "\n--- StereoPanWidth ---\n");

    vivid::OperatorLoader loader;
    std::string path = staging + "/stereo_pan_width.dylib";
    check(loader.load(path.c_str()), "load dylib");
    check(loader.is_loaded(), "is_loaded");

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor not null");
    if (!desc) return;

    check(std::strcmp(desc->name, "StereoPanWidth") == 0, "name matches");
    check(desc->execution_env == VIVID_ENV_AUDIO, "env = AUDIO");
    check(static_cast<int>(desc->param_count) == 3, "param_count = 3");
    check(static_cast<int>(desc->port_count) == 4, "port_count = 4");

    // Verify stereo channels on audio ports
    for (uint32_t p = 0; p < desc->port_count; p++) {
        if (desc->ports[p].type == VIVID_PORT_AUDIO) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "port '%s' channels = 2", desc->ports[p].name);
            check(desc->ports[p].channels == 2, msg);
        }
    }

    // Verify float params have KNOB hint
    for (uint32_t p = 0; p < desc->param_count; p++) {
        if (desc->params[p].type == VIVID_PARAM_FLOAT) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "param '%s' has KNOB hint", desc->params[p].name);
            check(desc->params[p].display_hint == VIVID_DISPLAY_KNOB, msg);
        }
    }

    // Helper to find param index
    auto find_param = [&](const char* name) -> int {
        for (uint32_t p = 0; p < desc->param_count; p++)
            if (std::strcmp(desc->params[p].name, name) == 0) return static_cast<int>(p);
        return -1;
    };

    int pan_idx = find_param("pan");
    int width_idx = find_param("width");
    int ms_idx = find_param("ms_balance");
    check(pan_idx >= 0, "pan param found");
    check(width_idx >= 0, "width param found");
    check(ms_idx >= 0, "ms_balance param found");

    auto make_defaults = [&]() {
        std::vector<float> v(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            v[p] = desc->params[p].default_value;
        return v;
    };

    // 1. Silence → silence
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        tc.fill_silence();
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
        check(rms(tc.L_out(), StereoTestContext::kFrames) < 0.001f, "silence -> L silent");
        check(rms(tc.R_out(), StereoTestContext::kFrames) < 0.001f, "silence -> R silent");
        loader.destroy_instance(inst);
    }

    // 2. Center/width=1/ms=0.5 → near-unity passthrough
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[pan_idx] = 0.0f;
        params[width_idx] = 1.0f;
        params[ms_idx] = 0.5f;
        tc.ctx.param_values = params.data();

        tc.fill_stereo_sine(440.0f, 0.5f, 0.5f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);

        // At center pan (angle = PI/4), cos = sin ≈ 0.707
        // With equal L/R input, mid = input, side = 0, so output ≈ 0.707 * input
        float in_rms = rms(tc.L_in(), StereoTestContext::kFrames);
        float out_rms_l = rms(tc.L_out(), StereoTestContext::kFrames);
        float out_rms_r = rms(tc.R_out(), StereoTestContext::kFrames);
        check(out_rms_l > in_rms * 0.5f, "center passthrough L has signal");
        check(out_rms_r > in_rms * 0.5f, "center passthrough R has signal");
        check(std::fabs(out_rms_l - out_rms_r) < 0.01f, "center passthrough L ≈ R");
        loader.destroy_instance(inst);
    }

    // 3. Hard left pan → R_out ≈ 0
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[pan_idx] = -1.0f;
        params[width_idx] = 1.0f;
        params[ms_idx] = 0.5f;
        tc.ctx.param_values = params.data();

        tc.fill_stereo_sine(440.0f, 0.5f, 0.5f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);

        check(rms(tc.L_out(), StereoTestContext::kFrames) > 0.01f, "hard left: L has signal");
        check(rms(tc.R_out(), StereoTestContext::kFrames) < 0.001f, "hard left: R ≈ 0");
        loader.destroy_instance(inst);
    }

    // 4. Hard right pan → L_out ≈ 0
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[pan_idx] = 1.0f;
        params[width_idx] = 1.0f;
        params[ms_idx] = 0.5f;
        tc.ctx.param_values = params.data();

        tc.fill_stereo_sine(440.0f, 0.5f, 0.5f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);

        check(rms(tc.L_out(), StereoTestContext::kFrames) < 0.001f, "hard right: L ≈ 0");
        check(rms(tc.R_out(), StereoTestContext::kFrames) > 0.01f, "hard right: R has signal");
        loader.destroy_instance(inst);
    }

    // 5. Width=0 → L_out ≈ R_out (mono collapse)
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[pan_idx] = 0.0f;
        params[width_idx] = 0.0f;
        params[ms_idx] = 0.5f;
        tc.ctx.param_values = params.data();

        // Asymmetric stereo input
        tc.fill_stereo_sine(440.0f, 0.8f, 0.2f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);

        // With width=0 and center pan, L and R should be the same (mono)
        float max_diff = 0.0f;
        for (int i = 0; i < StereoTestContext::kFrames; i++) {
            float d = std::fabs(tc.L_out()[i] - tc.R_out()[i]);
            if (d > max_diff) max_diff = d;
        }
        check(max_diff < 0.01f, "width=0: L ≈ R (mono collapse)");
        loader.destroy_instance(inst);
    }

    // 6. Width=2 → side content amplified
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[pan_idx] = 0.0f;
        params[width_idx] = 1.0f;
        params[ms_idx] = 0.5f;
        tc.ctx.param_values = params.data();

        // Asymmetric input with side content
        tc.fill_stereo_sine(440.0f, 0.8f, 0.2f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
        float diff_w1 = 0.0f;
        for (int i = 0; i < StereoTestContext::kFrames; i++)
            diff_w1 += std::fabs(tc.L_out()[i] - tc.R_out()[i]);

        params[width_idx] = 2.0f;
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
        float diff_w2 = 0.0f;
        for (int i = 0; i < StereoTestContext::kFrames; i++)
            diff_w2 += std::fabs(tc.L_out()[i] - tc.R_out()[i]);

        check(diff_w2 > diff_w1, "width=2: more L/R difference than width=1");
        loader.destroy_instance(inst);
    }

    // 7. ms_balance=1 + mono input → silence (no side content to pass)
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[pan_idx] = 0.0f;
        params[width_idx] = 1.0f;
        params[ms_idx] = 1.0f;  // side only
        tc.ctx.param_values = params.data();

        // Mono input (L = R)
        tc.fill_stereo_sine(440.0f, 0.5f, 0.5f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);

        // ms_balance=1 means mid_gain=0, side_gain=1. Mono input has no side, so output ≈ 0
        check(rms(tc.L_out(), StereoTestContext::kFrames) < 0.001f, "ms=1 + mono: L ≈ 0");
        check(rms(tc.R_out(), StereoTestContext::kFrames) < 0.001f, "ms=1 + mono: R ≈ 0");
        loader.destroy_instance(inst);
    }

    // 8. Extreme params → finite output
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        tc.fill_stereo_sine(440.0f, 0.8f, 0.8f);

        std::vector<float> params(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            params[p] = desc->params[p].min_value;
        tc.ctx.param_values = params.data();
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
        check(is_finite(tc.output, StereoTestContext::kFrames * 2), "min params -> finite");

        for (uint32_t p = 0; p < desc->param_count; p++)
            params[p] = desc->params[p].max_value;
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
        check(is_finite(tc.output, StereoTestContext::kFrames * 2), "max params -> finite");
        loader.destroy_instance(inst);
    }

    // 9. CV modulation offsets pan
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[pan_idx] = 0.0f;
        params[width_idx] = 1.0f;
        params[ms_idx] = 0.5f;
        tc.ctx.param_values = params.data();

        tc.fill_stereo_sine(440.0f, 0.5f, 0.5f);

        // Without CV: center pan
        tc.clear_cv();
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
        float center_l = rms(tc.L_out(), StereoTestContext::kFrames);
        float center_r = rms(tc.R_out(), StereoTestContext::kFrames);

        // With pan_cv = +0.5: should shift right
        loader.destroy_instance(inst);
        inst = loader.create_instance();
        tc.float_values[0] = 0.5f;
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
        float shifted_l = rms(tc.L_out(), StereoTestContext::kFrames);
        float shifted_r = rms(tc.R_out(), StereoTestContext::kFrames);

        check(shifted_r > center_r * 1.05f || shifted_l < center_l * 0.95f,
              "pan CV shifts balance");
        loader.destroy_instance(inst);
    }
}

// ---------------------------------------------------------------------------
// PingPongDelay tests
// ---------------------------------------------------------------------------

static void test_ping_pong_delay(const std::string& staging) {
    std::fprintf(stderr, "\n--- PingPongDelay ---\n");

    vivid::OperatorLoader loader;
    std::string path = staging + "/ping_pong_delay.dylib";
    check(loader.load(path.c_str()), "load dylib");
    check(loader.is_loaded(), "is_loaded");

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor not null");
    if (!desc) return;

    check(std::strcmp(desc->name, "PingPongDelay") == 0, "name matches");
    check(desc->execution_env == VIVID_ENV_AUDIO, "env = AUDIO");
    check(static_cast<int>(desc->param_count) == 6, "param_count = 6");
    check(static_cast<int>(desc->port_count) == 4, "port_count = 4");

    // Verify stereo channels on audio ports
    for (uint32_t p = 0; p < desc->port_count; p++) {
        if (desc->ports[p].type == VIVID_PORT_AUDIO) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "port '%s' channels = 2", desc->ports[p].name);
            check(desc->ports[p].channels == 2, msg);
        }
    }

    // Verify float params have KNOB hint
    for (uint32_t p = 0; p < desc->param_count; p++) {
        if (desc->params[p].type == VIVID_PARAM_FLOAT) {
            char msg[128];
            std::snprintf(msg, sizeof(msg), "param '%s' has KNOB hint", desc->params[p].name);
            check(desc->params[p].display_hint == VIVID_DISPLAY_KNOB, msg);
        }
    }

    auto find_param = [&](const char* name) -> int {
        for (uint32_t p = 0; p < desc->param_count; p++)
            if (std::strcmp(desc->params[p].name, name) == 0) return static_cast<int>(p);
        return -1;
    };

    int time_idx   = find_param("time");
    int fb_idx     = find_param("feedback");
    int spread_idx = find_param("spread");
    int filt_idx   = find_param("filter");
    int freq_idx   = find_param("filter_freq");
    int mix_idx    = find_param("mix");

    auto make_defaults = [&]() {
        std::vector<float> v(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            v[p] = desc->params[p].default_value;
        return v;
    };

    // 1. Silence → silence
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        tc.fill_silence();
        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(rms(tc.L_out(), StereoTestContext::kFrames) < 0.001f, "silence -> L silent");
        check(rms(tc.R_out(), StereoTestContext::kFrames) < 0.001f, "silence -> R silent");
        loader.destroy_instance(inst);
    }

    // 2. Mix=0 → dry passthrough
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[mix_idx] = 0.0f;
        tc.ctx.param_values = params.data();

        // Process a few buffers for init
        tc.fill_stereo_sine(440.0f, 0.5f, 0.5f);
        for (int b = 0; b < 3; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }

        tc.fill_stereo_sine(440.0f, 0.5f, 0.5f);
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);

        float max_diff = 0.0f;
        for (int i = 0; i < StereoTestContext::kFrames; i++) {
            float dl = std::fabs(tc.L_out()[i] - tc.L_in()[i]);
            float dr = std::fabs(tc.R_out()[i] - tc.R_in()[i]);
            if (dl > max_diff) max_diff = dl;
            if (dr > max_diff) max_diff = dr;
        }
        check(max_diff < 0.01f, "mix=0: dry passthrough");
        loader.destroy_instance(inst);
    }

    // 3. Wet signal appears after delay time (multi-buffer impulse test)
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        // 50ms delay at 44100 = 2205 samples ≈ 4.3 buffers of 512
        params[time_idx] = 50.0f;
        params[mix_idx] = 1.0f;   // wet only
        params[fb_idx] = 0.0f;    // no feedback
        params[spread_idx] = 0.0f;
        params[filt_idx] = 0.0f;  // filter off
        tc.ctx.param_values = params.data();

        // First buffer: impulse in sample 0
        tc.fill_silence();
        tc.L_in()[0] = 1.0f;
        tc.R_in()[0] = 1.0f;
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);
        // Output should be near-silent (delay hasn't elapsed)
        float first_rms = rms(tc.L_out(), StereoTestContext::kFrames);

        // Process more buffers until delay has elapsed
        float found_impulse_rms = 0.0f;
        for (int b = 0; b < 10; b++) {
            tc.fill_silence();
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
            float r = rms(tc.L_out(), StereoTestContext::kFrames);
            if (r > found_impulse_rms) found_impulse_rms = r;
        }
        check(found_impulse_rms > 0.001f, "wet impulse appears after delay");
        loader.destroy_instance(inst);
    }

    // 4. Spread=1 → echoes alternate L/R channels
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[time_idx] = 50.0f;
        params[mix_idx] = 1.0f;
        params[fb_idx] = 0.5f;
        params[spread_idx] = 1.0f;  // full cross-feed
        params[filt_idx] = 0.0f;
        tc.ctx.param_values = params.data();

        // Impulse only in L
        tc.fill_silence();
        tc.L_in()[0] = 1.0f;
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);

        // Process buffers and accumulate energy per channel
        float energy_L = 0.0f, energy_R = 0.0f;
        for (int b = 0; b < 20; b++) {
            tc.fill_silence();
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
            for (int i = 0; i < StereoTestContext::kFrames; i++) {
                energy_L += tc.L_out()[i] * tc.L_out()[i];
                energy_R += tc.R_out()[i] * tc.R_out()[i];
            }
        }
        // With full cross-feed, L input should produce energy in both channels
        check(energy_L > 0.0001f, "spread=1: L echo energy present");
        check(energy_R > 0.0001f, "spread=1: R echo energy present (cross-feed)");
        loader.destroy_instance(inst);
    }

    // 5. Spread=0 → echoes equal in both channels (from respective inputs)
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[time_idx] = 50.0f;
        params[mix_idx] = 1.0f;
        params[fb_idx] = 0.3f;
        params[spread_idx] = 0.0f;  // no cross-feed
        params[filt_idx] = 0.0f;
        tc.ctx.param_values = params.data();

        // Impulse only in L
        tc.fill_silence();
        tc.L_in()[0] = 1.0f;
        tc.clear_output();
        loader.process_audio(inst, &tc.ctx);

        float energy_R = 0.0f;
        for (int b = 0; b < 20; b++) {
            tc.fill_silence();
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
            for (int i = 0; i < StereoTestContext::kFrames; i++)
                energy_R += tc.R_out()[i] * tc.R_out()[i];
        }
        // With spread=0, L impulse should NOT produce R echoes
        check(energy_R < 0.0001f, "spread=0: no cross-feed to R");
        loader.destroy_instance(inst);
    }

    // 6. DC stability under high feedback (100 buffers)
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        auto params = make_defaults();
        params[fb_idx] = 0.9f;
        tc.ctx.param_values = params.data();

        bool bounded = true;
        for (int b = 0; b < 100; b++) {
            tc.fill_silence();
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
            for (int i = 0; i < StereoTestContext::kFrames * 2; i++) {
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

    // 7. Each filter mode → finite output
    {
        for (int mode = 0; mode < 3; mode++) {
            void* inst = loader.create_instance();
            StereoTestContext tc;
            auto params = make_defaults();
            params[filt_idx] = static_cast<float>(mode);
            tc.ctx.param_values = params.data();

            tc.fill_stereo_sine(440.0f, 0.5f, 0.5f);
            for (int b = 0; b < 8; b++) {
                tc.clear_output();
                loader.process_audio(inst, &tc.ctx);
            }

            char msg[64];
            std::snprintf(msg, sizeof(msg), "filter mode %d -> finite output", mode);
            check(is_finite(tc.output, StereoTestContext::kFrames * 2), msg);
            loader.destroy_instance(inst);
        }
    }

    // 8. Extreme params → finite output
    {
        void* inst = loader.create_instance();
        StereoTestContext tc;
        tc.fill_stereo_sine(440.0f, 0.8f, 0.8f);

        std::vector<float> params(desc->param_count);
        for (uint32_t p = 0; p < desc->param_count; p++)
            params[p] = desc->params[p].min_value;
        tc.ctx.param_values = params.data();

        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, StereoTestContext::kFrames * 2), "min params -> finite");

        for (uint32_t p = 0; p < desc->param_count; p++)
            params[p] = desc->params[p].max_value;
        for (int b = 0; b < 4; b++) {
            tc.clear_output();
            loader.process_audio(inst, &tc.ctx);
        }
        check(is_finite(tc.output, StereoTestContext::kFrames * 2), "max params -> finite");
        loader.destroy_instance(inst);
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::string build_dir = ".";

    std::string staging = build_dir + "/.test_spatial_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);

    const char* ops[] = {"stereo_pan_width", "ping_pong_delay"};
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

    std::fprintf(stderr, "\n=== Test: Spatial Operators ===\n");

    test_stereo_pan_width(staging);
    test_ping_pong_delay(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
