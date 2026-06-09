// Test fixture: a frame REDUCE op (many → scalar). Sums its many-valued input
// to a single scalar. Used by test_value_flow_runtime to prove the value-flow
// pass infers SCALAR for a REDUCE node's (non-breakout) output.
#include "operator_api/operator.h"

struct ReduceOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "ReduceOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_REDUCE;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        const VividValueView* iv = ctx->values ? &ctx->values[0] : nullptr;
        const float* src = vivid_value_floats(iv);
        const uint32_t len = vivid_value_count(iv);
        float sum = 0.0f;
        if (src && len > 0)
            for (uint32_t i = 0; i < len; ++i) sum += src[i];
        else
            sum = ctx->input_values[0];
        ctx->output_values[0] = sum;   // scalar reduction result
    }
};
