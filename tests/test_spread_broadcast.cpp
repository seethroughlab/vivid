// Test: spread broadcast/wrap semantics.
// When two spreads of different lengths feed the same input, the shorter
// wraps to match the longer and values are additively mixed.
#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
#include "runtime/compiled_graph.h"
#include <cstdio>
#include <cmath>
#include <filesystem>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%f)\n", msg, actual);
    }
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string graph_path = build_dir + "/test_spread_broadcast.json";

    // Setup: staging dir with required operators
    std::string staging = build_dir + "/.test_broadcast_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/spread_source_op.dylib",
        staging + "/spread_source_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/spread_sink_op.dylib",
        staging + "/spread_sink_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Spread Broadcast ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    // --- Test 1: Single source spread passthrough ---
    std::fprintf(stderr, "\n--- single source spread ---\n");
    runtime.tick(0.0, 1.0 / 60.0, 0);

    // Find single_sink node
    const vivid::CompiledNode* single_sink = nullptr;
    for (const auto& ns : runtime.compiled_graph()->nodes) {
        if (ns.node_id == "single_sink") { single_sink = &ns; break; }
    }
    check(single_sink != nullptr, "found single_sink node");
    if (single_sink) {
        // single_src: base=2.0, count=4 → spread = [2, 4, 6, 8]
        check(single_sink->output_spreads.size() > 0, "single_sink has output_spreads");
        const auto& sp = single_sink->output_spreads[0];
        check(sp.size() == 4, "single spread has 4 elements");
        if (sp.size() >= 4) {
            check_float(sp[0], 2.0f, 0.01f, "single spread[0] = 2.0");
            check_float(sp[1], 4.0f, 0.01f, "single spread[1] = 4.0");
            check_float(sp[2], 6.0f, 0.01f, "single spread[2] = 6.0");
            check_float(sp[3], 8.0f, 0.01f, "single spread[3] = 8.0");
        }
    }

    // --- Test 2: Broadcast (two different-length spreads) ---
    std::fprintf(stderr, "\n--- broadcast two spreads ---\n");
    const vivid::CompiledNode* sink = nullptr;
    for (const auto& ns : runtime.compiled_graph()->nodes) {
        if (ns.node_id == "sink") { sink = &ns; break; }
    }
    check(sink != nullptr, "found sink node");
    if (sink) {
        const auto& sp = sink->output_spreads[0];
        // src3: base=1, count=3 → spread = [1, 2, 3]
        // src5: base=10, count=5 → spread = [10, 20, 30, 40, 50]
        // Broadcasting: shorter wraps → [1, 2, 3, 1, 2] (modulo indexing)
        // Additive: [1+10, 2+20, 3+30, 1+40, 2+50] = [11, 22, 33, 41, 52]
        std::fprintf(stderr, "  INFO: broadcast result has %zu elements\n", sp.size());
        check(sp.size() == 5, "broadcast result has 5 elements (max of 3 and 5)");
        if (sp.size() >= 5) {
            check_float(sp[0], 11.0f, 0.01f, "broadcast[0] = 1+10 = 11");
            check_float(sp[1], 22.0f, 0.01f, "broadcast[1] = 2+20 = 22");
            check_float(sp[2], 33.0f, 0.01f, "broadcast[2] = 3+30 = 33");
            check_float(sp[3], 41.0f, 0.01f, "broadcast[3] = 1+40 = 41");
            check_float(sp[4], 52.0f, 0.01f, "broadcast[4] = 2+50 = 52");
        }
    }

    // --- Test 3: Verify scalar value is spread[0] ---
    std::fprintf(stderr, "\n--- scalar value from spread ---\n");
    if (sink) {
        check_float(sink->output_values[0], 11.0f, 0.01f, "sink scalar output = spread[0] = 11");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
