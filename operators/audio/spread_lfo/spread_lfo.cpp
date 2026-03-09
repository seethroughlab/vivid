#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/audio_dsp.h"
#include <cmath>
#include <cstring>
#include <algorithm>

struct SpreadLFO : vivid::OperatorBase {
    static constexpr const char* kName   = "SpreadLFO";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> frequency {"frequency",  1.0f,  0.01f, 100.0f};
    vivid::Param<float> amplitude {"amplitude",  1.0f,  0.0f,  10.0f};
    vivid::Param<float> offset    {"offset",     0.0f,  0.0f,  10.0f};
    vivid::Param<int>   waveform  {"waveform",   0, {"sine", "saw", "square", "triangle"}};
    vivid::Param<int>   mode      {"mode",       0, {"free", "per_voice"}};

    static constexpr int kMaxSlots = 16;

    // Per-voice state (only used in per_voice mode)
    double phases_[kMaxSlots] = {};
    float prev_gates_[kMaxSlots] = {};
    uint32_t prev_len_ = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&frequency);
        out.push_back(&amplitude);
        out.push_back(&offset);
        out.push_back(&waveform);
        out.push_back(&mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gates",  VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"values", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
    }

    void process(VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float freq = frequency.value;
        float amp = amplitude.value;
        float off = offset.value;
        int wave = waveform.int_value();
        int lfo_mode = mode.int_value();
        double sample_rate = static_cast<double>(audio->sample_rate);
        uint32_t frames = audio->buffer_size;

        // Read gates spread input for slot count and retrigger
        uint32_t len = 0;
        const float* gate_data = nullptr;
        if (ctx->input_spreads) {
            const auto& gates_sp = ctx->input_spreads[0];
            len = std::min(gates_sp.length, static_cast<uint32_t>(kMaxSlots));
            gate_data = gates_sp.data;
        }

        if (len == 0) {
            // No gates → no output
            if (ctx->output_spreads) {
                ctx->output_spreads[0].length = 0;
            }
            prev_len_ = 0;
            return;
        }

        if (lfo_mode == 0) {
            // Free-running: single phase from time, all slots get same value
            double phase = std::fmod(ctx->time * static_cast<double>(freq), 1.0);
            if (phase < 0.0) phase += 1.0;
            double raw = audio_dsp::waveform(phase, wave);
            float value = static_cast<float>(raw) * amp + off;

            if (ctx->output_spreads) {
                auto& out_sp = ctx->output_spreads[0];
                uint32_t out_len = std::min(len, out_sp.capacity);
                out_sp.length = out_len;
                for (uint32_t i = 0; i < out_len; ++i) {
                    out_sp.data[i] = value;
                }
            }
        } else {
            // Per-voice: independent phase per slot, gate-on resets phase
            double phase_inc = static_cast<double>(freq) / sample_rate;

            // Detect gate edges for retrigger
            for (uint32_t i = 0; i < len; ++i) {
                float cur_gate = gate_data ? gate_data[i] : 0.0f;
                float prev_gate = (i < prev_len_) ? prev_gates_[i] : 0.0f;

                if ((cur_gate > 0.5f) && (prev_gate <= 0.5f)) {
                    phases_[i] = 0.0;  // Reset phase on gate-on
                }

                prev_gates_[i] = cur_gate;
            }

            // Advance phases per-sample
            for (uint32_t s = 0; s < frames; ++s) {
                for (uint32_t i = 0; i < len; ++i) {
                    phases_[i] += phase_inc;
                    if (phases_[i] >= 1.0) phases_[i] -= 1.0;
                }
            }

            // Write output
            if (ctx->output_spreads) {
                auto& out_sp = ctx->output_spreads[0];
                uint32_t out_len = std::min(len, out_sp.capacity);
                out_sp.length = out_len;
                for (uint32_t i = 0; i < out_len; ++i) {
                    double raw = audio_dsp::waveform(phases_[i], wave);
                    out_sp.data[i] = static_cast<float>(raw) * amp + off;
                }
            }
        }

        // Clear disappeared slots
        for (uint32_t i = len; i < prev_len_; ++i) {
            prev_gates_[i] = 0.0f;
            phases_[i] = 0.0;
        }
        prev_len_ = len;
    }
};

VIVID_REGISTER(SpreadLFO)
