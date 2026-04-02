#include "step_seq.h"
#include "control/audio_scalar_utils.h"

struct StepSeq_AU : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "step_seq_au";
    static constexpr bool kTimeDependent = true;

    static constexpr int kMaxSteps = 32;

    vivid::Param<int>   num_steps  {"num_steps",  8,    1, 32};
    vivid::Param<float> frequency  {"frequency",  1.0f, 0.01f, 20.0f};
    vivid::Param<int>   rate_mode  {"rate_mode",  0,    {"free", "sync"}};
    vivid::Param<float> glide      {"glide",      0.0f, 0.0f, 1.0f};
    vivid::Param<float> amplitude  {"amplitude",  1.0f, 0.0f, 10000.0f};
    vivid::Param<float> offset     {"offset",     0.0f, -20000.0f, 20000.0f};
    vivid::Param<int>   polarity   {"polarity",   0,    {"bipolar", "unipolar"}};

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

    StepSeq_AU() {
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

        vivid::description(num_steps, "Number of active steps in the sequence, 1 to 32");
        vivid::description(frequency, "Cycle rate in Hz (free mode) or beat multiplier (sync mode)");
        vivid::description(rate_mode, "Clock source: free-running internal clock or synced to beat_phase input");
        vivid::description(glide, "Smoothing between step values, 0 = instant, 1 = full portamento");
        vivid::description(amplitude, "Scales the output value");
        vivid::description(offset, "DC offset added to the output after amplitude scaling");
        vivid::description(polarity, "Output range: bipolar (-1 to 1) or unipolar (0 to 1)");
        vivid::description(step_value[0], "Value for step 1");
        vivid::description(step_gate[0], "Gate length for step 1, 0 = silent, 1 = full step");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(frequency, VIVID_DISPLAY_KNOB);
        display_hint(glide, VIVID_DISPLAY_KNOB);

        layout_row(frequency, 2, 0);
        layout_row(glide, 2, 1);

        out.push_back(&num_steps);
        out.push_back(&frequency);
        out.push_back(&rate_mode);
        out.push_back(&glide);
        out.push_back(&amplitude);
        out.push_back(&offset);
        out.push_back(&polarity);

        for (int i = 0; i < kMaxSteps; ++i) {
            display_hint(step_value[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&step_value[i]);
        }
        for (int i = 0; i < kMaxSteps; ++i) {
            display_hint(step_gate[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&step_gate[i]);
        }
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate",       VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value",      VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"trigger",    VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float local_in[2] = {
            vivid::audio_scalar_block_start(ctx, 0),  // gate
            vivid::audio_scalar_block_start(ctx, 1),  // beat_phase
        };
        float local_out[2] = {};
        compute(local_in, ctx->delta_time, local_out);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 2; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }

private:
    void compute(const float* input_values, double delta_time, float* output_values) {
        float dt = static_cast<float>(delta_time);
        float beat_phase_in = input_values[1];

        int ns = std::clamp(num_steps.int_value(), 1, kMaxSteps);
        float freq = frequency.value;
        int mode = rate_mode.int_value();
        float glide_val = glide.value;
        float amp = amplitude.value;
        float off = offset.value;
        int pol = polarity.int_value();

        double phase;
        if (mode == 1) {
            phase = std::fmod(static_cast<double>(beat_phase_in) * static_cast<double>(freq), 1.0);
            if (phase < 0.0) phase += 1.0;
        } else {
            free_phase_ += static_cast<double>(dt) * static_cast<double>(freq);
            free_phase_ -= std::floor(free_phase_);
            phase = free_phase_;
        }

        int step = static_cast<int>(phase * ns) % ns;
        if (step < 0) step = 0;

        float trigger_out = 0.0f;
        if (step != prev_step_) {
            trigger_out = 1.0f;
            prev_step_ = step;
        }

        float target = step_value[step].value;
        float frac_in_step = static_cast<float>(std::fmod(phase * ns, 1.0));
        float gate_len = step_gate[step].value;
        bool gate_open = frac_in_step <= gate_len;

        if (glide_val < 0.001f) {
            current_value_ = target;
        } else {
            float slew_factor = 1.0f - glide_val * glide_val * glide_val;
            current_value_ += (target - current_value_) * slew_factor;
        }

        float out_val = gate_open ? current_value_ : 0.0f;
        if (pol == 0) {
            out_val = out_val * 2.0f - 1.0f;
        }

        output_values[0] = out_val * amp + off;
        output_values[1] = trigger_out;
    }

    double free_phase_    = 0.0;
    int    prev_step_     = -1;
    float  current_value_ = 0.5f;
};

VIVID_REGISTER(StepSeq_AU)
