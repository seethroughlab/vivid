// Test: scalar-to-lane lifting and lane count normalization.
//
// Phase 1 (scalar lift): a scalar output connected to a VIVID_PORT_LANE_ARRAY
// input becomes a 1-element lane array in the wire propagation.
//
// Phase 2 (normalization): shorter lane arrays on a node are expanded to
// match the longest by repeating their last element.
#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include <cstdio>
#include <cmath>
#include <filesystem>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string graph_path = build_dir + "/test_scalar_lane_broadcast.json";

    // Setup: staging dir with required operators
    std::string staging = build_dir + "/.test_scalar_broadcast_staging";
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* name) {
        std::filesystem::copy_file(build_dir + "/" + name, staging + "/" + name,
            std::filesystem::copy_options::overwrite_existing);
    };
    stage("lane_source_op.dylib");
    stage("scalar_source_op.dylib");
    stage("dual_lane_sink_op.dylib");

    std::fprintf(stderr, "\n=== Test: Scalar-to-Lane Lift + Normalization ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");
    check_graph_clean(runtime.compiled_graph(), "scalar lane broadcast");

    runtime.tick(0.0, 1.0 / 60.0, 0);

    // --- Test 1: Lane array input passes through unchanged ---
    std::fprintf(stderr, "\n--- lane array passes through ---\n");
    const vivid::CompiledNode* sink = nullptr;
    for (const auto& ns : runtime.compiled_graph()->nodes) {
        if (ns.node_id == "sink") { sink = &ns; break; }
    }
    check(sink != nullptr, "found sink node");
    if (sink) {
        check(sink->output_lanes.size() > 0, "sink has output_lanes");
        const auto& sp_a = sink->output_lanes[0];
        check(sp_a.size() == 4, "out_a lane array has 4 elements");
        if (sp_a.size() >= 4) {
            check_float(sp_a[0], 1.0f, 0.01f, "out_a[0] = 1.0");
            check_float(sp_a[1], 2.0f, 0.01f, "out_a[1] = 2.0");
            check_float(sp_a[2], 3.0f, 0.01f, "out_a[2] = 3.0");
            check_float(sp_a[3], 4.0f, 0.01f, "out_a[3] = 4.0");
        }
    }

    // --- Test 2: Scalar normalized to match lane count ---
    std::fprintf(stderr, "\n--- scalar broadcast to match lane count ---\n");
    if (sink) {
        check(sink->output_lanes.size() > 1, "sink has second output_lanes");
        const auto& sp_b = sink->output_lanes[1];
        check(sp_b.size() == 4, "out_b has 4 elements (normalized)");
        if (sp_b.size() >= 4) {
            check_float(sp_b[0], 5.0f, 0.01f, "out_b[0] = 5.0");
            check_float(sp_b[1], 5.0f, 0.01f, "out_b[1] = 5.0");
            check_float(sp_b[2], 5.0f, 0.01f, "out_b[2] = 5.0");
            check_float(sp_b[3], 5.0f, 0.01f, "out_b[3] = 5.0");
        }
    }

    // --- Test 3: Scalar values preserved ---
    std::fprintf(stderr, "\n--- scalar values preserved ---\n");
    if (sink) {
        check_float(sink->output_values[0], 1.0f, 0.01f,
                     "scalar out_a = lane_array[0] = 1.0");
        check_float(sink->output_values[1], 5.0f, 0.01f,
                     "scalar out_b = 5.0");
    }

    // --- Test 4: Scalar-only lift (no lane source on node) ---
    // Both inputs are scalar → each becomes a 1-element lane array.
    std::fprintf(stderr, "\n--- scalar-only lift ---\n");
    const vivid::CompiledNode* so_sink = nullptr;
    for (const auto& ns : runtime.compiled_graph()->nodes) {
        if (ns.node_id == "scalar_only_sink") { so_sink = &ns; break; }
    }
    check(so_sink != nullptr, "found scalar_only_sink node");
    if (so_sink) {
        check(so_sink->output_lanes.size() > 1, "scalar_only_sink has output_lanes");
        const auto& sp_a = so_sink->output_lanes[0];
        const auto& sp_b = so_sink->output_lanes[1];
        check(sp_a.size() == 1, "out_a is 1-element lane array");
        check(sp_b.size() == 1, "out_b is 1-element lane array");
        if (sp_a.size() >= 1)
            check_float(sp_a[0], 3.0f, 0.01f, "out_a[0] = 3.0 (lifted scalar)");
        if (sp_b.size() >= 1)
            check_float(sp_b[0], 7.0f, 0.01f, "out_b[0] = 7.0 (lifted scalar)");

        check_float(so_sink->output_values[0], 3.0f, 0.01f, "scalar out_a = 3.0");
        check_float(so_sink->output_values[1], 7.0f, 0.01f, "scalar out_b = 7.0");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
