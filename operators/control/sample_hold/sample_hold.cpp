#include "operator_api/operator.h"
#include <cmath>

struct SampleHold : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "SampleHold";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> mode{"mode", 0, {"sample", "track_and_hold"}};

    SampleHold() {
        vivid::semantic_tag(mode, "mode");
        vivid::semantic_shape(mode, "enum");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"signal",  VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"trigger", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"value",   VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float signal = ctx->input_values[0];
        bool trig = ctx->input_values[1] > 0.5f;
        bool rising = trig && !prev_trigger_;
        prev_trigger_ = trig;

        if (mode.int_value() == 0) {
            // Sample mode: capture on rising edge only
            if (rising)
                held_value_ = signal;
        } else {
            // Track-and-hold: pass through while high, hold when low
            if (trig)
                held_value_ = signal;
        }

        ctx->output_values[0] = held_value_;
    }

private:
    float held_value_ = 0.0f;
    bool prev_trigger_ = false;
};

VIVID_REGISTER(SampleHold)
