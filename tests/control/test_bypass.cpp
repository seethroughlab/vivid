// Bypass feature integration test.
//
// Covers:
//   1. Compiler-derived `bypassable` flag on CompiledNode (first input/output
//      port types match → bypassable; otherwise not).
//   2. RuntimeAPI::set_node_bypassed validation: rejects ineligible nodes,
//      propagates to NodeDef + CompiledNode for eligible ones.
//   3. Live frame-executor passthrough: bypassed ControlPassOp passes its
//      input through unchanged regardless of `gain` param.
//   4. JSON serialization round-trip: `bypassed: true` survives save/reload.

#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/runtime_api.h"
#include <cstdio>
#include <filesystem>
#include <string>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::fprintf(stderr, "\n=== Test: Bypass ===\n\n");

    // Stage operator dylibs into a fresh dir so the registry only sees what
    // the test needs (TestOp = source, ControlPassOp = passthrough-eligible).
    const std::string staging = build_dir + "/.test_bypass_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
        staging + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/control_pass_op.dylib",
        staging + "/control_pass_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    // Build a graph: src (TestOp, scale=10 → out=20) → pass (ControlPassOp, gain=3)
    // Wire src/out → pass/in. With gain=3 not bypassed: pass/out = 20 * 3 = 60.
    // With pass bypassed: pass/out should equal pass/in = 20 (gain has no effect).
    vivid::Graph graph;
    {
        std::unordered_map<std::string, float> src_params{{"scale", 10.0f}};
        std::unordered_map<std::string, float> pass_params{{"gain", 3.0f}};
        check(graph.add_node("src", "TestOp", src_params), "add src");
        check(graph.add_node("pass", "ControlPassOp", pass_params), "add pass");
        check(graph.add_connection("src", "out", "pass", "in"), "connect src/out → pass/in");
    }

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    auto* cg = runtime.compiled_graph();
    check(cg != nullptr, "compiled graph exists");

    const vivid::CompiledNode* src_cn = cg->find_node("src");
    const vivid::CompiledNode* pass_cn = cg->find_node("pass");
    check(src_cn != nullptr, "src compiled node");
    check(pass_cn != nullptr, "pass compiled node");

    // --- Compiler bypassability ---
    std::fprintf(stderr, "\n--- compiler bypassability ---\n");
    if (src_cn) {
        // TestOp has no inputs (only output) → not bypassable.
        check(!src_cn->bypassable, "TestOp not bypassable (no input port)");
        check(!src_cn->bypassed, "TestOp not bypassed initially");
    }
    if (pass_cn) {
        // ControlPassOp has scalar in + scalar out → bypassable.
        check(pass_cn->bypassable, "ControlPassOp bypassable (scalar→scalar)");
        check(!pass_cn->bypassed, "ControlPassOp not bypassed initially");
    }

    vivid::AudioEngine audio_engine;
    vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);

    // --- Tick once: pass should be a regular pipeline (output = in * gain) ---
    std::fprintf(stderr, "\n--- baseline tick (no bypass) ---\n");
    runtime.tick(0.0, 0.016, 0);
    if (pass_cn) {
        // src: scale=10 → out=20. pass: in=20, gain=3 → out=60.
        check_float(pass_cn->output_values[0], 60.0f, "pass/out = 20 * 3 = 60");
    }

    // --- set_node_bypassed: rejects ineligible (TestOp) ---
    std::fprintf(stderr, "\n--- set_node_bypassed validation ---\n");
    {
        auto r = api.set_node_bypassed("src", true);
        check(!r.ok, "set_node_bypassed(src, true) rejected");
        check(r.message.find("not bypass-eligible") != std::string::npos,
              "rejection message mentions eligibility");
        // NodeDef must remain unchanged.
        const auto* ndef = graph.find_node("src");
        check(ndef && !ndef->bypassed, "src NodeDef.bypassed still false after rejection");
    }

    // --- set_node_bypassed: missing node ---
    {
        auto r = api.set_node_bypassed("nonexistent", true);
        check(!r.ok, "unknown node rejected");
    }

    // --- set_node_bypassed: bypass ControlPassOp, verify passthrough ---
    std::fprintf(stderr, "\n--- bypass passthrough ---\n");
    {
        auto r = api.set_node_bypassed("pass", true);
        check(r.ok, "set_node_bypassed(pass, true) accepted");
        const auto* ndef = graph.find_node("pass");
        check(ndef && ndef->bypassed, "pass NodeDef.bypassed is true");
        check(pass_cn && pass_cn->bypassed, "pass CompiledNode.bypassed is true (live)");

        // Tick. Bypass branch should run: pass/out = pass/in (passthrough).
        runtime.tick(0.016, 0.016, 1);
        if (pass_cn) {
            check_float(pass_cn->output_values[0], 20.0f,
                        "pass/out = pass/in = 20 (gain ignored when bypassed)");
        }
    }

    // --- set_node_bypassed: idempotent toggle off ---
    std::fprintf(stderr, "\n--- toggle off ---\n");
    {
        auto r = api.set_node_bypassed("pass", false);
        check(r.ok, "set_node_bypassed(pass, false)");
        const auto* ndef = graph.find_node("pass");
        check(ndef && !ndef->bypassed, "pass NodeDef.bypassed is false again");

        runtime.tick(0.032, 0.016, 2);
        if (pass_cn) {
            check_float(pass_cn->output_values[0], 60.0f,
                        "pass/out back to 20 * 3 = 60");
        }

        auto r2 = api.set_node_bypassed("pass", false);
        check(r2.ok, "second toggle-off succeeds (idempotent)");
        check(r2.message.find("already") != std::string::npos,
              "no-op message indicates already-enabled");
    }

    // --- JSON round-trip preserves the bypassed flag ---
    std::fprintf(stderr, "\n--- JSON round-trip ---\n");
    {
        // Set bypass on, save, reload into a fresh Graph, verify.
        auto r = api.set_node_bypassed("pass", true);
        check(r.ok, "bypass on for round-trip");

        std::string json;
        check(graph.save_to_string(json), "graph.save_to_string()");
        check(json.find("\"bypassed\": true") != std::string::npos ||
              json.find("\"bypassed\":true") != std::string::npos,
              "serialized JSON contains bypassed: true");

        vivid::Graph loaded;
        check(loaded.load_from_string(json.c_str(), json.size()),
              "graph.load_from_string()");
        const auto* loaded_pass = loaded.find_node("pass");
        const auto* loaded_src  = loaded.find_node("src");
        check(loaded_pass && loaded_pass->bypassed,
              "loaded pass has bypassed=true");
        check(loaded_src && !loaded_src->bypassed,
              "loaded src has bypassed=false (default)");
    }

    std::fprintf(stderr, "\n=== %s: %d failures ===\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
