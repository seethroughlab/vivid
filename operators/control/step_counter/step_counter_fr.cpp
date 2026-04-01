#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

struct StepCounterFr : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "step_counter_fr";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int> initial{"initial", 0, -1000000, 1000000};

    StepCounterFr() {
        vivid::semantic_tag(initial, "index");
        vivid::semantic_shape(initial, "int");
        vivid::description(initial, "Starting count value and reset target");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&initial);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"trigger", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"modulus", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"reset", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"index", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"wrapped", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (!initialized_) {
            step_ = initial.int_value();
            initialized_ = true;
        }

        float trigger = ctx->input_values[0];
        int modulus = std::max(1, static_cast<int>(std::floor(ctx->input_values[1])));
        bool reset = ctx->input_values[2] > 0.5f;

        bool wrapped = false;
        if (reset) {
            step_ = initial.int_value();
            if (step_ >= modulus || step_ < 0) {
                step_ = ((step_ % modulus) + modulus) % modulus;
                wrapped = true;
            }
        } else if (trigger > 0.5f && prev_trigger_ <= 0.5f) {
            step_++;
            if (step_ >= modulus) {
                step_ = 0;
                wrapped = true;
            }
        }

        prev_trigger_ = trigger;
        ctx->output_values[0] = static_cast<float>(step_);
        ctx->output_values[1] = wrapped ? 1.0f : 0.0f;
    }

private:
    float prev_trigger_ = 0.0f;
    bool initialized_ = false;
    int step_ = 0;
};

VIVID_REGISTER(StepCounterFr)
