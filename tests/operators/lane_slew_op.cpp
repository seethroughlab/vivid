// Test operator: strategy-independent pointwise slew limiter.
// Uses vivid_lane_state() for per-lane state, NOT member variables.
// Declares kStrategyIndependent = true so the compiler can assign LoopBased.
//
// When receiving multi-lane lane input from a structural source, the
// runtime drives the per-lane loop. Each lane gets its own slew state
// keyed by lane_id via vivid_lane_state().

#include "operator_api/operator.h"
#include <cmath>

struct LaneSlewOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "LaneSlewOp";
    static constexpr bool kTimeDependent = false;
    static constexpr bool kStrategyIndependent = true;

    vivid::Param<float> rate {"rate", 0.1f, 0.001f, 1.0f};

    struct State {
        float value = 0.0f;
    };

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rate);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
        // Spread input for identity-bearing lane_ids from structural upstream
        out.push_back({.name="lane_ids", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        // Signal outputs for test readback
        out.push_back({"lane_count_out",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"lane_index_out",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"lane_id_out",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"state_value_out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        // Per-lane state via vivid_lane_state (NOT member variable)
        State& s = *vivid_lane_state(ctx, ctx->lane_id, State);

        float r = rate.value;
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float target = in[i];
            s.value += (target - s.value) * r;
            out[i] = s.value;
        }

        // Write lane metadata + state to float outputs for test readback
        {
            float vals[4];
            vals[0] = static_cast<float>(ctx->lane_count);
            vals[1] = static_cast<float>(ctx->lane_index);
            vals[2] = static_cast<float>(ctx->lane_id);
            vals[3] = s.value;
            for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
                for (int j = 0; j < 4; ++j)
                    ctx->output_buffers[j][i] = vals[j];
            }
        }
    }
};

