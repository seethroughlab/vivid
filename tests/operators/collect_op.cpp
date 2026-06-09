// Test fixture: a COLLECT op (scalars → one many). No seed operator declares
// COLLECT yet, so this exercises the value-flow inference path for it — a COLLECT
// node's output is Many regardless of input multiplicity. Emits [in, in+1, in+2].
#include "operator_api/operator.h"

struct CollectOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "CollectOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_COLLECT;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float in = ctx->input_values[0];
        ctx->output_values[0] = in;
        VividValueOutput* ov = ctx->value_outputs ? &ctx->value_outputs[0] : nullptr;
        float* buf = vivid_value_output_floats(ov, 3);
        if (buf) {
            buf[0] = in; buf[1] = in + 1.0f; buf[2] = in + 2.0f;
            vivid_value_output_commit(ov, 3);
        }
    }
};
