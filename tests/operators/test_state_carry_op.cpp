// Test operator for cross-graph state carry regression checks.
// Exposes one float param and one text param with the same node IDs across graphs.
#include "operator_api/operator.h"

struct TestStateCarryOp : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "TestStateCarryOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> scale{"scale", 1.0f, 0.0f, 100.0f};
    vivid::Param<vivid::TextValue> label{"label", "default"};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
        out.push_back(&label);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0];
    }
};

VIVID_REGISTER(TestStateCarryOp)
