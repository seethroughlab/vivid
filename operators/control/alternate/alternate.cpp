#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

static constexpr int kMaxInputs = 16;

/**
 * @brief Cycles through up to 16 input lane arrays based on beat phase.
 *
 * Selects one of the connected lane-array inputs in round-robin order,
 * advancing on each beat wrap. Use the cycle parameter to set how many
 * beats before advancing. Uses repeat-group ports for grow-on-connect
 * UI behavior.
 *
 * @param cycle Number of beats per input selection (Beat, 2 Beats, Bar, etc.).
 * @see Stack, PatTransform
 */
struct Alternate : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Alternate";
    static constexpr bool kTimeDependent = true;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE;

    vivid::Param<int> cycle {"cycle", 2, {"Beat","2 Beats","Bar","2 Bars","4 Bars"}};
    vivid::Param<int> clock_mode {"clock_mode", vivid::kClockModeSyncedMetronome, vivid::clock_mode_synced_labels()};

    Alternate() {
        vivid::description(cycle, "How many beats before advancing to the next input");
        vivid::description(clock_mode, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&cycle);
        out.push_back(&clock_mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "beat_phase"});
        out.push_back({.name="input_0",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=0,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_1",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=1,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_2",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=2,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_3",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=3,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_4",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=4,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_5",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=5,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_6",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=6,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_7",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=7,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_8",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=8,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_9",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=9,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_10", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=10, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_11", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=11, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_12", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=12, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_13", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=13, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_14", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=14, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_15", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_SIGNAL, .repeat_group="input", .repeat_group_idx=15, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="output", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({"index",  VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float beat_phase = vivid::resolve_clock_phase(
            clock_mode.int_value(), ctx->input_values[0], vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->param_values, ctx->values, ctx->value_outputs, ctx->output_values);
    }

private:
    void compute(float beat_phase, const float* params, const VividValueView* in_values,
                 VividValueOutput* out_values, float* output_values) {
        int c = std::clamp(static_cast<int>(params[0]), 0, 4);

        // Cycle lengths in beats: Beat=1, 2 Beats=2, Bar=4, 2 Bars=8, 4 Bars=16
        static constexpr int kCycleBeats[] = {1, 2, 4, 8, 16};
        int cycle_beats = kCycleBeats[c];

        // Beat tracking
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) beat_count_++;
        prev_phase_ = beat_phase;

        if (!in_values || !out_values) return;

        // Collect connected (non-empty) value inputs
        // Value inputs are at port indices 1..kMaxInputs (port 0 is beat_phase scalar)
        const VividValueView* inputs[kMaxInputs];
        int input_indices[kMaxInputs];
        int input_count = 0;
        for (int i = 0; i < kMaxInputs; ++i) {
            if (vivid_value_count(&in_values[1 + i]) > 0) {
                inputs[input_count] = &in_values[1 + i];
                input_indices[input_count] = i;
                input_count++;
            }
        }

        auto& out = out_values[0];

        if (input_count == 0) {
            vivid_value_output_commit(&out, 0);
            output_values[0] = 0.0f;
            return;
        }

        // Select active input based on beat count and cycle
        int active = (beat_count_ / cycle_beats) % input_count;
        active = std::clamp(active, 0, input_count - 1);

        // Pass through selected input
        const VividValueView* sel = inputs[active];
        uint32_t len = vivid_value_count(sel);
        const float* sel_data = vivid_value_floats(sel);
        float* buf = vivid_value_output_floats(&out, len);
        if (buf && sel_data) {
            for (uint32_t i = 0; i < len; ++i)
                buf[i] = sel_data[i];
            vivid_value_output_commit(&out, len);
        }

        // Output the logical index (which input is active)
        output_values[0] = static_cast<float>(input_indices[active]);
    }

    float prev_phase_ = 0.0f;
    int beat_count_ = 0;
};

VIVID_DEFINE_OP(Alternate) {
}

