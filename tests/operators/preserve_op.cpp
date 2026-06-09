// Test fixture: a PRESERVE op (many → many pass-through, no per-element compute).
// No seed operator declares PRESERVE yet, so this exercises the value-flow
// inference path for it — output multiplicity follows the input (Many in → Many out).
#include "operator_api/operator.h"

struct PreserveOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "PreserveOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_PRESERVE;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0];
        const VividValueView* iv = ctx->values        ? &ctx->values[0]        : nullptr;
        VividValueOutput*     ov = ctx->value_outputs ? &ctx->value_outputs[0] : nullptr;
        const float*          src = vivid_value_floats(iv);
        const uint32_t        len = vivid_value_count(iv);
        if (src && len > 0) {
            float* buf = vivid_value_output_floats(ov, len);
            if (buf) {
                for (uint32_t i = 0; i < len; ++i) buf[i] = src[i];   // pass through
                vivid_value_output_commit(ov, len);
            }
        }
    }
};
