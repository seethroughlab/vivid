#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

struct Alternate : vivid::OperatorBase {
    static constexpr const char* kName   = "Alternate";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<int> cycle {"cycle", 2, {"Beat","2 Beats","Bar","2 Bars","4 Bars"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&cycle);  // 0
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_INPUT});   // in float[0]
        out.push_back({"a",          VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // in spread[0]
        out.push_back({"b",          VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // in spread[1]
        out.push_back({"c",          VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // in spread[2]
        out.push_back({"d",          VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // in spread[3]
        out.push_back({"output",     VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});  // out spread[0]
        out.push_back({"index",      VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});  // out float[0]
    }

    void process(VividProcessContext* ctx) override {
        float beat_phase = ctx->input_values[0];
        int c = std::clamp(static_cast<int>(ctx->param_values[0]), 0, 4);

        // Cycle lengths in beats: Beat=1, 2 Beats=2, Bar=4, 2 Bars=8, 4 Bars=16
        static constexpr int kCycleBeats[] = {1, 2, 4, 8, 16};
        int cycle_beats = kCycleBeats[c];

        // Beat tracking
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) beat_count_++;
        prev_phase_ = beat_phase;

        if (!ctx->input_spreads || !ctx->output_spreads) return;

        // Collect connected (non-empty) spread inputs
        // Spread inputs are at port indices 1..4 (a,b,c,d); port 0 is beat_phase (float)
        const VividSpreadPort* inputs[4];
        int input_indices[4];
        int input_count = 0;
        for (int i = 0; i < 4; ++i) {
            if (ctx->input_spreads[1 + i].length > 0) {
                inputs[input_count] = &ctx->input_spreads[1 + i];
                input_indices[input_count] = i;
                input_count++;
            }
        }

        auto& out = ctx->output_spreads[0];

        if (input_count == 0) {
            out.length = 0;
            ctx->output_values[0] = 0.0f;
            return;
        }

        // Select active input based on beat count and cycle
        int active = (beat_count_ / cycle_beats) % input_count;
        active = std::clamp(active, 0, input_count - 1);

        // Pass through selected input
        auto& sel = *inputs[active];
        uint32_t len = std::min(sel.length, out.capacity);
        out.length = len;
        for (uint32_t i = 0; i < len; ++i)
            out.data[i] = sel.data[i];

        // Output the logical index (which of a,b,c,d is active)
        ctx->output_values[0] = static_cast<float>(input_indices[active]);
    }

private:
    float prev_phase_ = 0.0f;
    int beat_count_ = 0;
};

VIVID_REGISTER(Alternate)
