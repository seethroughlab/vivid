#include "operator_api/operator.h"
#include <algorithm>

/**
 * @brief Pick one lane from a multi-lane input.
 *
 * Extracts a single element from the input lane array by index, producing
 * a scalar output. This is a lane reduction — the output has no lane
 * provenance and can be freely mixed with any other lane set at
 * downstream pointwise operators.
 *
 * @tip Use Select to solo one voice from a polyphonic chain for monitoring.
 * @see Repeat, Tile, Stack
 */
struct Select : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "Select";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_REDUCE;

    vivid::Param<int> lane{"lane", 0, 0, 1023};

    Select() {
        vivid::description(lane, "Zero-based index of the lane to extract");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&lane);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({.name="input", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({"output", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->values, ctx->param_values, ctx->output_values);
    }


private:
    void compute(const VividValueView* in_values, const float* params,
                 float* output_values) {
        if (!output_values) return;
        uint32_t count = in_values ? vivid_value_count(&in_values[0]) : 0u;
        const float* data = in_values ? vivid_value_floats(&in_values[0]) : nullptr;
        if (count == 0 || !data) {
            output_values[0] = 0.0f;
            return;
        }
        uint32_t idx = std::clamp(static_cast<uint32_t>(params[0]),
                                   0u, count - 1);
        output_values[0] = data[idx];
    }
};

VIVID_DEFINE_OP(Select) {
    name = "Select";
    keywords = {"select", "lane", "index", "extract", "polyphony", "pick"};
    summary = "Extracts a single element from a multi-lane array by index.";
}

