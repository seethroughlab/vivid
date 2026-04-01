// Test operator v2: output = scale * 3.0 + offset
// Params: "scale" (default 1.0), "offset" (default 10.0)
// "scale" is preserved from v1; "offset" is new and should get its default.
#include "operator_api/operator.h"

struct TestOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "TestOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> scale{"scale", 1.0f, 0.0f, 100.0f};
    vivid::Param<float> offset{"offset", 10.0f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
        out.push_back(&offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0] * 3.0f + ctx->param_values[1];
    }
};

VIVID_REGISTER(TestOp)
