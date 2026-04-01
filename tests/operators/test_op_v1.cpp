// Test operator v1: output = scale * 2.0
// Param: "scale" (default 1.0)
#include "operator_api/operator.h"

struct TestOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "TestOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> scale{"scale", 1.0f, 0.0f, 100.0f};

    TestOp() {
        vivid::semantic_tag(scale, "frequency_hz");
        vivid::semantic_shape(scale, "scalar");
        vivid::semantic_unit(scale, "Hz");
        vivid::semantic_intent(scale, "test_scale");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0] * 2.0f;
    }
};

VIVID_REGISTER(TestOp)
