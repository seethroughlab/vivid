// Many-value source used by the audio-frame bridge snapshot tests.
// Generates value data [base*1, base*2, ..., base*count] via the value API
// (ctx->value_outputs) — the successor to ctx->output_lanes. (7d.5b)
#include "operator_api/operator.h"

struct LaneSourceOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "LaneSourceOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE;

    vivid::Param<float> base{"base", 1.0f, 0.0f, 100.0f};
    vivid::Param<int>   count{"count", 4, 1, 64};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&base);
        out.push_back(&count);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float b = ctx->param_values[0];
        int   n = static_cast<int>(ctx->param_values[1]);

        ctx->output_values[0] = b;

        // Write value array: [base*1, base*2, ..., base*count]
        VividValueOutput* out = ctx->value_outputs ? &ctx->value_outputs[0] : nullptr;
        uint32_t len = static_cast<uint32_t>(n);
        float* buf = vivid_value_output_floats(out, len);
        if (buf) {
            for (uint32_t i = 0; i < len; ++i) {
                buf[i] = b * static_cast<float>(i + 1);
            }
            vivid_value_output_commit(out, len);
        }
    }
};

