#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"
#include "control/smooth/smooth.h"

// ModulatedGain — example composite operator using ChildOp
//
// Passes an input signal through a gain stage modulated by an internal LFO.
// The LFO output is smoothed before applying, giving a "breathing" effect.
//
// output = input * (base_gain + lfo_depth * smoothed_lfo)

struct ModulatedGain : vivid::OperatorBase {
    static constexpr const char* kName   = "ModulatedGain";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> base_gain {"base_gain",  1.0f, 0.0f, 10.0f};
    vivid::Param<float> lfo_rate  {"lfo_rate",   2.0f, 0.01f, 20.0f};
    vivid::Param<float> lfo_depth {"lfo_depth",  0.5f, 0.0f, 10.0f};
    vivid::Param<int>   lfo_shape {"lfo_shape",  0, {"sine", "saw", "square", "triangle"}};
    vivid::Param<float> smooth_time{"smooth_time", 0.05f, 0.0f, 2.0f};

    vivid::ChildOp<LFO>    lfo_;
    vivid::ChildOp<Smooth> smoother_;

    ModulatedGain() {
        vivid::semantic_tag(base_gain, "amplitude_linear");
        vivid::semantic_shape(base_gain, "scalar");

        vivid::semantic_tag(lfo_rate, "frequency_hz");
        vivid::semantic_shape(lfo_rate, "scalar");
        vivid::semantic_unit(lfo_rate, "Hz");

        vivid::semantic_tag(lfo_depth, "amplitude_linear");
        vivid::semantic_shape(lfo_depth, "scalar");

        vivid::semantic_tag(smooth_time, "time_seconds");
        vivid::semantic_shape(smooth_time, "scalar");
        vivid::semantic_unit(smooth_time, "s");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&base_gain);
        out.push_back(&lfo_rate);
        out.push_back(&lfo_depth);
        out.push_back(&lfo_shape);
        out.push_back(&smooth_time);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(VividProcessContext* ctx) override {
        float input = ctx->input_values[0];

        // Drive the internal LFO
        lfo_.set_param("frequency", lfo_rate.value);
        lfo_.set_param("amplitude", 1.0f);
        lfo_.set_param("offset", 0.0f);
        lfo_.set_param("waveform", static_cast<float>(lfo_shape.int_value()));
        lfo_.process(ctx);

        // Smooth the LFO output
        smoother_.set_param("rise_time", smooth_time.value);
        smoother_.set_param("fall_time", smooth_time.value);
        smoother_.set_input("input", lfo_.output("value"));
        smoother_.process(ctx);

        float mod = smoother_.output("value");
        float gain = base_gain.value + lfo_depth.value * mod;
        ctx->output_values[0] = input * gain;
    }
};

VIVID_REGISTER(ModulatedGain)
