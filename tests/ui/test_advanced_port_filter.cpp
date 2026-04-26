// Advanced-port affordance tests.
//
// Verifies that:
//   1. VividPortDescriptor.display_hint round-trips through the operator
//      loader / descriptor pipeline.
//   2. GraphCompiler populates CompiledNode.advanced_output_port_indices
//      for every output port tagged VIVID_PORT_DISPLAY_ADVANCED. Downstream,
//      GraphSnapshotBuilder copies that field verbatim into NodeSnapshot,
//      so the inspector can hide advanced ports on the node body until a
//      connection lands on them (mirrors analysis-port behavior).
//
// Uses the test_op_advanced_port fixture operator (one default output named
// "primary", one advanced output named "advanced").

#include "operator_api/types.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/graph_compiler.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_loader.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include "test_helpers.h"

static void test_descriptor_round_trips_display_hint(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- descriptor round-trips display_hint ---\n");
    const std::string dylib_path = build_dir + "/test_op_advanced_port.dylib";
    if (!std::filesystem::exists(dylib_path)) {
        std::fprintf(stderr, "FATAL: %s not found\n", dylib_path.c_str());
        ++failures;
        return;
    }

    vivid::OperatorLoader loader;
    check(loader.load(dylib_path.c_str()), "fixture dylib loads");
    if (!loader.is_loaded()) return;

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor not null");
    if (!desc) return;

    int primary_idx = -1, advanced_idx = -1;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (std::string(desc->ports[i].name) == "primary")  primary_idx  = static_cast<int>(i);
        if (std::string(desc->ports[i].name) == "advanced") advanced_idx = static_cast<int>(i);
    }
    check(primary_idx >= 0,  "primary port present");
    check(advanced_idx >= 0, "advanced port present");

    if (primary_idx >= 0)
        check(desc->ports[primary_idx].display_hint == VIVID_PORT_DISPLAY_DEFAULT,
              "primary port has DEFAULT display_hint");
    if (advanced_idx >= 0)
        check(desc->ports[advanced_idx].display_hint == VIVID_PORT_DISPLAY_ADVANCED,
              "advanced port has ADVANCED display_hint");
}

static void test_compiler_collects_advanced_port_index(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- graph compiler populates advanced_output_port_indices ---\n");

    const std::string staging = build_dir + "/.test_advanced_port_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    auto stage = [&](const char* name) {
        std::string src = build_dir + "/" + name;
        std::string dst = staging + "/" + name;
        if (std::filesystem::exists(src))
            std::filesystem::copy_file(src, dst,
                std::filesystem::copy_options::overwrite_existing);
    };
    stage("test_op_advanced_port.dylib");

    vivid::OperatorRegistry registry;
    registry.scan_deferred(staging.c_str());

    vivid::Graph g;
    g.add_node("op", "TestOpAdvancedPort");

    vivid::GraphCompiler::Options opts;
    auto cg = vivid::GraphCompiler::compile(g, registry, opts);
    check(cg != nullptr, "graph compiles");
    if (!cg) return;

    auto* node = cg->find_node("op");
    check(node != nullptr, "compiled node found");
    if (!node) return;

    check(node->output_port_indices.count("primary") == 1,
          "primary port in output_port_indices");
    check(node->output_port_indices.count("advanced") == 1,
          "advanced port in output_port_indices");
    check(node->advanced_output_port_indices.count("primary") == 0,
          "primary port NOT in advanced_output_port_indices");
    check(node->advanced_output_port_indices.count("advanced") == 1,
          "advanced port in advanced_output_port_indices");

    std::filesystem::remove_all(staging);
}

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";

    test_descriptor_round_trips_display_hint(build_dir);
    test_compiler_collects_advanced_port_index(build_dir);

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
