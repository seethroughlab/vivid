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
    // EnvelopeFollower → SmoothFr (the alias added for compelling-AV-demos)
    // -----------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n=== EnvelopeFollower → SmoothFr ===\n");
        std::unordered_map<std::string, float> params{
            {"rise_time", 0.005f},
            {"fall_time", 0.4f},
        };
        std::unordered_map<std::string, std::string> string_params;
        auto resolved = vivid::resolve_operator_alias("EnvelopeFollower", params, string_params);
        check(resolved == "SmoothFr",
              "EnvelopeFollower resolves to SmoothFr");
        // Pure rename should preserve params unchanged (Smooth uses the same names)
        check(params.size() == 2, "params count preserved");
        check(params["rise_time"] == 0.005f, "rise_time preserved");
        check(params["fall_time"] == 0.4f, "fall_time preserved");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
