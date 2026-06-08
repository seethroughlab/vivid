// Value-API string example operator (lane-value clean-break, Phase 4b).
//
// Echoes a many-string (STRING_LANES) input to its output using the value-model
// API (VividValueView strings + VividValueOutput::set_string) — the successor to
// the string-lane views — rather than ctx->input_string_lanes/output_string_lanes.
// Exercises the many-string value path through real frame execution, interoperating
// with string-lane neighbors. Declares Map multiplicity behavior.
#include "operator_api/operator.h"

struct StringValueEchoOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "StringValueEchoOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_MAP;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({.name="in",  .type=VIVID_PORT_STRING, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="out", .type=VIVID_PORT_STRING, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
    }

    void process_frame(const VividFrameContext* ctx) override {
        const VividValueView* in  = ctx->values       ? &ctx->values[0]       : nullptr;
        VividValueOutput*     out = ctx->value_outputs ? &ctx->value_outputs[0] : nullptr;
        const char* const*    strs = vivid_value_strings(in);
        const uint32_t        n    = in ? vivid_value_count(in) : 0u;

        if (!out || !out->resize) return;
        if (out->resize(out->handle, n)) {            // size the output (RT-safe)
            for (uint32_t i = 0; i < n; ++i)
                vivid_value_output_set_string(out, i, strs ? strs[i] : "");
            vivid_value_output_commit(out, n);
        }
    }
};
