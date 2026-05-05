// Test operator: strategy-independent frame-rate pointwise operator.
//
// Multiplies input by 2 and accumulates into per-lane state via
// vivid_lane_state(). Used to test frame-domain LoopBased lifting.

#include "operator_api/operator.h"

struct LaneFrameOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "LaneFrameOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    struct State {
        float accumulated = 0.0f;
        uint32_t tick_count = 0;
    };

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",    VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"output",   VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"lane_ids", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float in = ctx->input_values[0];

        // Per-lane state via vivid_lane_state (same macro as audio path)
        if (ctx->lane_state_fn && ctx->lane_state_service) {
            State& s = *vivid_lane_state(ctx, ctx->lane_id, State);
            s.accumulated += in;
            s.tick_count++;
            ctx->output_values[0] = s.accumulated;
        } else {
            // Scalar fallback (no lane state service)
            ctx->output_values[0] = in * 2.0f;
        }
    }
};

