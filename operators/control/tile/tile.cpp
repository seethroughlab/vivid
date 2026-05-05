#include "operator_api/operator.h"
#include <algorithm>

/**
 * @brief Repeat a short lane pattern to fill a longer lane array.
 *
 * Tiles the input lane array cyclically to produce an output of the target
 * length. For example, a 3-element input tiled to 9 produces
 * [a, b, c, a, b, c, a, b, c].
 *
 * Use this to match a short modulation pattern to a larger polyphonic
 * lane set without introducing a lane-set mismatch.
 *
 * @tip Tile a 2-element lane array to 8 lanes for alternating left/right panning.
 * @see Repeat, Select, Stack
 */
struct Tile : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "Tile";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    vivid::Param<int> count{"count", 8, 1, 1024};

    Tile() {
        vivid::description(count, "Target output length (tiles input pattern to fill)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&count);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_lanes, ctx->param_values,
                ctx->output_lanes, ctx->output_values);
    }


private:
    void compute(const VividLaneView* in_lanes, const float* params,
                 VividLaneOutput* out_lanes, float* output_values) {
        if (!in_lanes || !out_lanes) return;
        auto& in = in_lanes[0];
        auto& out = out_lanes[0];

        if (in.length == 0) {
            out.commit(out.handle, 0);
            if (output_values) output_values[0] = 0.0f;
            return;
        }

        uint32_t n = std::clamp(static_cast<uint32_t>(params[0]), 1u, 1024u);
        float* buf = out.resize(out.handle, n);
        if (!buf) return;
        for (uint32_t i = 0; i < n; ++i)
            buf[i] = in.data[i % in.length];
        out.commit(out.handle, n);

        if (output_values)
            output_values[0] = buf[0];
    }
};

VIVID_DEFINE_OP(Tile) {
}

