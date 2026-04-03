// Test: VividFrameContext lane metadata population.
//
// Verifies that the frame executor populates lane_count, lane_index,
// and lane_set_id on VividFrameContext before calling process_frame().
// Uses LaneMetadataOp which copies these fields to scalar outputs.
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

    std::string graph_path = build_dir + "/test_lane_metadata.json";

    // Stage operators
    std::string staging = build_dir + "/.test_lane_metadata_staging";
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst,
                std::filesystem::copy_options::overwrite_existing);
    };
    stage("lane_source_op.dylib");
    stage("lane_metadata_op.dylib");

    std::fprintf(stderr, "\n=== Test: Lane Metadata ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    runtime.tick(0.0, 1.0 / 60.0, 0);

    // --- Test 1: Lane-fed operator sees lane metadata ---
    std::fprintf(stderr, "\n--- lane-fed lane metadata ---\n");
    const vivid::CompiledNode* meta = nullptr;
    for (const auto& ns : runtime.compiled_graph()->nodes) {
        if (ns.node_id == "meta") { meta = &ns; break; }
    }
    check(meta != nullptr, "found meta node");
    if (meta) {
        // LaneSourceOp emits count=8 lane array. LaneMetadataOp should see:
        // lane_count = 8 (runtime materialized lane length)
        // lane_index = 0 (always 0 in Phase 3)
        // lane_set_id != 0 (LaneSourceOp is Structural → nonzero provenance)
        check_float(meta->output_values[0], 8.0f, 0.01f,
                     "lane_count = 8 (lane length)");
        check_float(meta->output_values[1], 0.0f, 0.01f,
                     "lane_index = 0 (no per-lane lifting)");
        check(meta->output_values[2] != 0.0f,
              "lane_set_id != 0 (structural upstream provenance)");
    }

    // --- Test 2: Scalar-only operator sees scalar lane metadata ---
    std::fprintf(stderr, "\n--- scalar-only lane metadata ---\n");
    const vivid::CompiledNode* meta_scalar = nullptr;
    for (const auto& ns : runtime.compiled_graph()->nodes) {
        if (ns.node_id == "meta_scalar") { meta_scalar = &ns; break; }
    }
    check(meta_scalar != nullptr, "found meta_scalar node");
    if (meta_scalar) {
        // No lane input → lane_count = 1, lane_set_id = 0
        check_float(meta_scalar->output_values[0], 1.0f, 0.01f,
                     "lane_count = 1 (scalar)");
        check_float(meta_scalar->output_values[1], 0.0f, 0.01f,
                     "lane_index = 0");
        check_float(meta_scalar->output_values[2], 0.0f, 0.01f,
                     "lane_set_id = 0 (no upstream provenance)");
    }

    std::filesystem::remove_all(staging);
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
