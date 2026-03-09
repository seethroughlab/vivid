#include "operator_api/operator.h"

#include <algorithm>
#include <cmath>

struct StepCounter : vivid::OperatorBase {
    static constexpr const char* kName = "StepCounter";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<int> initial{"initial", 0, -1000000, 1000000};

    float prev_trigger_ = 0.0f;
    bool initialized_ = false;
    int step_ = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&initial);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"trigger", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"modulus", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"reset", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"index", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"wrapped", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        if (!initialized_) {
            step_ = initial.int_value();
            initialized_ = true;
        }
        const float trigger = ctx->input_values[0];
        const int modulus = std::max(1, static_cast<int>(std::floor(ctx->input_values[1])));
        const bool reset = ctx->input_values[2] > 0.5f;

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
};

VIVID_REGISTER(StepCounter)
