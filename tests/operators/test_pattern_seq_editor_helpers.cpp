// Pure-logic tests for the PatternSeq editor helpers.

#include "pattern_seq_editor_shared.h"

#include <cmath>
#include <cstdio>

#include "test_helpers.h"

namespace ps = ::vivid::pattern_seq_editor;

int main() {
    std::fprintf(stderr, "=== Test: PatternSeq editor helpers ===\n\n");

    // --- Param name / index encoding ---
    check(ps::param_name_for(0) == "val_0",  "step 0 → val_0");
    check(ps::param_name_for(15) == "val_15", "step 15 → val_15");
    check(ps::param_index_for(0) == 1,       "val_0 at descriptor index 1");
    check(ps::param_index_for(15) == 16,     "val_15 at descriptor index 16");
    check(ps::kRateIndex        == 17,       "rate at 17");
    check(ps::kGateLengthIndex  == 18,       "gate_length at 18");
    check(ps::kProbabilityIndex == 19,       "probability at 19");

    // --- value_from_cell_y: bipolar mapping ---
    {
        const float top_v = ps::value_from_cell_y(0.0f);
        const float mid_v = ps::value_from_cell_y(0.5f);
        const float bot_v = ps::value_from_cell_y(1.0f);
        check(std::abs(top_v - ps::kValueMax) < 1e-3f, "y=0 → kValueMax");
        check(std::abs(mid_v - 0.0f) < 1e-3f,          "y=0.5 → 0");
        check(std::abs(bot_v - ps::kValueMin) < 1e-3f, "y=1 → kValueMin");

        // Off-range clamp.
        check(ps::value_from_cell_y(-0.5f) == ps::kValueMax,
              "negative y clamps to top (kValueMax)");
        check(ps::value_from_cell_y(1.5f) == ps::kValueMin,
              "y > 1 clamps to bottom (kValueMin)");
    }

    // --- clamp_value ---
    check(ps::clamp_value(5.0f) == 5.0f,          "in-range unchanged");
    check(ps::clamp_value(1e9f) == ps::kValueMax, "over-max clamps");
    check(ps::clamp_value(-1e9f) == ps::kValueMin, "under-min clamps");

    // --- fill_ramp_up: first = -max, last = +max ---
    {
        float buf[ps::kMaxSteps] = {};
        ps::fill_ramp_up(buf, 8);
        check(std::abs(buf[0] - ps::kValueMin) < 1e-2f,
              "ramp_up: first = kValueMin");
        check(std::abs(buf[7] - ps::kValueMax) < 1e-2f,
              "ramp_up: last = kValueMax");
        // Monotonic increasing
        bool monotone = true;
        for (int i = 1; i < 8; ++i) if (buf[i] < buf[i-1]) { monotone = false; break; }
        check(monotone, "ramp_up is monotonically increasing");
    }

    // --- fill_ramp_down: first = +max, last = -max ---
    {
        float buf[ps::kMaxSteps] = {};
        ps::fill_ramp_down(buf, 8);
        check(std::abs(buf[0] - ps::kValueMax) < 1e-2f,
              "ramp_down: first = kValueMax");
        check(std::abs(buf[7] - ps::kValueMin) < 1e-2f,
              "ramp_down: last = kValueMin");
    }

    // --- fill_zero ---
    {
        float buf[ps::kMaxSteps] = {1.0f, 2.0f, 3.0f, 4.0f};
        ps::fill_zero(buf, 4);
        for (int i = 0; i < 4; ++i) {
            if (buf[i] != 0.0f) { check(false, "fill_zero yields all zero"); return 1; }
        }
        check(true, "fill_zero yields all zero");
    }

    // --- fill_random: deterministic given a fixed seed ---
    {
        float a[ps::kMaxSteps] = {};
        float b[ps::kMaxSteps] = {};
        ps::fill_random(a, 8, 0x12345678u);
        ps::fill_random(b, 8, 0x12345678u);
        bool match = true;
        for (int i = 0; i < 8; ++i) if (a[i] != b[i]) { match = false; break; }
        check(match, "fill_random is deterministic given the same seed");

        // All values in range
        bool in_range = true;
        for (int i = 0; i < 8; ++i) {
            if (a[i] < ps::kValueMin || a[i] > ps::kValueMax) {
                in_range = false; break;
            }
        }
        check(in_range, "fill_random outputs stay within [kValueMin, kValueMax]");
    }

    // --- Single-step fill edge case: ramp of 1 step is just 0 ---
    {
        float buf[ps::kMaxSteps] = {};
        ps::fill_ramp_up(buf, 1);
        check(buf[0] == 0.0f, "ramp_up of 1 step is 0");
        ps::fill_ramp_down(buf, 1);
        check(buf[0] == 0.0f, "ramp_down of 1 step is 0");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
