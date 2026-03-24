#pragma once

#include "operator_api/operator.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct LFO : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "LFO";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> frequency    {"frequency",     1.0f,  0.01f, 20.0f};
    vivid::Param<float> amplitude    {"amplitude",     1.0f,  0.0f,  10.0f};
    vivid::Param<float> offset       {"offset",        0.0f, -10.0f, 10.0f};
    vivid::Param<int>   waveform     {"waveform",      0, {"sine", "saw", "square", "triangle", "sample_hold", "smooth_random", "noise"}};
    vivid::Param<int>   rate_mode    {"rate_mode",     0, {"free", "sync"}};
    vivid::Param<int>   polarity     {"polarity",      0, {"bipolar", "unipolar"}};
    vivid::Param<float> phase_offset {"phase_offset",  0.0f, 0.0f, 1.0f};
    vivid::Param<float> fade_in      {"fade_in",       0.0f, 0.0f, 5.0f};

    double free_phase_ = 0.0;
    double prev_phase_ = 0.0;      // for detecting phase wrap
    float sh_value_    = 0.0f;     // current S&H value
    float sh_prev_     = 0.0f;     // previous random anchor (smooth random)
    float sh_next_     = 0.0f;     // next random anchor
    float sh_prev2_    = 0.0f;     // two cycles ago (cubic interp)
    uint32_t noise_seed_ = 12345;  // simple LCG seed
    float elapsed_time_ = 0.0f;   // for fade-in
    bool  gate_seen_    = false;   // track if gate was ever on

    LFO() {
        vivid::semantic_tag(frequency, "frequency_hz");
        vivid::semantic_shape(frequency, "scalar");
        vivid::semantic_unit(frequency, "Hz");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");

        vivid::semantic_tag(offset, "amplitude_linear");
        vivid::semantic_shape(offset, "scalar");
        vivid::semantic_intent(offset, "dc_offset");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(rate_mode,     VIVID_DISPLAY_DEFAULT);
        display_hint(polarity,      VIVID_DISPLAY_DEFAULT);
        display_hint(phase_offset,  VIVID_DISPLAY_KNOB);
        display_hint(fade_in,       VIVID_DISPLAY_KNOB);

        layout_row(phase_offset, 2, 0);
        layout_row(fade_in,      2, 1);

        out.push_back(&frequency);
        out.push_back(&phase_offset);
        out.push_back(&fade_in);
        out.push_back(&amplitude);
        out.push_back(&offset);
        out.push_back(&waveform);
        out.push_back(&rate_mode);
        out.push_back(&polarity);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate",       VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"beat_phase", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"value",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    // Simple LCG random: returns value in [-1, 1]
    float lcg_random() {
        noise_seed_ = noise_seed_ * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(noise_seed_)) / 2147483648.0f;
    }

    // Catmull-Rom cubic interpolation between 4 points
    static float catmull_rom(float p0, float p1, float p2, float p3, float t) {
        return 0.5f * ((2.0f * p1) +
               (-p0 + p2) * t +
               (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t * t +
               (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t * t * t);
    }

    void process_audio(const VividAudioContext* ctx) override {
        float gate_in  = ctx->input_float_values[0];
        float phase_in = ctx->input_float_values[1];
        const double sample_dt = 1.0 / static_cast<double>(ctx->sample_rate);

        // Gate tracking for fade-in reset (once per buffer, from cross-domain scalar)
        bool gate_on = gate_in > 0.5f;
        if (gate_on && !gate_seen_) {
            elapsed_time_ = 0.0f;
            gate_seen_ = true;
        } else if (!gate_on) {
            gate_seen_ = false;
        }

        int wf = waveform.int_value();
        int rm = rate_mode.int_value();
        int pol = polarity.int_value();
        float amp = amplitude.value;
        float off = offset.value;
        float ph_off = static_cast<float>(phase_offset.value);
        float fade = fade_in.value;
        float freq = frequency.value;

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            elapsed_time_ += static_cast<float>(sample_dt);

            // Phase computation
            double phase;
            if (rm == 1 && phase_in != 0.0f) {
                phase = std::fmod(static_cast<double>(phase_in) * static_cast<double>(freq), 1.0);
            } else if (phase_in != 0.0f && rm == 0) {
                phase = std::fmod(static_cast<double>(phase_in), 1.0);
            } else {
                free_phase_ += sample_dt * static_cast<double>(freq);
                free_phase_ -= std::floor(free_phase_);
                phase = free_phase_;
            }

            // Apply phase offset
            phase = std::fmod(phase + static_cast<double>(ph_off), 1.0);

            // Detect phase wrap for S&H and smooth random
            bool phase_wrapped = (phase < prev_phase_ - 0.5);
            prev_phase_ = phase;

            double raw = 0.0;
            switch (wf) {
                case 0: raw = std::sin(phase * 2.0 * M_PI); break;
                case 1: raw = 2.0 * phase - 1.0; break;
                case 2: raw = phase < 0.5 ? 1.0 : -1.0; break;
                case 3: raw = 4.0 * (phase < 0.5 ? phase : (1.0 - phase)) - 1.0; break;
                case 4:
                    if (phase_wrapped) sh_value_ = lcg_random();
                    raw = static_cast<double>(sh_value_);
                    break;
                case 5: {
                    if (phase_wrapped) {
                        sh_prev2_ = sh_prev_;
                        sh_prev_ = sh_next_;
                        sh_next_ = lcg_random();
                        sh_value_ = lcg_random();
                    }
                    float t = static_cast<float>(phase);
                    raw = static_cast<double>(catmull_rom(sh_prev2_, sh_prev_, sh_next_, sh_value_, t));
                    break;
                }
                case 6: raw = static_cast<double>(lcg_random()); break;
            }

            if (pol == 1) raw = raw * 0.5 + 0.5;

            float output = static_cast<float>(raw) * amp + off;

            if (fade > 0.0f && elapsed_time_ < fade) {
                output *= elapsed_time_ / fade;
            }

            ctx->output_buffers[0][i] = output;
        }
    }
};
