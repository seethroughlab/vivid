// Scalar source: outputs a single scalar value from a parameter.
// Used to test scalar-to-lane-array broadcasting.
#include "operator_api/operator.h"

struct ScalarSourceOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "ScalarSourceOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> value{"value", 0.0f, -100.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&value);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0];
    }
};

