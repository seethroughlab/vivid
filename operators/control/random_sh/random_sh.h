#pragma once

#include "operator_api/operator.h"
#include <cmath>

struct RandomSH : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "RandomSH";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> rate         {"rate",         1.0f,  0.1f,  20.0f};
    vivid::Param<float> amplitude    {"amplitude",    1.0f,  0.0f,  10.0f};
    vivid::Param<float> offset       {"offset",       0.0f, -10.0f, 10.0f};
    vivid::Param<float> slew         {"slew",         0.0f,  0.0f,  1.0f};
    vivid::Param<int>   distribution {"distribution", 0, {"uniform", "gaussian"}};
    vivid::Param<int>   mode         {"mode",         0, {"timed", "triggered"}};
    vivid::Param<int>   polarity     {"polarity",     1, {"bipolar", "unipolar"}};
    vivid::Param<int>   seed         {"seed",         12345, 1, 99999};

    RandomSH() {
        vivid::semantic_tag(rate, "frequency_hz");
        vivid::semantic_shape(rate, "scalar");
        vivid::semantic_unit(rate, "Hz");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::semantic_intent(amplitude, "env_amount");

        vivid::semantic_tag(offset, "amplitude_linear");
        vivid::semantic_shape(offset, "scalar");
        vivid::semantic_intent(offset, "dc_offset");

        vivid::semantic_tag(slew, "probability_01");
        vivid::semantic_shape(slew, "scalar");

        vivid::semantic_tag(seed, "seed");
        vivid::semantic_shape(seed, "int");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(rate, VIVID_DISPLAY_KNOB);
        display_hint(slew, VIVID_DISPLAY_KNOB);

        layout_row(rate, 2, 0);
        layout_row(slew, 2, 1);

        out.push_back(&rate);
        out.push_back(&amplitude);
        out.push_back(&offset);
        out.push_back(&slew);
        out.push_back(&distribution);
        out.push_back(&mode);
        out.push_back(&polarity);
        out.push_back(&seed);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate",       VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"beat_phase", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"value",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float dt = static_cast<float>(ctx->delta_time);
        float gate_in = ctx->input_values[0];
        bool gate_on = gate_in > 0.5f;

        // Init RNG from seed on first call
        if (!seeded_) {
            rng_state_ = static_cast<uint32_t>(seed.int_value());
            if (rng_state_ == 0) rng_state_ = 1;
            target_value_ = generate_random();
            current_value_ = target_value_;
            seeded_ = true;
        }

        bool new_sample = false;

        if (mode.int_value() == 0) {
            // Timed mode: free-running phase
            phase_ += static_cast<double>(dt) * static_cast<double>(rate.value);
            if (phase_ >= 1.0) {
                phase_ -= std::floor(phase_);
                new_sample = true;
            }
        } else {
            // Triggered mode: gate rising edge
            if (gate_on && !prev_gate_) {
                new_sample = true;
            }
        }

        prev_gate_ = gate_on;

        if (new_sample) {
            target_value_ = generate_random();
        }

        // Slew: interpolate toward target
        float slew_val = slew.value;
        if (slew_val < 0.001f) {
            current_value_ = target_value_;
        } else {
            float slew_factor = 1.0f - slew_val * slew_val * slew_val;
            current_value_ += (target_value_ - current_value_) * slew_factor;
        }

        // Polarity: if unipolar, remap [-1,1] -> [0,1]
        float out_val = current_value_;
        if (polarity.int_value() == 1) {
            out_val = out_val * 0.5f + 0.5f;
        }

        ctx->output_values[0] = out_val * amplitude.value + offset.value;
    }

private:
    uint32_t rng_state_ = 12345;
    float target_value_  = 0.0f;
    float current_value_ = 0.0f;
    double phase_        = 0.0;
    bool prev_gate_      = false;
    bool seeded_         = false;

    uint32_t rng_next() {
        rng_state_ ^= rng_state_ << 13;
        rng_state_ ^= rng_state_ >> 17;
        rng_state_ ^= rng_state_ << 5;
        return rng_state_;
    }

    // Generate random value in [-1, 1]
    float generate_random() {
        if (distribution.int_value() == 0) {
            // Uniform [-1, 1]
            float u = static_cast<float>(rng_next()) / 4294967295.0f;
            return u * 2.0f - 1.0f;
        } else {
            // Gaussian via Box-Muller, clamped to [-1, 1]
            float u1 = (static_cast<float>(rng_next()) + 1.0f) / 4294967296.0f;
            float u2 = static_cast<float>(rng_next()) / 4294967295.0f;
            float z = std::sqrt(-2.0f * std::log(u1)) * std::cos(6.2831853f * u2);
            // Scale so ~99.7% falls within [-1, 1] (3 sigma = 1)
            z /= 3.0f;
            return std::max(-1.0f, std::min(1.0f, z));
        }
    }
};
