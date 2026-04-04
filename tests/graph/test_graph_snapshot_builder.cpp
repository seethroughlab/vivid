// Tests for graph snapshot builder: verifies snapshot construction from
// compiled graph state.
#include "runtime/graph/graph_snapshot_builder.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_info_cache.h"
#include <cstdio>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    std::fprintf(stderr, "=== test_graph_snapshot_builder ===\n");

    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    // --- Setup: load a simple graph with test operators ---
    std::string staging = build_dir + "/.test_snapshot_staging";
    std::filesystem::create_directories(staging);

    // Copy test operator plugin
    std::string plugin_name = "test_op_v1";
#if defined(__APPLE__)
    std::string suffix = ".dylib";
#elif defined(_WIN32)
    std::string suffix = ".dll";
#else
    std::string suffix = ".so";
#endif
    std::string src_path = build_dir + "/" + plugin_name + suffix;
    std::string dst_path = staging + "/" + plugin_name + suffix;
    if (std::filesystem::exists(src_path)) {
        std::filesystem::copy_file(src_path, dst_path,
            std::filesystem::copy_options::overwrite_existing);
    }

    vivid::OperatorRegistry registry;
    registry.scan(staging.c_str());

    // Create a graph with one node
    vivid::Graph graph;
    graph.add_node("n1", "TestOpV1");

    vivid::RuntimeCore runtime;
    bool built = runtime.build(graph, registry);
    if (!built) {
        std::fprintf(stderr, "  SKIP: runtime.build() failed (operator not available)\n");
        std::filesystem::remove_all(staging);
        return 0;
    }

    OperatorInfoCache op_cache;

    // --- Test: build snapshot from running graph ---
    std::fprintf(stderr, "\n--- snapshot from single-node graph ---\n");

    auto snap = vivid::build_graph_snapshot(
        graph, runtime, nullptr, registry, op_cache);

    check(!snap.nodes.empty(), "snapshot has nodes");
    check(snap.nodes.size() == 1, "snapshot has exactly 1 node");
    if (!snap.nodes.empty()) {
        check(snap.nodes[0].node_id == "n1", "node_id matches");
        check(!snap.nodes[0].type_name.empty(), "type_name populated");
    }

    // --- Test: empty graph produces empty snapshot ---
    std::fprintf(stderr, "\n--- empty graph snapshot ---\n");

    vivid::Graph empty_graph;
    vivid::RuntimeCore empty_runtime;
    empty_runtime.build(empty_graph, registry);

    auto empty_snap = vivid::build_graph_snapshot(
        empty_graph, empty_runtime, nullptr, registry, op_cache);

    check(empty_snap.nodes.empty(), "empty graph produces empty snapshot");
    check(empty_snap.connections.empty(), "empty graph has no connections");

    // --- Cleanup ---
    runtime.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
