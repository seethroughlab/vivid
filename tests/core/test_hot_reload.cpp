#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <string>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    // Usage: test_hot_reload <build_dir>
    // Expects: <build_dir>/test_op_v1.dylib, test_op_v2.dylib, test_reload.json
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string v1_path = build_dir + "/test_op_v1.dylib";
    std::string v2_path = build_dir + "/test_op_v2.dylib";
    std::string bad_port_path = build_dir + "/test_op_incompatible_port.dylib";
    std::string graph_path = build_dir + "/test_reload.json";

    // --- Step 1: Create staging directory with v1 ---
    std::string staging = build_dir + "/.test_staging";
    std::filesystem::create_directories(staging);

    // Copy v1 as "test_op_v1.dylib" into staging dir
    // Registry needs it as just the dylib name; we scan the staging dir
    std::string staged_v1 = staging + "/test_op_v1.dylib";
    std::filesystem::copy_file(v1_path, staged_v1,
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Hot-Reload Param Reconciliation ===\n\n");

    // --- Step 2: Load v1 via registry ---
    std::fprintf(stderr, "--- Loading v1 ---\n");
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("TestOp") != nullptr, "TestOp found in registry");

    // Verify target→type mapping (dylib stem "test_op_v1" → "TestOp")
    const std::string* type_ptr = registry.type_name_for_target("test_op_v1");
    check(type_ptr != nullptr, "target_to_type mapping exists for test_op_v1");
    if (type_ptr) {
        check(*type_ptr == "TestOp", "target maps to TestOp");
    }

    // --- Step 3: Build graph and runtime ---
    std::fprintf(stderr, "\n--- Building graph ---\n");
    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load() succeeds");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build() succeeds");
    check(runtime.compiled_graph()->nodes.size() == 1, "runtime has 1 node");

    // Verify initial param: scale=5.0 (from JSON)
    const auto& nodes = runtime.compiled_graph()->nodes;
    check_float(nodes[0].param_values[0], 5.0f, "scale param initialized to 5.0");

    // --- Step 4: Tick v1 and verify output ---
    std::fprintf(stderr, "\n--- Tick with v1 ---\n");
    runtime.tick(0.0, 0.016, 0);
    // v1: output = scale * 2.0 = 5.0 * 2.0 = 10.0
    check_float(nodes[0].output_values[0], 10.0f, "v1 output = scale * 2.0 = 10.0");

    // --- Step 5: Reload with v2 ---
    std::fprintf(stderr, "\n--- Reloading with v2 ---\n");

    // Stage v2 to a unique path (like HotReloader does)
    std::string staged_v2 = staging + "/test_op_v2_reload_0.dylib";
    std::filesystem::copy_file(v2_path, staged_v2,
        std::filesystem::copy_options::overwrite_existing);

    check(runtime.reload_operator("TestOp", registry, staged_v2),
          "reload_operator() succeeds");

    // --- Step 6: Verify param reconciliation ---
    std::fprintf(stderr, "\n--- Verify param reconciliation ---\n");
    check(nodes[0].param_values.size() == 2, "now has 2 params (was 1)");
    check_float(nodes[0].param_values[0], 5.0f, "scale preserved at 5.0");
    check_float(nodes[0].param_values[1], 10.0f, "offset got default 10.0");
    check(nodes[0].dirty, "dirty flag set after reload");

    // Verify type_name accessor
    std::string tn = runtime.type_name(0);
    check(tn == "TestOp", "type_name() returns TestOp after reload");

    // --- Step 7: Tick v2 and verify new output ---
    std::fprintf(stderr, "\n--- Tick with v2 ---\n");
    runtime.tick(0.0, 0.016, 1);
    // v2: output = scale * 3.0 + offset = 5.0 * 3.0 + 10.0 = 25.0
    check_float(nodes[0].output_values[0], 25.0f, "v2 output = scale * 3.0 + offset = 25.0");

    // --- Step 8: Test reload with same dylib (idempotent) ---
    std::fprintf(stderr, "\n--- Reload idempotent ---\n");
    std::string staged_v2b = staging + "/test_op_v2_reload_1.dylib";
    std::filesystem::copy_file(v2_path, staged_v2b,
        std::filesystem::copy_options::overwrite_existing);
    check(runtime.reload_operator("TestOp", registry, staged_v2b),
          "re-reload with same version succeeds");
    check_float(nodes[0].param_values[0], 5.0f, "scale still preserved");
    check_float(nodes[0].param_values[1], 10.0f, "offset still preserved");
    runtime.tick(0.0, 0.016, 2);
    check_float(nodes[0].output_values[0], 25.0f, "output unchanged after re-reload");

    // --- Step 9: Incompatible port layout change is rejected ---
    std::fprintf(stderr, "\n--- Reject incompatible port layout ---\n");
    std::string staged_bad = staging + "/test_op_bad_reload_0.dylib";
    std::filesystem::copy_file(bad_port_path, staged_bad,
        std::filesystem::copy_options::overwrite_existing);
    check(!runtime.reload_operator("TestOp", registry, staged_bad),
          "reload rejects incompatible port layout");
    check_float(nodes[0].param_values[0], 5.0f, "scale preserved after rejected reload");
    check_float(nodes[0].param_values[1], 10.0f, "offset preserved after rejected reload");
    runtime.tick(0.0, 0.016, 3);
    check_float(nodes[0].output_values[0], 25.0f,
                "previous operator remains active after rejected reload");

    // --- Cleanup ---
    runtime.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
