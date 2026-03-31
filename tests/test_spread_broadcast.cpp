// Test: lane-aware spread propagation in the frame executor.
//
// Replaces the old cycle-expand/modulo-wrap broadcast test.
// Verifies that spreads propagate intact through single wires and that
// the scalar input_value is set to spread[0].
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

    std::fprintf(stderr, "\n=== Test: Lane-Aware Spread Propagation ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    // --- Test 1: Single spread propagates intact (no cycle-expand, no modulo) ---
    std::fprintf(stderr, "\n--- single spread propagation ---\n");
    runtime.tick(0.0, 1.0 / 60.0, 0);

    const vivid::CompiledNode* sink = nullptr;
    for (const auto& ns : runtime.compiled_graph()->nodes) {
        if (ns.node_id == "single_sink") { sink = &ns; break; }
    }
    check(sink != nullptr, "found single_sink node");
    if (sink) {
        // SpreadSourceOp with base=2.0, count=4 → spread = [2, 4, 6, 8]
        check(sink->output_spreads.size() > 0, "sink has output_spreads");
        const auto& sp = sink->output_spreads[0];
        check(sp.size() == 4, "spread has 4 elements (no cycle-expand)");
        if (sp.size() >= 4) {
            check_float(sp[0], 2.0f, 0.01f, "spread[0] = 2.0");
            check_float(sp[1], 4.0f, 0.01f, "spread[1] = 4.0");
            check_float(sp[2], 6.0f, 0.01f, "spread[2] = 6.0");
            check_float(sp[3], 8.0f, 0.01f, "spread[3] = 8.0");
        }
    }

    // --- Test 2: Scalar value = spread[0] ---
    std::fprintf(stderr, "\n--- scalar value from spread ---\n");
    if (sink) {
        check_float(sink->output_values[0], 2.0f, 0.01f,
                     "scalar output = spread[0] = 2.0");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
