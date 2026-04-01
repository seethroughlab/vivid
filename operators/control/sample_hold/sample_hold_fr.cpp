#include "operator_api/operator.h"
#include <cmath>

struct SampleHoldFr : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "sample_hold_fr";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> mode{"mode", 0, {"sample", "track_and_hold"}};

    SampleHoldFr() {
        vivid::semantic_shape(mode, "enum");
        vivid::description(mode, "Sample latches on rising edge; track-and-hold follows while high");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"signal",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"trigger", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value",   VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float signal = ctx->input_values[0];
        bool trig = ctx->input_values[1] > 0.5f;
        int m = mode.int_value();

        bool rising = trig && !prev_trigger_;
        prev_trigger_ = trig;
        if (m == 0) {
            if (rising) held_value_ = signal;
        } else {
            if (trig) held_value_ = signal;
        }

        ctx->output_values[0] = held_value_;
    }

private:
    float held_value_ = 0.0f;
    bool prev_trigger_ = false;
};

VIVID_REGISTER(SampleHoldFr)
