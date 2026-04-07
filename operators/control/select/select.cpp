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
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_REDUCTION;

    vivid::Param<int> lane{"lane", 0, 0, 1023};

    Select() {
        vivid::description(lane, "Zero-based index of the lane to extract");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&lane);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_lanes, ctx->param_values, ctx->output_values);
    }


private:
    void compute(const VividLaneView* in_lanes, const float* params,
                 float* output_values) {
        if (!output_values) return;
        if (!in_lanes || in_lanes[0].length == 0) {
            output_values[0] = 0.0f;
            return;
        }
        auto& in = in_lanes[0];
        uint32_t idx = std::clamp(static_cast<uint32_t>(params[0]),
                                   0u, in.length - 1);
        output_values[0] = in.data[idx];
    }
};

VIVID_REGISTER(Select)
