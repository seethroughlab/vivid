#include "operator_api/operator.h"
#include <algorithm>

/**
 * @brief Repeat a short lane pattern to fill a longer spread.
 *
 * Tiles the input spread cyclically to produce an output of the target
 * length. For example, a 3-element input tiled to 9 produces
 * [a, b, c, a, b, c, a, b, c].
 *
 * Use this to match a short modulation pattern to a larger polyphonic
 * lane set without introducing a lane-set mismatch.
 *
 * @tip Tile a 2-element spread to 8 lanes for alternating left/right panning.
 * @see Repeat, Select, Stack
 */
struct Tile : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
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
        out.push_back({"input",  VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_spreads, ctx->param_values,
                ctx->output_spreads, ctx->output_values);
    }

    void process_audio(const VividAudioContext* ctx) override {
        compute(ctx->input_spreads, ctx->param_values,
                ctx->output_spreads, ctx->output_float_values);
    }

private:
    void compute(VividSpreadPort* in_spreads, const float* params,
                 VividSpreadPort* out_spreads, float* output_values) {
        if (!in_spreads || !out_spreads) return;
        auto& in = in_spreads[0];
        auto& out = out_spreads[0];

        if (in.length == 0) {
            out.length = 0;
            if (output_values) output_values[0] = 0.0f;
            return;
        }

        uint32_t n = std::clamp(static_cast<uint32_t>(params[0]), 1u, out.capacity);
        out.length = n;
        for (uint32_t i = 0; i < n; ++i)
            out.data[i] = in.data[i % in.length];

        if (output_values)
            output_values[0] = out.data[0];
    }
};

VIVID_REGISTER(Tile)
