// Test operator: accumulates per-lane state from lane input values.
//
// Strategy-independent operator used to test identity compaction.
// Each lane reads its value from the input spread (indexed by lane_index),
// accumulates it into vivid_lane_state keyed by lane_id, and writes the
// accumulated value to the audio output buffer.
//
// After compaction (lane removal + reordering), the accumulated value
// should follow the lane_id, not the positional index.

#include "operator_api/operator.h"

struct LaneStateTrackerOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "LaneStateTrackerOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    struct State {
        float accumulated = 0.0f;
        uint32_t update_count = 0;
    };

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",    VIVID_PORT_AUDIO,  VIVID_PORT_INPUT});
        out.push_back({"output",   VIVID_PORT_AUDIO,  VIVID_PORT_OUTPUT});
        // Spread inputs for per-lane values and identity-bearing lane_ids
        out.push_back({"values",   VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        out.push_back({"lane_ids", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        // Signal outputs for last-lane readback
        out.push_back({"lane_count_out",  VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"lane_id_out",     VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"accumulated_out", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
        out.push_back({"update_count_out",VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        State& s = *vivid_lane_state(ctx, ctx->lane_id, State);

        // Read per-lane value from spread input
        // Port order: input(audio)=0, values(spread)=1, lane_ids(spread)=2
        // input_lanes is indexed by input port ordinal
        float value = 0.0f;
        if (ctx->input_lanes) {
            uint32_t ci = ctx->lane_index;
            const auto& val_sp = ctx->input_lanes[1];  // "values" spread (port index 1)
            if (val_sp.data && ci < val_sp.length)
                value = val_sp.data[ci];
        }

        s.accumulated += value;
        s.update_count++;

        // Write accumulated state as DC to audio output (for per-lane readback via buffer offsets)
        float* out = ctx->output_buffers[0];
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            out[i] = s.accumulated;

        // Signal outputs (last lane wins — used for quick sanity checks, not per-lane verification)
        if (ctx->output_float_values) {
            ctx->output_float_values[0] = static_cast<float>(ctx->lane_count);
            ctx->output_float_values[1] = static_cast<float>(ctx->lane_id);
            ctx->output_float_values[2] = s.accumulated;
            ctx->output_float_values[3] = static_cast<float>(s.update_count);
        }
    }
};

VIVID_REGISTER(LaneStateTrackerOp)
