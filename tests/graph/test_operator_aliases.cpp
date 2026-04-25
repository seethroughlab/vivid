#include "runtime/graph/operator_aliases.h"
#include "test_helpers.h"
#include <cstdio>
#include <string>
#include <unordered_map>

int main() {
    // -----------------------------------------------------------------------
    // Unknown type passes through unchanged
    // -----------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n=== Unknown type passes through ===\n");
        std::unordered_map<std::string, float> params;
        std::unordered_map<std::string, std::string> string_params;
        auto resolved = vivid::resolve_operator_alias("NoSuchOperator", params, string_params);
        check(resolved == "NoSuchOperator", "unknown type returned unchanged");
        check(params.empty(), "params untouched on no-alias path");
    }

    // -----------------------------------------------------------------------
    // EnvelopeFollower → Smooth (alias from compelling-AV-demos work, retargeted
    // to the unsuffixed canonical name after the Fr-variant retirement).
    // -----------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n=== EnvelopeFollower → Smooth ===\n");
        std::unordered_map<std::string, float> params{
            {"rise_time", 0.005f},
            {"fall_time", 0.4f},
        };
        std::unordered_map<std::string, std::string> string_params;
        auto resolved = vivid::resolve_operator_alias("EnvelopeFollower", params, string_params);
        check(resolved == "Smooth",
              "EnvelopeFollower resolves to Smooth");
        // Pure rename should preserve params unchanged (Smooth uses the same names)
        check(params.size() == 2, "params count preserved");
        check(params["rise_time"] == 0.005f, "rise_time preserved");
        check(params["fall_time"] == 0.4f, "fall_time preserved");
    }

    // -----------------------------------------------------------------------
    // *Fr variants → unsuffixed canonical (Phase-1 of operator-naming
    // consolidation). Old graphs saved with frame-rate type names must keep
    // loading after the dual-cadence operators were retired in favour of the
    // single audio-rate variant.
    // -----------------------------------------------------------------------
    {
        struct Case { const char* legacy; const char* canonical; };
        const Case cases[] = {
            {"LfoFr",         "Lfo"},
            {"ClockFr",       "Clock"},
            {"EnvelopeFr",    "Envelope"},
            {"SmoothFr",      "Smooth"},
            {"StepCounterFr", "StepCounter"},
        };
        for (const auto& c : cases) {
            std::fprintf(stderr, "\n=== %s → %s ===\n", c.legacy, c.canonical);
            std::unordered_map<std::string, float> params{{"frequency", 2.0f}};
            std::unordered_map<std::string, std::string> string_params;
            auto resolved = vivid::resolve_operator_alias(c.legacy, params, string_params);
            check(resolved == c.canonical,
                  (std::string(c.legacy) + " resolves to " + c.canonical).c_str());
            // Pure rename — params should be untouched.
            check(params.size() == 1, "params count preserved");
            check(params["frequency"] == 2.0f, "param value preserved");
        }
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
