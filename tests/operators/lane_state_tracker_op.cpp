// Test operator: accumulates per-lane state from many-value input.
//
// Strategy-independent operator used to test identity compaction.
// Each lane reads its value from the many-value input (ctx->values, indexed by
// lane_index — a lifted invocation sees the FULL view), accumulates it into
// vivid_lane_state keyed by lane_id, and writes the accumulated value to the
// audio output buffer. Uses the value API — successor to the lane views. (7d.5b)
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
        out.push_back({"input",    VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_INPUT});
        out.push_back({"output",   VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_OUTPUT});
        // Lane-array inputs for per-lane values and identity-bearing lane_ids
        out.push_back({.name="values",   .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="lane_ids", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        // Signal outputs for last-lane readback
        out.push_back({"lane_count_out",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"lane_id_out",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"accumulated_out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"update_count_out",VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        State& s = *vivid_lane_state(ctx, ctx->lane_id, State);

        // Read per-lane value from the many-value input.
        // Port order: input(audio)=0, values(many)=1, lane_ids(many)=2
        // ctx->values is indexed by input port ordinal; a lifted invocation sees
        // the FULL view, so index it by lane_index.
        float value = 0.0f;
        if (ctx->values) {
            uint32_t ci = ctx->lane_index;
            const VividValueView* val = &ctx->values[1];  // "values" many input (port index 1)
            const float* data = vivid_value_floats(val);
            if (data && ci < vivid_value_count(val))
                value = data[ci];
        }

        s.accumulated += value;
        s.update_count++;

        // Write accumulated state as DC to audio output (for per-lane readback via buffer offsets)
        float* out = ctx->output_buffers[0];
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            out[i] = s.accumulated;

        // Signal outputs at ports 1-4 (last lane wins — quick sanity checks).
        // Port 0 is the main audio output (accumulated DC), ports 1-4 are diagnostics.
        {
            float vals[4];
            vals[0] = static_cast<float>(ctx->lane_count);
            vals[1] = static_cast<float>(ctx->lane_id);
            vals[2] = s.accumulated;
            vals[3] = static_cast<float>(s.update_count);
            for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
                for (int j = 0; j < 4; ++j)
                    ctx->output_buffers[1 + j][i] = vals[j];
            }
        }
    }
};

