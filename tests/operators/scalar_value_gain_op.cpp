// Pure value-API scalar control operator (lane-value clean-break, Phase 5c).
//
// Reads its scalar input ONLY via ctx->values[0] and writes its output ONLY via
// ctx->value_outputs[0] — it never touches ctx->input_values/output_values. Used
// to verify the scalar-float value path end-to-end: the input value view must
// carry the scalar (aliased from input_values when there's no lane), and the
// value output must reach downstream scalar consumers (via the 1-element-lane
// propagation). If this op works between scalar neighbors, the value API fully
// carries the frame scalar-float path (the Phase-6 prerequisite).
#include "operator_api/operator.h"

struct ScalarValueGainOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "ScalarValueGainOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_MAP;

    vivid::Param<float> gain{"gain", 2.0f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&gain); }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        const float g = ctx->param_values[0];
        const VividValueView* in  = ctx->values        ? &ctx->values[0]        : nullptr;
        const float*          src = vivid_value_floats(in);
        const uint32_t        n   = in ? vivid_value_count(in) : 0u;
        if (n == 0 || !src) return;  // pure value API: NO input_values fallback

        VividValueOutput* out = ctx->value_outputs ? &ctx->value_outputs[0] : nullptr;
        float* dst = vivid_value_output_floats(out, n);
        if (dst) {
            for (uint32_t i = 0; i < n; ++i) dst[i] = src[i] * g;
            vivid_value_output_commit(out, n);
        }
        // Intentionally does NOT write ctx->output_values — pure value API.
    }
};
