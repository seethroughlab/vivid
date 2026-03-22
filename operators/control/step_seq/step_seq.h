#pragma once

#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

struct StepSeq : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "StepSeq";
    static constexpr bool kTimeDependent = true;

    static constexpr int kMaxSteps = 32;

    // --- Visible params (indices 0–6) ---
    vivid::Param<int>   num_steps  {"num_steps",  8,    1, 32};
    vivid::Param<float> frequency  {"frequency",  1.0f, 0.01f, 20.0f};
    vivid::Param<int>   rate_mode  {"rate_mode",  0,    {"free", "sync"}};
    vivid::Param<float> glide      {"glide",      0.0f, 0.0f, 1.0f};
    vivid::Param<float> amplitude  {"amplitude",  1.0f, 0.0f, 10000.0f};
    vivid::Param<float> offset     {"offset",     0.0f, -20000.0f, 20000.0f};
    vivid::Param<int>   polarity   {"polarity",   0,    {"bipolar", "unipolar"}};

    // --- Hidden params: step values (indices 7–38) ---
    vivid::Param<float> step_value[kMaxSteps] = {
        {"step_value_0",  0.5f, 0.0f, 1.0f}, {"step_value_1",  0.5f, 0.0f, 1.0f},
        {"step_value_2",  0.5f, 0.0f, 1.0f}, {"step_value_3",  0.5f, 0.0f, 1.0f},
        {"step_value_4",  0.5f, 0.0f, 1.0f}, {"step_value_5",  0.5f, 0.0f, 1.0f},
        {"step_value_6",  0.5f, 0.0f, 1.0f}, {"step_value_7",  0.5f, 0.0f, 1.0f},
        {"step_value_8",  0.5f, 0.0f, 1.0f}, {"step_value_9",  0.5f, 0.0f, 1.0f},
        {"step_value_10", 0.5f, 0.0f, 1.0f}, {"step_value_11", 0.5f, 0.0f, 1.0f},
        {"step_value_12", 0.5f, 0.0f, 1.0f}, {"step_value_13", 0.5f, 0.0f, 1.0f},
        {"step_value_14", 0.5f, 0.0f, 1.0f}, {"step_value_15", 0.5f, 0.0f, 1.0f},
        {"step_value_16", 0.5f, 0.0f, 1.0f}, {"step_value_17", 0.5f, 0.0f, 1.0f},
        {"step_value_18", 0.5f, 0.0f, 1.0f}, {"step_value_19", 0.5f, 0.0f, 1.0f},
        {"step_value_20", 0.5f, 0.0f, 1.0f}, {"step_value_21", 0.5f, 0.0f, 1.0f},
        {"step_value_22", 0.5f, 0.0f, 1.0f}, {"step_value_23", 0.5f, 0.0f, 1.0f},
        {"step_value_24", 0.5f, 0.0f, 1.0f}, {"step_value_25", 0.5f, 0.0f, 1.0f},
        {"step_value_26", 0.5f, 0.0f, 1.0f}, {"step_value_27", 0.5f, 0.0f, 1.0f},
        {"step_value_28", 0.5f, 0.0f, 1.0f}, {"step_value_29", 0.5f, 0.0f, 1.0f},
        {"step_value_30", 0.5f, 0.0f, 1.0f}, {"step_value_31", 0.5f, 0.0f, 1.0f},
    };

    // --- Hidden params: step gates (indices 39–70) ---
    vivid::Param<float> step_gate[kMaxSteps] = {
        {"step_gate_0",  1.0f, 0.0f, 1.0f}, {"step_gate_1",  1.0f, 0.0f, 1.0f},
        {"step_gate_2",  1.0f, 0.0f, 1.0f}, {"step_gate_3",  1.0f, 0.0f, 1.0f},
        {"step_gate_4",  1.0f, 0.0f, 1.0f}, {"step_gate_5",  1.0f, 0.0f, 1.0f},
        {"step_gate_6",  1.0f, 0.0f, 1.0f}, {"step_gate_7",  1.0f, 0.0f, 1.0f},
        {"step_gate_8",  1.0f, 0.0f, 1.0f}, {"step_gate_9",  1.0f, 0.0f, 1.0f},
        {"step_gate_10", 1.0f, 0.0f, 1.0f}, {"step_gate_11", 1.0f, 0.0f, 1.0f},
        {"step_gate_12", 1.0f, 0.0f, 1.0f}, {"step_gate_13", 1.0f, 0.0f, 1.0f},
        {"step_gate_14", 1.0f, 0.0f, 1.0f}, {"step_gate_15", 1.0f, 0.0f, 1.0f},
        {"step_gate_16", 1.0f, 0.0f, 1.0f}, {"step_gate_17", 1.0f, 0.0f, 1.0f},
        {"step_gate_18", 1.0f, 0.0f, 1.0f}, {"step_gate_19", 1.0f, 0.0f, 1.0f},
        {"step_gate_20", 1.0f, 0.0f, 1.0f}, {"step_gate_21", 1.0f, 0.0f, 1.0f},
        {"step_gate_22", 1.0f, 0.0f, 1.0f}, {"step_gate_23", 1.0f, 0.0f, 1.0f},
        {"step_gate_24", 1.0f, 0.0f, 1.0f}, {"step_gate_25", 1.0f, 0.0f, 1.0f},
        {"step_gate_26", 1.0f, 0.0f, 1.0f}, {"step_gate_27", 1.0f, 0.0f, 1.0f},
        {"step_gate_28", 1.0f, 0.0f, 1.0f}, {"step_gate_29", 1.0f, 0.0f, 1.0f},
        {"step_gate_30", 1.0f, 0.0f, 1.0f}, {"step_gate_31", 1.0f, 0.0f, 1.0f},
    };

    StepSeq() {
        vivid::semantic_tag(frequency, "frequency_hz");
        vivid::semantic_shape(frequency, "scalar");
        vivid::semantic_unit(frequency, "Hz");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::semantic_intent(amplitude, "env_amount");

        vivid::semantic_tag(offset, "amplitude_linear");
        vivid::semantic_shape(offset, "scalar");
        vivid::semantic_intent(offset, "dc_offset");

        vivid::semantic_tag(glide, "probability_01");
        vivid::semantic_shape(glide, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(frequency, VIVID_DISPLAY_KNOB);
        display_hint(glide, VIVID_DISPLAY_KNOB);

        layout_row(frequency, 4, 0);
        layout_row(glide, 4, 1);

        // Visible params (indices 0–6)
        out.push_back(&num_steps);
        out.push_back(&frequency);
        out.push_back(&rate_mode);
        out.push_back(&glide);
        out.push_back(&amplitude);
        out.push_back(&offset);
        out.push_back(&polarity);

        // Hidden step values (indices 7–38)
        for (int i = 0; i < kMaxSteps; ++i) {
            display_hint(step_value[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&step_value[i]);
        }

        // Hidden step gates (indices 39–70)
        for (int i = 0; i < kMaxSteps; ++i) {
            display_hint(step_gate[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&step_gate[i]);
        }
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate",       VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});   // 0
        out.push_back({"beat_phase", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});   // 1
        out.push_back({"value",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});  // 0
        out.push_back({"trigger",    VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});  // 1
    }

    void process(const VividProcessContext* ctx) override {
        float dt = static_cast<float>(ctx->delta_time);
        float beat_phase_in = ctx->input_values[1];

        int ns = std::clamp(num_steps.int_value(), 1, kMaxSteps);
        float freq = frequency.value;
        int mode = rate_mode.int_value();
        float glide_val = glide.value;
        float amp = amplitude.value;
        float off = offset.value;
        int pol = polarity.int_value();

        // Phase computation
        double phase;
        if (mode == 1) {
            // Sync mode: derive from beat_phase input
            phase = std::fmod(static_cast<double>(beat_phase_in) * static_cast<double>(freq), 1.0);
            if (phase < 0.0) phase += 1.0;
        } else {
            // Free mode: accumulate
            free_phase_ += static_cast<double>(dt) * static_cast<double>(freq);
            free_phase_ -= std::floor(free_phase_);
            phase = free_phase_;
        }

        // Step determination
        int step = static_cast<int>(phase * ns) % ns;
        if (step < 0) step = 0;

        // Step change detection → trigger output
        float trigger_out = 0.0f;
        if (step != prev_step_) {
            trigger_out = 1.0f;
            prev_step_ = step;
        }

        // Target value for this step
        float target = step_value[step].value;

        // Gate length check
        float frac_in_step = static_cast<float>(std::fmod(phase * ns, 1.0));
        float gate_len = step_gate[step].value;
        bool gate_open = frac_in_step <= gate_len;

        // Glide: cubic-mapped slew (same as RandomSH)
        if (glide_val < 0.001f) {
            current_value_ = target;
        } else {
            float slew_factor = 1.0f - glide_val * glide_val * glide_val;
            current_value_ += (target - current_value_) * slew_factor;
        }

        // Apply gate
        float out_val = gate_open ? current_value_ : 0.0f;

        // Polarity: bipolar remaps [0,1] → [-1,1]
        if (pol == 0) {
            out_val = out_val * 2.0f - 1.0f;
        }

        // Output with amplitude and offset
        ctx->output_values[0] = out_val * amp + off;
        ctx->output_values[1] = trigger_out;
    }

    void draw_inspector(VividInspectorContext* ctx) override;

private:
    double free_phase_    = 0.0;
    int    prev_step_     = -1;
    float  current_value_ = 0.5f;
    int    dragged_step_  = -1;  // inspector drag state
};
