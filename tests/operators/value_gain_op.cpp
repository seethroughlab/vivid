// Value-API example operator (lane-value clean-break, Phase 4a).
//
// Maps each input value to input * gain using the value-model API
// (VividValueView / VividValueOutput) — the successor to the lane views — rather
// than ctx->input_lanes/output_lanes. Exercises the value path end-to-end through
// real frame execution, interoperating with lane-API neighbors. Declares Map
// multiplicity behavior.
#include "operator_api/operator.h"

struct ValueGainOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "ValueGainOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_MAP;

    vivid::Param<float> gain{"gain", 2.0f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&gain); }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        const float g = ctx->param_values[0];

        const VividValueView* in  = ctx->values        ? &ctx->values[0]        : nullptr;
        VividValueOutput*     out = ctx->value_outputs  ? &ctx->value_outputs[0] : nullptr;
        const float*          src = vivid_value_floats(in);
        const uint32_t        n   = in ? vivid_value_count(in) : 0u;

        if (n <= 1) {
            // Scalar: map the single scalar input through the value output.
            const float v = (src && n == 1) ? src[0] : ctx->input_values[0];
            float* dst = vivid_value_output_floats(out, 1);
            if (dst) { dst[0] = v * g; vivid_value_output_commit(out, 1); }
            ctx->output_values[0] = v * g;
            return;
        }

        // Many: map each element through the value output (Map behavior).
        float* dst = vivid_value_output_floats(out, n);
        if (dst) {
            for (uint32_t i = 0; i < n; ++i) dst[i] = (src ? src[i] : 0.0f) * g;
            vivid_value_output_commit(out, n);
        }
        ctx->output_values[0] = src ? src[0] * g : 0.0f;
    }
};
