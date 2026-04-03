// Test: lane-aware lane-array propagation in the frame executor.
//
// Replaces the old cycle-expand/modulo-wrap broadcast test.
// Verifies that lane arrays propagate intact through single wires and that
// the scalar input_value is set to lane_array[0].
#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
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

    std::string graph_path = build_dir + "/test_lane_broadcast.json";

    // Setup: staging dir with required operators
    std::string staging = build_dir + "/.test_broadcast_staging";
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* name) {
        std::filesystem::copy_file(build_dir + "/" + name, staging + "/" + name,
            std::filesystem::copy_options::overwrite_existing);
    };
    stage("lane_source_op.dylib");
    stage("lane_smooth_op.dylib");
    std::filesystem::copy_file(build_dir + "/lane_sink_op.dylib",
        staging + "/lane_sink_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Lane-Aware Lane Propagation ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    // --- Test 1: Single lane array propagates intact (no cycle-expand, no modulo) ---
    std::fprintf(stderr, "\n--- single lane-array propagation ---\n");
    runtime.tick(0.0, 1.0 / 60.0, 0);

    const vivid::CompiledNode* sink = nullptr;
    for (const auto& ns : runtime.compiled_graph()->nodes) {
        if (ns.node_id == "single_sink") { sink = &ns; break; }
    }
    check(sink != nullptr, "found single_sink node");
    if (sink) {
        // LaneSourceOp with base=2.0, count=4 → lane array = [2, 4, 6, 8]
        check(sink->output_lanes.size() > 0, "sink has output_lanes");
        const auto& sp = sink->output_lanes[0];
        check(sp.size() == 4, "lane array has 4 elements (no cycle-expand)");
        if (sp.size() >= 4) {
            check_float(sp[0], 2.0f, 0.01f, "lane_array[0] = 2.0");
            check_float(sp[1], 4.0f, 0.01f, "lane_array[1] = 4.0");
            check_float(sp[2], 6.0f, 0.01f, "lane_array[2] = 6.0");
            check_float(sp[3], 8.0f, 0.01f, "lane_array[3] = 8.0");
        }
    }

    // --- Test 2: Scalar value = lane_array[0] ---
    std::fprintf(stderr, "\n--- scalar value from lane array ---\n");
    if (sink) {
        check_float(sink->output_values[0], 2.0f, 0.01f,
                     "scalar output = lane_array[0] = 2.0");
    }

    // --- Test 3: Same-provenance same-length merge (element-wise add) ---
    // merge_sink receives two wires from the same single_src/out.
    // Both edges carry the same lane_set_id (same Structural source).
    // The executor should add them element-wise:
    //   [2, 4, 6, 8] + [2, 4, 6, 8] = [4, 8, 12, 16]
    std::fprintf(stderr, "\n--- same-provenance merge (element-wise add) ---\n");
    const vivid::CompiledNode* merge_sink = nullptr;
    for (const auto& ns : runtime.compiled_graph()->nodes) {
        if (ns.node_id == "merge_sink") { merge_sink = &ns; break; }
    }
    check(merge_sink != nullptr, "found merge_sink node");
    if (merge_sink) {
        check(merge_sink->output_lanes.size() > 0, "merge_sink has output_lanes");
        const auto& sp = merge_sink->output_lanes[0];
        check(sp.size() == 4, "merged lane array has 4 elements (same length, no cycle-expand)");
        if (sp.size() >= 4) {
            check_float(sp[0],  4.0f, 0.01f, "merge[0] = 2+2 = 4");
            check_float(sp[1],  8.0f, 0.01f, "merge[1] = 4+4 = 8");
            check_float(sp[2], 12.0f, 0.01f, "merge[2] = 6+6 = 12");
            check_float(sp[3], 16.0f, 0.01f, "merge[3] = 8+8 = 16");
        }
        // Scalar should be lane_array[0] of the merged result
        check_float(merge_sink->output_values[0], 4.0f, 0.01f,
                     "merge scalar = lane_array[0] = 4.0");
    }

    // --- Test 4: Kernel operator receives full lane-set data ---
    // LaneSmoothOp (KERNEL) reads all lanes from LaneSourceOp [2,4,6,8]
    // and writes 3-element moving average: [2.67, 4.0, 6.0, 7.33]
    std::fprintf(stderr, "\n--- kernel operator (cross-lane smoothing) ---\n");
    const vivid::CompiledNode* smooth_sink = nullptr;
    for (const auto& ns : runtime.compiled_graph()->nodes) {
        if (ns.node_id == "smooth_sink") { smooth_sink = &ns; break; }
    }
    check(smooth_sink != nullptr, "found smooth_sink node");
    if (smooth_sink) {
        check(smooth_sink->output_lanes.size() > 0, "smooth_sink has output_lanes");
        const auto& sp = smooth_sink->output_lanes[0];
        check(sp.size() == 4, "smoothed lane array has 4 elements");
        if (sp.size() >= 4) {
            // [2,4,6,8] → avg neighbors:
            // [0]: (2+2+4)/3 = 2.667
            // [1]: (2+4+6)/3 = 4.0
            // [2]: (4+6+8)/3 = 6.0
            // [3]: (6+8+8)/3 = 7.333
            check_float(sp[0], 2.667f, 0.01f, "smooth[0] = avg(2,2,4) = 2.667");
            check_float(sp[1], 4.0f,   0.01f, "smooth[1] = avg(2,4,6) = 4.0");
            check_float(sp[2], 6.0f,   0.01f, "smooth[2] = avg(4,6,8) = 6.0");
            check_float(sp[3], 7.333f, 0.01f, "smooth[3] = avg(6,8,8) = 7.333");
        }
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
