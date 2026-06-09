// Dual many-value sink: two many-capable inputs, copies each to a corresponding
// output. Used to verify scalar-to-many broadcasting. Uses the value API
// (ctx->values/value_outputs) — successor to the lane views. (7d.5b)
#include "operator_api/operator.h"

struct DualLaneSinkOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "DualLaneSinkOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_MAP;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({.name="in_a", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="in_b", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({"out_a", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"out_b", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0];
        ctx->output_values[1] = ctx->input_values[1];

        if (!ctx->values || !ctx->value_outputs) return;

        // Copy input value arrays to output value arrays
        for (int port = 0; port < 2; ++port) {
            const VividValueView* iv = &ctx->values[port];
            VividValueOutput*     ov = &ctx->value_outputs[port];
            const float*          src = vivid_value_floats(iv);
            const uint32_t        len = vivid_value_count(iv);
            float* buf = vivid_value_output_floats(ov, len);
            if (buf) {
                for (uint32_t i = 0; i < len; ++i)
                    buf[i] = src ? src[i] : 0.0f;
                vivid_value_output_commit(ov, len);
            }
        }
    }
};

