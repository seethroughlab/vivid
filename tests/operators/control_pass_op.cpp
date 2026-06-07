// Control passthrough with multi-value support: output = input * gain
// Uses the value API (ctx->values/value_outputs) — successor to the lane views. (7d.5b)
#include "operator_api/operator.h"

struct ControlPassOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "ControlPassOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_MAP;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float g = ctx->param_values[0];
        float in = ctx->input_values[0];
        ctx->output_values[0] = in * g;

        // Value propagation: multiply each element by gain
        const VividValueView* iv = ctx->values        ? &ctx->values[0]        : nullptr;
        VividValueOutput*     ov = ctx->value_outputs ? &ctx->value_outputs[0] : nullptr;
        const float*          src = vivid_value_floats(iv);
        const uint32_t        n   = vivid_value_count(iv);
        if (src && n > 0) {
            float* buf = vivid_value_output_floats(ov, n);
            if (buf) {
                for (uint32_t i = 0; i < n; ++i) {
                    buf[i] = src[i] * g;
                }
                vivid_value_output_commit(ov, n);
            }
        }
    }
};

