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
    vivid::Param<int> clock_source {"clock_source", vivid::kClockSourceExternal, vivid::clock_source_labels()};

    char port_names_[kMaxInputs][16];

    Alternate() {
        vivid::description(cycle, "How many beats before advancing to the next input");
        vivid::description(clock_source, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
        for (int i = 0; i < kMaxInputs; ++i)
            std::snprintf(port_names_[i], sizeof(port_names_[i]), "input_%d", i);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&cycle);         // 0
        out.push_back(&clock_source);  // 1
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // beat_phase is a standalone scalar input (not part of the repeat group)
        VividPortDescriptor bp{};
        bp.name = "beat_phase";
        bp.type = VIVID_PORT_SCALAR;
        bp.direction = VIVID_PORT_INPUT;
        out.push_back(bp);

        // Repeating lane-array inputs
        for (int i = 0; i < kMaxInputs; ++i) {
            VividPortDescriptor pd{};
            pd.name = port_names_[i];
            pd.type = VIVID_PORT_LANE_ARRAY;
            pd.direction = VIVID_PORT_INPUT;
            pd.repeat_group = "input";
            pd.repeat_group_idx = static_cast<uint16_t>(i);
            out.push_back(pd);
        }

        VividPortDescriptor out_port{};
        out_port.name = "output";
        out_port.type = VIVID_PORT_LANE_ARRAY;
        out_port.direction = VIVID_PORT_OUTPUT;
        out.push_back(out_port);

        VividPortDescriptor idx_port{};
        idx_port.name = "index";
        idx_port.type = VIVID_PORT_SCALAR;
        idx_port.direction = VIVID_PORT_OUTPUT;
        out.push_back(idx_port);
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

VIVID_REGISTER(Alternate)
