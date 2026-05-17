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
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    vivid::Param<int> cycle {"cycle", 2, {"Beat","2 Beats","Bar","2 Bars","4 Bars"}};
    vivid::Param<int> clock_source {"clock_source", vivid::kClockSourceMetronome, vivid::clock_source_labels()};

    Alternate() {
        vivid::description(cycle, "How many beats before advancing to the next input");
        vivid::description(clock_source, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&cycle);
        out.push_back(&clock_source);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"input_0",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  0});
        out.push_back({"input_1",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  1});
        out.push_back({"input_2",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  2});
        out.push_back({"input_3",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  3});
        out.push_back({"input_4",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  4});
        out.push_back({"input_5",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  5});
        out.push_back({"input_6",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  6});
        out.push_back({"input_7",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  7});
        out.push_back({"input_8",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  8});
        out.push_back({"input_9",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  9});
        out.push_back({"input_10", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 10});
        out.push_back({"input_11", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 11});
        out.push_back({"input_12", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 12});
        out.push_back({"input_13", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 13});
        out.push_back({"input_14", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 14});
        out.push_back({"input_15", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 15});
        out.push_back({"output", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        out.push_back({"index",  VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), ctx->input_values[0], vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->param_values, ctx->input_lanes, ctx->output_lanes, ctx->output_values);
    }

private:
    void compute(float beat_phase, const float* params, const VividLaneView* in_lanes,
                 VividLaneOutput* out_lanes, float* output_values) {
        int c = std::clamp(static_cast<int>(params[0]), 0, 4);

        // Cycle lengths in beats: Beat=1, 2 Beats=2, Bar=4, 2 Bars=8, 4 Bars=16
        static constexpr int kCycleBeats[] = {1, 2, 4, 8, 16};
        int cycle_beats = kCycleBeats[c];

        // Beat tracking
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) beat_count_++;
        prev_phase_ = beat_phase;

        if (!in_lanes || !out_lanes) return;

        // Collect connected (non-empty) lane inputs
        // Lane inputs are at port indices 1..kMaxInputs (port 0 is beat_phase scalar)
        const VividLaneView* inputs[kMaxInputs];
        int input_indices[kMaxInputs];
        int input_count = 0;
        for (int i = 0; i < kMaxInputs; ++i) {
            if (in_lanes[1 + i].length > 0) {
                inputs[input_count] = &in_lanes[1 + i];
                input_indices[input_count] = i;
                input_count++;
            }
        }

        auto& out = out_lanes[0];

        if (input_count == 0) {
            out.commit(out.handle, 0);
            output_values[0] = 0.0f;
            return;
        }

        // Select active input based on beat count and cycle
        int active = (beat_count_ / cycle_beats) % input_count;
        active = std::clamp(active, 0, input_count - 1);

        // Pass through selected input
        auto& sel = *inputs[active];
        uint32_t len = sel.length;
        float* buf = out.resize(out.handle, len);
        if (buf) {
            for (uint32_t i = 0; i < len; ++i)
                buf[i] = sel.data[i];
            out.commit(out.handle, len);
        }

        // Output the logical index (which input is active)
        output_values[0] = static_cast<float>(input_indices[active]);
    }

    float prev_phase_ = 0.0f;
    int beat_count_ = 0;
};

VIVID_DEFINE_OP(Alternate) {
}

