// Tests for RuntimeAPI role binding commands:
//   set_role_binding, clear_role_binding
// Covers success/failure, undo/redo via RuntimeCommandSink, serialization round-trip,
// and dirty flag tracking.

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/runtime_api.h"
#include "runtime/runtime_command_sink.h"
#include "runtime/operator_info_cache.h"
#include "runtime/settings.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void settle_topology(vivid::RuntimeAPI& api, bool& has_gpu_ops, bool& has_audio) {
    if (api.has_pending()) {
        api.apply_pending(has_gpu_ops, has_audio);
    }
}

// Non-mutating helper to inspect role binding state.
static const vivid::NodeDef::RoleBindingState* find_role_binding(
    const vivid::Graph& g, const char* node_id, const char* role_id)
{
    const auto* node = g.find_node(node_id);
    if (!node) return nullptr;
    auto it = node->role_bindings.find(role_id);
    return it != node->role_bindings.end() ? &it->second : nullptr;
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::fprintf(stderr, "\n=== Test: Role binding commands ===\n\n");

    // Clean and stage test operator plugins so RuntimeAPI can resolve types.
    std::string staging = build_dir + "/.test_role_binding_cmd_staging";
    fs::remove_all(staging);
    fs::create_directories(staging);
    fs::copy_file(build_dir + "/test_op_with_roles.dylib",
                  staging + "/test_op_with_roles.dylib",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(build_dir + "/test_op_v1.dylib",
                  staging + "/test_op_v1.dylib",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(build_dir + "/envelope.dylib",
                  staging + "/envelope.dylib",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(build_dir + "/lfo.dylib",
                  staging + "/lfo.dylib",
                  fs::copy_options::overwrite_existing);
    fs::copy_file(build_dir + "/test_multi_output_bindable.dylib",
                  staging + "/test_multi_output_bindable.dylib",
                  fs::copy_options::overwrite_existing);

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load((build_dir + "/test_role_binding_commands.json").c_str()), "graph.load()");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");

    vivid::AudioEngine audio_engine;
    vivid::RuntimeAPI api(graph, scheduler, audio_engine, registry);
    bool has_gpu_ops = false;
    bool has_audio = false;

    RuntimeCommandSink sink(api);
    sink.set_graph(&graph);
    sink.set_runtime_flags(&has_gpu_ops, &has_audio);
    vivid::Settings sink_settings;
    sink_settings.editor = "custom";
    sink_settings.editor_command = "true";
    sink.set_settings(&sink_settings);

    // -----------------------------------------------------------------------
    // A. set_role_binding — success
    // -----------------------------------------------------------------------
    std::fprintf(stderr, "\n--- A. set_role_binding success ---\n");
    {
        auto r = api.set_role_binding("host", "mod", "env1", "value");
        check(r.ok, "set_role_binding returns ok");

        const auto* binding = find_role_binding(graph, "host", "mod");
        check(binding != nullptr, "role_bindings has 'mod' key");
        check(binding && binding->target_node_id == "env1", "binding target is 'src'");
        check(binding && binding->target_output_name == "value", "binding output is 'out'");
        check(api.has_pending(), "topology change pending after set_role_binding");
        check(api.graph_dirty(), "graph dirty after set_role_binding");
        settle_topology(api, has_gpu_ops, has_audio);
    }

    // -----------------------------------------------------------------------
    // B. set_role_binding — validation failures
    // -----------------------------------------------------------------------
    std::fprintf(stderr, "\n--- B. set_role_binding validation failures ---\n");
    {
        auto r1 = api.set_role_binding("nonexistent", "mod", "env1", "value");
        check(!r1.ok, "bad node returns error");

        auto r2 = api.set_role_binding("host", "bad_role", "env1", "value");
        check(!r2.ok, "bad role returns error");

        // Target not bindable (TestOp doesn't export VIVID_BINDABLE)
        auto r3 = api.set_role_binding("host", "mod", "src", "out");
        check(!r3.ok, "non-bindable target returns error");

        // Invalid output name on a bindable target
        auto r4 = api.set_role_binding("host", "mod", "env1", "nonexistent_output");
        check(!r4.ok, "invalid output name returns error");
    }

    // -----------------------------------------------------------------------
    // C. clear_role_binding
    // -----------------------------------------------------------------------
    std::fprintf(stderr, "\n--- C. clear_role_binding ---\n");
    {
        // Ensure binding is assigned first
        api.set_role_binding("host", "mod", "env1", "value");
        settle_topology(api, has_gpu_ops, has_audio);

        auto r = api.clear_role_binding("host", "mod");
        check(r.ok, "clear_role_binding returns ok");
        check(find_role_binding(graph, "host", "mod") == nullptr,
              "role_bindings no longer has 'mod' key");
        check(api.has_pending(), "topology change pending after clear");

        auto r2 = api.clear_role_binding("host", "mod");
        check(!r2.ok, "clear nonexistent role returns error");
        settle_topology(api, has_gpu_ops, has_audio);
    }

    // -----------------------------------------------------------------------
    // D. Serialization round-trip
    // -----------------------------------------------------------------------
    std::fprintf(stderr, "\n--- D. Serialization round-trip ---\n");
    {
        api.set_role_binding("host", "mod", "env1", "value");
        settle_topology(api, has_gpu_ops, has_audio);

        std::string json;
        check(graph.save_to_string(json), "save_to_string succeeds");

        std::string tmp_path = build_dir + "/test_role_binding_roundtrip.json";
        {
            std::ofstream ofs(tmp_path);
            ofs << json;
        }

        vivid::Graph graph2;
        check(graph2.load(tmp_path.c_str()), "reload graph from file");

        const auto* binding2 = find_role_binding(graph2, "host", "mod");
        check(binding2 != nullptr, "mod role exists in reloaded graph");
        check(binding2 && binding2->target_node_id == "env1", "target_node_id preserved");
        check(binding2 && binding2->target_output_name == "value", "target_output_name preserved");

        fs::remove(tmp_path);
    }

    // -----------------------------------------------------------------------
    // E. Undo/redo via RuntimeCommandSink
    // -----------------------------------------------------------------------
    std::fprintf(stderr, "\n--- E. Undo/redo via RuntimeCommandSink ---\n");
    {
        // Start fresh
        check(graph.load_from_string(R"({
  "nodes": {
    "host": { "type": "TestOpWithRoles", "params": { "gain": 1.0 } },
    "src":  { "type": "TestOp", "params": { "scale": 2.0 } },
    "env1": { "type": "Envelope", "params": {} }
  },
  "connections": []
})"), "reload clean graph for undo tests");
        check(scheduler.build(graph, registry), "rebuild scheduler");
        sink.reset_undo_history();

        // E1: set_role_binding + undo + redo
        sink.set_role_binding("host", "mod", "env1", "value");
        settle_topology(api, has_gpu_ops, has_audio);
        check(find_role_binding(graph, "host", "mod") != nullptr,
              "E1: binding assigned via sink");

        check(sink.undo(), "E1: undo set_role_binding");
        check(find_role_binding(graph, "host", "mod") == nullptr,
              "E1: role_bindings cleared after undo");

        check(sink.redo(), "E1: redo set_role_binding");
        check(find_role_binding(graph, "host", "mod") != nullptr,
              "E1: role_bindings restored after redo");
        settle_topology(api, has_gpu_ops, has_audio);

        // E2: clear_role_binding + undo + redo
        sink.clear_role_binding("host", "mod");
        settle_topology(api, has_gpu_ops, has_audio);
        check(find_role_binding(graph, "host", "mod") == nullptr,
              "E2: role cleared via sink");

        check(sink.undo(), "E2: undo clear_role_binding");
        check(find_role_binding(graph, "host", "mod") != nullptr,
              "E2: binding restored after undo clear");

        check(sink.redo(), "E2: redo clear_role_binding");
        check(find_role_binding(graph, "host", "mod") == nullptr,
              "E2: role cleared again after redo");
    }

    // -----------------------------------------------------------------------
    // F. Multi-output binding — bind to non-default output
    // -----------------------------------------------------------------------
    std::fprintf(stderr, "\n--- F. Multi-output binding ---\n");
    {
        // Reload graph with multi-output node (test E replaced it)
        check(graph.load_from_string(R"({
  "nodes": {
    "host":  { "type": "TestOpWithRoles", "params": { "gain": 1.0 } },
    "src":   { "type": "TestOp", "params": { "scale": 2.0 } },
    "env1":  { "type": "Envelope", "params": {} },
    "multi": { "type": "TestMultiOutputBindable", "params": { "rate": 1.0 } }
  },
  "connections": []
})"), "reload graph with multi-output node");
        check(scheduler.build(graph, registry), "rebuild scheduler");
    }
    {
        // Bind to the "phase" output on a multi-output operator
        auto r1 = api.set_role_binding("host", "mod", "multi", "phase");
        check(r1.ok, "bind to 'phase' output succeeds");

        const auto* binding = find_role_binding(graph, "host", "mod");
        check(binding != nullptr, "role_bindings has 'mod' key");
        check(binding && binding->target_node_id == "multi",
              "binding target is 'multi'");
        check(binding && binding->target_output_name == "phase",
              "binding output is 'phase'");
        settle_topology(api, has_gpu_ops, has_audio);

        // Re-bind to "value" output
        auto r2 = api.set_role_binding("host", "mod", "multi", "value");
        check(r2.ok, "rebind to 'value' output succeeds");
        binding = find_role_binding(graph, "host", "mod");
        check(binding && binding->target_output_name == "value",
              "binding output updated to 'value'");
        settle_topology(api, has_gpu_ops, has_audio);
    }

    // -----------------------------------------------------------------------
    // G. Multi-output serialization round-trip
    // -----------------------------------------------------------------------
    std::fprintf(stderr, "\n--- G. Multi-output serialization round-trip ---\n");
    {
        api.set_role_binding("host", "mod", "multi", "phase");
        settle_topology(api, has_gpu_ops, has_audio);

        std::string json;
        check(graph.save_to_string(json), "save_to_string succeeds");

        std::string tmp_path = build_dir + "/test_role_binding_multi_rt.json";
        {
            std::ofstream ofs(tmp_path);
            ofs << json;
        }

        vivid::Graph graph2;
        check(graph2.load(tmp_path.c_str()), "reload graph from file");

        const auto* binding2 = find_role_binding(graph2, "host", "mod");
        check(binding2 != nullptr, "mod role exists in reloaded graph");
        check(binding2 && binding2->target_node_id == "multi",
              "target_node_id 'multi' preserved");
        check(binding2 && binding2->target_output_name == "phase",
              "target_output_name 'phase' preserved");

        fs::remove(tmp_path);
    }

    // -----------------------------------------------------------------------
    // Summary
    // -----------------------------------------------------------------------
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
