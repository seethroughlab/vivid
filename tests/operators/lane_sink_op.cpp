// Many-value sink: reads input value data and copies it to output (passthrough).
// Uses the value API (ctx->values/value_outputs) — successor to the lane views. (7d.5b)
// (Operator name kept as LaneSinkOp for test fixture compatibility.)
#include "operator_api/operator.h"

struct LaneSinkOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "LaneSinkOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_MAP;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0];

        // Copy input value array to output value array
        const VividValueView* iv = ctx->values        ? &ctx->values[0]        : nullptr;
        VividValueOutput*     ov = ctx->value_outputs ? &ctx->value_outputs[0] : nullptr;
        const float*          src = vivid_value_floats(iv);
        const uint32_t        len = vivid_value_count(iv);
        if (src && len > 0) {
            float* buf = vivid_value_output_floats(ov, len);
            if (buf) {
                for (uint32_t i = 0; i < len; ++i)
                    buf[i] = src[i];
                vivid_value_output_commit(ov, len);
            }
        }
    }
};

