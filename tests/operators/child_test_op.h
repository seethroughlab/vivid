#pragma once

#include "operator_api/operator.h"

// Minimal operator for testing ChildOp: scales input by a param.
// output = input * gain
struct ChildTestOp : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "ChildTestOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0] * gain.value;
    }
};
