#include "operator_api/operator.h"
#include <algorithm>

/**
 * @brief Broadcast a scalar value to N lanes.
 *
 * Takes a single input value and produces an output spread of the
 * specified length, with every element set to the input value.
 * This is the explicit version of scalar broadcast — use it to match
 * a control signal to a polyphonic lane set.
 *
 * @tip Connect a knob or LFO to the input, then wire the output alongside
 *      a polyphonic spread to apply the same modulation to every voice.
 * @see Tile, Select, Stack
 */
struct Repeat : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName = "Repeat";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    vivid::Param<int> count{"count", 4, 1, 1024};

    Repeat() {
        vivid::description(count, "Number of output lanes");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&count);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values,
                ctx->output_spreads, ctx->output_values);
    }

    void process_audio(const VividAudioContext* ctx) override {
        float in = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        compute(in, ctx->param_values, ctx->output_spreads, ctx->output_float_values);
    }

private:
    void compute(float input, const float* params,
                 VividSpreadPort* out_spreads, float* output_values) {
        if (!out_spreads) return;
        auto& out = out_spreads[0];
        uint32_t n = std::clamp(static_cast<uint32_t>(params[0]), 1u, out.capacity);
        out.length = n;
        for (uint32_t i = 0; i < n; ++i)
            out.data[i] = input;
        if (output_values)
            output_values[0] = input;
    }
};

VIVID_REGISTER(Repeat)
