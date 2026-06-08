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
        out.push_back({.name="input",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="output", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->values, ctx->param_values,
                ctx->value_outputs, ctx->output_values);
    }


private:
    void compute(const VividValueView* in_values, const float* params,
                 VividValueOutput* out_values, float* output_values) {
        if (!in_values || !out_values) return;
        const VividValueView* in = &in_values[0];
        VividValueOutput* out = &out_values[0];

        uint32_t in_length = vivid_value_count(in);
        const float* in_data = vivid_value_floats(in);

        if (in_length == 0) {
            vivid_value_output_commit(out, 0);
            if (output_values) output_values[0] = 0.0f;
            return;
        }

        uint32_t n = std::clamp(static_cast<uint32_t>(params[0]), 1u, 1024u);
        float* buf = vivid_value_output_floats(out, n);
        if (!buf) return;
        for (uint32_t i = 0; i < n; ++i)
            buf[i] = in_data[i % in_length];
        vivid_value_output_commit(out, n);

        if (output_values)
            output_values[0] = buf[0];
    }
};

VIVID_DEFINE_OP(Tile) {
}

