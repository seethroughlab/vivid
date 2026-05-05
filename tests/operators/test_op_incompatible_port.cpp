// Test operator with an incompatible hot-reload port layout change.
#include "operator_api/operator.h"

struct TestOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "TestOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> scale{"scale", 1.0f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"broken_out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0] * 5.0f;
    }
};

