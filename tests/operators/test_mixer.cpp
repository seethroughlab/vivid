// Mixer smoke + behavioral tests.
//
// Verifies the stereo-aware Mixer (per-input gain + pan, equal-power law):
//   1. Port surface — 16 stereo audio inputs, 1 stereo output, 32 params
//      (gain_0..15, pan_0..15).
//   2. Mono input on port_0 with pan_0 = -1 → all energy on L, R silent.
//   3. Stereo input on port_0 with pan_0 = 0 → both legs equal source × 0.707.
//   4. Two stereo inputs at pan = ±0.5 with distinct sources → side_rms
//      (RMS of L−R) is non-zero, proving the stereo image survives the mix.
//   5. Disconnected input contributes silence (input_buffers[i] == nullptr).
//   6. gain_X = 0 short-circuits (input bypassed entirely).

#include "operator_api/types.h"
#include "runtime/operators/operator_loader.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

constexpr int kFrames = 256;
constexpr uint32_t kSampleRate = 48000;
constexpr int kMaxInputs = 16;

static void* stub_lane_state(void*, uint32_t, uint32_t) { return nullptr; }

static float rms(const float* buf, int n) {
    double sum = 0.0;
    for (int i = 0; i < n; ++i) sum += static_cast<double>(buf[i]) * buf[i];
    return static_cast<float>(std::sqrt(sum / n));
}

// Planar stereo per port: L at [0..kFrames-1], R at [kFrames..2*kFrames-1].
struct Harness {
    float in_data[kMaxInputs][kFrames * 2] = {};
    float out_data[kFrames * 2] = {};
    float* in_bufs[kMaxInputs];
    float* out_bufs[1] = { out_data };
    uint8_t in_ch[kMaxInputs];
    uint8_t out_ch[1] = { 2 };

    VividAudioContext ctx{};

    Harness() {
        for (int i = 0; i < kMaxInputs; ++i) {
            in_bufs[i] = in_data[i];
            in_ch[i] = 2;
        }
        ctx.sample_rate           = kSampleRate;
        ctx.buffer_size           = kFrames;
        ctx.input_buffers         = in_bufs;
        ctx.output_buffers        = out_bufs;
        ctx.input_channel_counts  = in_ch;
        ctx.output_channel_counts = out_ch;
        ctx.lane_state_fn         = stub_lane_state;
        ctx.lane_id               = 1;
    }

    float* L_in(int port) { return in_data[port]; }
    float* R_in(int port) { return in_data[port] + kFrames; }
    float* L_out() { return out_data; }
    float* R_out() { return out_data + kFrames; }

    void disconnect(int port) { in_bufs[port] = nullptr; }
};

static int param_index(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->param_count; ++p)
        if (std::strcmp(desc->params[p].name, name) == 0) return static_cast<int>(p);
    return -1;
}

static bool has_param(const VividOperatorDescriptor* desc, const char* name) {
    return param_index(desc, name) >= 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string dylib_path = build_dir + "/mixer.dylib";

    if (!std::filesystem::exists(dylib_path)) {
        std::fprintf(stderr, "FATAL: %s not found\n", dylib_path.c_str());
        return 1;
    }

    vivid::OperatorLoader loader;
    if (!loader.load(dylib_path.c_str())) {
        std::fprintf(stderr, "FATAL: failed to load %s\n", dylib_path.c_str());
        return 1;
    }

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "Mixer descriptor not null");
    if (!desc) return 1;
    check(std::strcmp(desc->name, "Mixer") == 0, "operator name is Mixer");

    // ── Test 1: Port surface ────────────────────────────────────
    {
        std::fprintf(stderr, "\n--- port surface ---\n");
        int audio_in = 0, audio_out = 0;
        for (uint32_t p = 0; p < desc->port_count; ++p) {
            const auto& port = desc->ports[p];
            if (port.transport != VIVID_PORT_TRANSPORT_AUDIO_BUFFER) continue;
            if (port.direction == VIVID_PORT_INPUT) {
                audio_in++;
                check(port.channels == 2, "audio input port channels=2");
            } else {
                audio_out++;
                check(port.channels == 2, "audio output port channels=2");
            }
        }
        check(audio_in == 16, "16 audio inputs declared");
        check(audio_out == 1, "1 audio output declared");

        for (int i = 0; i < 16; ++i) {
            char gn[16], pn[16], gmsg[64], pmsg[64];
            std::snprintf(gn, sizeof(gn), "gain_%d", i);
            std::snprintf(pn, sizeof(pn), "pan_%d", i);
            std::snprintf(gmsg, sizeof(gmsg), "param %s exists", gn);
            std::snprintf(pmsg, sizeof(pmsg), "param %s exists", pn);
            check(has_param(desc, gn), gmsg);
            check(has_param(desc, pn), pmsg);
        }
    }

    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
    const int idx_gain_0 = param_index(desc, "gain_0");
    const int idx_pan_0  = param_index(desc, "pan_0");
    const int idx_gain_1 = param_index(desc, "gain_1");
    const int idx_pan_1  = param_index(desc, "pan_1");
    if (idx_gain_0 < 0 || idx_pan_0 < 0 || idx_gain_1 < 0 || idx_pan_1 < 0) {
        std::fprintf(stderr, "FATAL: required params missing\n");
        return 1;
    }

    constexpr float kCenter = 0.70710678f;  // cos(π/4) = sin(π/4) ≈ 1/√2

    // ── Test 2: Mono input panned hard left ─────────────────────
    {
        std::fprintf(stderr, "\n--- mono input pan_0=-1 (hard left) ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_gain_0] = 1.0f;
        params[idx_pan_0]  = -1.0f;

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();
        h.in_ch[0] = 1;  // declare port_0 mono — operator reads only kFrames samples

        for (int i = 0; i < kFrames; ++i) {
            h.L_in(0)[i] = std::sin(2.0f * 3.14159265f * 440.0f * i / kSampleRate);
        }

        loader.process_audio(inst, &h.ctx);

        const float lr = rms(h.L_out(), kFrames);
        const float rr = rms(h.R_out(), kFrames);
        check(lr > 0.5f,    "L-out has signal at pan=-1");
        check(rr < 1e-4f,   "R-out silent at pan=-1");

        loader.destroy_instance(inst);
    }

    // ── Test 3: Stereo input pan=0 (center) ─────────────────────
    {
        std::fprintf(stderr, "\n--- stereo input pan_0=0 (center) ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_gain_0] = 1.0f;
        params[idx_pan_0]  = 0.0f;

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();

        // Distinct L (sine) and R (cosine) — both legs should appear at center pan.
        for (int i = 0; i < kFrames; ++i) {
            float t = static_cast<float>(i) / kSampleRate;
            h.L_in(0)[i] = std::sin(2.0f * 3.14159265f * 440.0f * t);
            h.R_in(0)[i] = std::cos(2.0f * 3.14159265f * 660.0f * t);
        }

        loader.process_audio(inst, &h.ctx);

        bool match_l = true;
        bool match_r = true;
        for (int i = 0; i < kFrames; ++i) {
            float t = static_cast<float>(i) / kSampleRate;
            float exp_l = kCenter * std::sin(2.0f * 3.14159265f * 440.0f * t);
            float exp_r = kCenter * std::cos(2.0f * 3.14159265f * 660.0f * t);
            if (std::fabs(h.L_out()[i] - exp_l) > 1e-4f) match_l = false;
            if (std::fabs(h.R_out()[i] - exp_r) > 1e-4f) match_r = false;
        }
        check(match_l, "L-out = source_L × 0.707 at pan=0");
        check(match_r, "R-out = source_R × 0.707 at pan=0");

        loader.destroy_instance(inst);
    }

    // ── Test 4: Two stereo inputs at ±0.5 → stereo image survives ──
    {
        std::fprintf(stderr, "\n--- two stereo inputs at pan ±0.5 → side_rms > 0 ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_gain_0] = 1.0f;
        params[idx_pan_0]  = -0.5f;
        params[idx_gain_1] = 1.0f;
        params[idx_pan_1]  = +0.5f;

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();

        // Two distinct sources, each L=R within itself. After panning to
        // opposite sides the output L vs R must diverge (side_rms > 0).
        for (int i = 0; i < kFrames; ++i) {
            float t = static_cast<float>(i) / kSampleRate;
            float s0 = std::sin(2.0f * 3.14159265f * 440.0f * t);
            float s1 = std::sin(2.0f * 3.14159265f * 660.0f * t);
            h.L_in(0)[i] = s0; h.R_in(0)[i] = s0;
            h.L_in(1)[i] = s1; h.R_in(1)[i] = s1;
        }

        loader.process_audio(inst, &h.ctx);

        std::vector<float> diff(kFrames);
        for (int i = 0; i < kFrames; ++i) diff[i] = h.L_out()[i] - h.R_out()[i];
        const float side = rms(diff.data(), kFrames);
        check(side > 0.05f, "side_rms > 0.05 (stereo image preserved at output)");

        loader.destroy_instance(inst);
    }

    // ── Test 5: Disconnected input contributes silence ──────────
    {
        std::fprintf(stderr, "\n--- disconnected input ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_gain_0] = 1.0f;

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();

        for (int i = 0; i < kMaxInputs; ++i) h.disconnect(i);

        loader.process_audio(inst, &h.ctx);

        check(rms(h.L_out(), kFrames) < 1e-6f, "L-out silent when all inputs disconnected");
        check(rms(h.R_out(), kFrames) < 1e-6f, "R-out silent when all inputs disconnected");

        loader.destroy_instance(inst);
    }

    // ── Test 6: gain_X = 0 short-circuits the input ─────────────
    {
        std::fprintf(stderr, "\n--- gain_0 = 0 short-circuit ---\n");
        for (uint32_t p = 0; p < desc->param_count; ++p) params[p] = desc->params[p].default_value;
        params[idx_gain_0] = 0.0f;
        params[idx_pan_0]  = 0.0f;

        void* inst = loader.create_instance();
        Harness h;
        h.ctx.param_values = params.data();

        for (int i = 0; i < kFrames; ++i) {
            h.L_in(0)[i] = 1.0f;
            h.R_in(0)[i] = 1.0f;
        }

        loader.process_audio(inst, &h.ctx);

        check(rms(h.L_out(), kFrames) < 1e-6f, "L-out silent at gain=0 even with loud input");
        check(rms(h.R_out(), kFrames) < 1e-6f, "R-out silent at gain=0 even with loud input");

        loader.destroy_instance(inst);
    }

    if (failures > 0) {
        std::fprintf(stderr, "\nFAIL: %d mixer test failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "\nOK\n");
    return 0;
}
