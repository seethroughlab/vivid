#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/runtime_api.h"
#include "runtime/operators/operator_preparation_service.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include "test_helpers.h"

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string graph_path = build_dir + "/test_runtime_api.json";

    // Setup: staging dir with test_op_v1
    std::string staging = build_dir + "/.test_api_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
        staging + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/test_state_carry_op.dylib",
        staging + "/test_state_carry_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: RuntimeAPI ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    // --- Test shared operator preparation path on a deferred registry ---
    std::fprintf(stderr, "\n--- operator preparation service ---\n");
    {
        vivid::OperatorRegistry deferred_registry;
        check(deferred_registry.scan_deferred(staging.c_str()), "registry.scan_deferred()");
        auto prepared = vivid::prepare_operator_type_sync(deferred_registry, "TestOp");
        check(prepared.success, "prepare deferred TestOp succeeds");
        check(deferred_registry.find_loaded("TestOp") != nullptr, "TestOp promoted to loaded");
        auto prepared_again = vivid::prepare_operator_type_sync(deferred_registry, "TestOp");
        check(prepared_again.success, "re-preparing already-loaded TestOp succeeds");
    }

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    // --- Test null compiled graph guards ---
    std::fprintf(stderr, "\n--- null compiled graph guards ---\n");
    {
        vivid::Graph no_cg_graph;
        check(no_cg_graph.load(graph_path.c_str()), "no_cg graph.load()");
        vivid::RuntimeCore no_cg_runtime;
        vivid::AudioEngine no_cg_audio;
        vivid::RuntimeAPI no_cg_api(no_cg_graph, no_cg_runtime, no_cg_audio, registry);

        // VariationDef removed in Phase 1.

        vivid::OperatorPreset preset;
        preset.name = "NoCompiledGraphPreset";
        no_cg_graph.save_preset("a", preset);

        auto expect_no_compiled_graph = [](const vivid::CommandResult& r, const char* msg) {
            check(!r.ok, msg);
            check(r.message == "no compiled graph", "error is no compiled graph");
        };

        expect_no_compiled_graph(no_cg_api.set_param("a", "scale", 1.0f), "set_param fails cleanly");
        expect_no_compiled_graph(no_cg_api.set_string_param("a", "path", "x"), "set_string_param fails cleanly");
        expect_no_compiled_graph(no_cg_api.get_param("a", "scale"), "get_param fails cleanly");
        expect_no_compiled_graph(no_cg_api.set_param_lock("a", "scale", vivid::PARAM_LOCK_PRESETS),
                                 "set_param_lock fails cleanly");
        expect_no_compiled_graph(no_cg_api.get_param_lock("a", "scale"), "get_param_lock fails cleanly");
        expect_no_compiled_graph(no_cg_api.inspect("a"), "inspect fails cleanly");
        // save_variation / recall_variation / update_variation / queue_variation
        // removed in Phase 1 (RuntimeAPI variation methods removed).
        expect_no_compiled_graph(no_cg_api.save_preset("a", "NoCompiledGraphSavedPreset"),
                                 "save_preset fails cleanly");
        expect_no_compiled_graph(no_cg_api.recall_preset("a", "NoCompiledGraphPreset"),
                                 "recall_preset fails cleanly");
        expect_no_compiled_graph(no_cg_api.update_preset("a", "NoCompiledGraphPreset"),
                                 "update_preset fails cleanly");
        expect_no_compiled_graph(no_cg_api.set_solo("a"), "set_solo fails cleanly");

        auto list = no_cg_api.list_nodes();
        check(list.ok, "list_nodes succeeds without compiled graph");
        check(list.message == "(no nodes)", "list_nodes reports no nodes");
        check(no_cg_api.solo_node_id().empty(), "solo_node_id empty without compiled graph");

        auto res = no_cg_api.set_resolution("a", 320, 240);
        check(res.ok, "set_resolution updates authored graph without compiled graph");
        check(no_cg_api.needs_gpu_realloc(), "set_resolution marks gpu realloc without compiled graph");
        check(no_cg_api.graph_dirty(), "set_resolution marks graph dirty without compiled graph");

        no_cg_api.tick_state_presets();
        check(true, "tick helpers no-op without compiled graph");
    }

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    vivid::AudioEngine audio_engine;
    bool has_gpu_ops = false;
    bool has_audio = false;

    vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);

    // --- Test set_param ---
    std::fprintf(stderr, "\n--- set_param ---\n");
    {
        auto r = api.set_param("a", "scale", 7.0f);
        check(r.ok, "set a/scale = 7.0");
        runtime.tick(0.0, 0.016, 0);
        // a: output = scale * 2.0 = 7.0 * 2.0 = 14.0
        check_float(runtime.compiled_graph()->nodes[0].output_values[0], 14.0f, "a output = 14.0");
    }

    // --- Test get_param ---
    std::fprintf(stderr, "\n--- get_param ---\n");
    {
        auto r = api.get_param("a", "scale");
        check(r.ok, "get a/scale succeeds");
        check(r.message == "7", "value is 7");
    }

    // --- Test get_param error ---
    {
        auto r = api.get_param("nonexistent", "scale");
        check(!r.ok, "get nonexistent fails");
    }

    // --- Test set_param error ---
    {
        auto r = api.set_param("a", "bogus", 1.0f);
        check(!r.ok, "set unknown param fails");
    }

    // --- Test inspect ---
    std::fprintf(stderr, "\n--- inspect ---\n");
    {
        auto r = api.inspect("a");
        check(r.ok, "inspect a");
        check(r.message.find("TestOp") != std::string::npos, "inspect shows type");
        check(r.message.find("scale") != std::string::npos, "inspect shows param name");
    }

    // --- Test list_nodes ---
    std::fprintf(stderr, "\n--- list_nodes ---\n");
    {
        auto r = api.list_nodes();
        check(r.ok, "list_nodes");
        check(r.message.find("a (TestOp)") != std::string::npos, "list shows node a");
        check(r.message.find("b (TestOp)") != std::string::npos, "list shows node b");
    }

    // --- Test list_types ---
    std::fprintf(stderr, "\n--- list_types ---\n");
    {
        auto r = api.list_types();
        check(r.ok, "list_types");
        check(r.message.find("TestOp") != std::string::npos, "lists TestOp");
    }

    // --- Test add_node + connect + apply_pending ---
    std::fprintf(stderr, "\n--- add_node + connect ---\n");
    {
        auto r1 = api.add_node("TestOp", "c");
        check(r1.ok, "add TestOp as c");

        auto r2 = api.connect("a/out", "c/scale");
        check(r2.ok, "connect a/out -> c/scale");

        check(api.has_pending(), "has pending topology change");
        api.apply_pending(has_gpu_ops, has_audio);

        // Now tick: a has scale=7 (preserved), output=14. c gets a's output=14 as scale, output=14*2=28
        runtime.tick(0.0, 0.016, 1);

        // Find node c in rebuilt runtime
        const vivid::CompiledNode* c_node = nullptr;
        for (const auto& ns : runtime.compiled_graph()->nodes) {
            if (ns.node_id == "c") { c_node = &ns; break; }
        }
        check(c_node != nullptr, "node c exists after rebuild");
        if (c_node) {
            check_float(c_node->output_values[0], 28.0f, "c output = 14 * 2 = 28");
        }

        // Verify a's param was preserved across rebuild
        for (const auto& ns : runtime.compiled_graph()->nodes) {
            if (ns.node_id == "a") {
                auto pi = ns.param_indices.find("scale");
                check(pi != ns.param_indices.end(), "a still has scale param");
                if (pi != ns.param_indices.end()) {
                    check_float(ns.param_values[pi->second], 7.0f, "a/scale preserved at 7.0");
                }
                break;
            }
        }
    }

    // --- Test remove_node ---
    std::fprintf(stderr, "\n--- remove_node ---\n");
    {
        auto r = api.remove_node("c");
        check(r.ok, "remove c");
        api.apply_pending(has_gpu_ops, has_audio);
        check(runtime.compiled_graph()->nodes.size() == 2, "back to 2 nodes");
    }

    // --- Test disconnect ---
    std::fprintf(stderr, "\n--- disconnect ---\n");
    {
        auto r = api.disconnect("a/out", "b/scale");
        check(r.ok, "disconnect a/out -> b/scale");
        api.apply_pending(has_gpu_ops, has_audio);
        // b is now standalone with scale=2.0 (its original JSON value)
        runtime.tick(0.0, 0.016, 2);
    }

    // --- Test save + reload ---
    std::fprintf(stderr, "\n--- save + reload ---\n");
    {
        auto& meta = graph.meta_mut();
        meta = {};
        meta.title = "Runtime API Save Fixture";
        meta.domains = {"audio", "control"};
        meta.content_kind = "instrument";
        meta.category = "synth";
        meta.family = "pads";
        meta.role = "hero";
        meta.playability = "hybrid";
        meta.preview_controls = {
            {"a", "scale", "Drive"},
            {"b", "scale", ""}
        };

        std::string save_path = build_dir + "/test_api_saved.json";
        auto r1 = api.save_as(save_path);
        check(r1.ok, "save_as");
        check(graph.source_path() == save_path, "save_as updates graph source_path");

        // Verify the saved file can be loaded
        vivid::Graph g2;
        check(g2.load(save_path.c_str()), "reload saved graph");
        check(g2.nodes().size() == 2, "saved graph has 2 nodes");
        check(g2.meta().content_kind == "instrument", "save_as preserves content_kind");
        check(g2.meta().category == "synth", "save_as preserves category");
        check(g2.meta().family == "pads", "save_as preserves family");
        check(g2.meta().role == "hero", "save_as preserves role");
        check(g2.meta().playability == "hybrid", "save_as preserves playability");
        check(g2.meta().domains.size() == 2, "save_as preserves canonical domains");
        check(g2.meta().preview_controls.size() == 2, "save_as preserves preview controls");
        check(g2.meta().preview_controls[0].label == "Drive", "save_as preserves preview labels");

        std::filesystem::remove(save_path);
    }

    // --- Test add_node duplicate ---
    {
        auto r = api.add_node("TestOp", "a");
        check(!r.ok, "add duplicate node fails");
    }

    // --- Test add_node unknown type ---
    {
        auto r = api.add_node("NoSuchType", "z");
        check(!r.ok, "add unknown type fails");
    }

    // --- Test set_node_layout success ---
    std::fprintf(stderr, "\n--- set_node_layout ---\n");
    {
        auto r = api.set_node_layout("a", 100.5f, 200.0f);
        check(r.ok, "set_node_layout a success");
        const auto* ndef = graph.find_node("a");
        check(ndef != nullptr, "find node a");
        if (ndef) {
            check(ndef->has_layout(), "a has layout");
            check_float(ndef->layout_x, 100.5f, "a layout_x = 100.5");
            check_float(ndef->layout_y, 200.0f, "a layout_y = 200.0");
        }
    }

    // --- Test set_node_layout unknown node ---
    {
        auto r = api.set_node_layout("nonexistent", 1.0f, 2.0f);
        check(!r.ok, "set_node_layout unknown node fails");
        check(r.message.find("unknown node") != std::string::npos, "error mentions unknown node");
    }

    // --- Test set_resolution success ---
    std::fprintf(stderr, "\n--- set_resolution ---\n");
    {
        auto r = api.set_resolution("a", 1920, 1080);
        check(r.ok, "set_resolution a 1920x1080");
        const auto* ndef = graph.find_node("a");
        check(ndef != nullptr, "resolution: find node a");
        if (ndef) {
            check(ndef->tex_width == 1920, "NodeDef tex_width = 1920");
            check(ndef->tex_height == 1080, "NodeDef tex_height = 1080");
        }
        auto* cn = runtime.compiled_graph()->find_node("a");
        check(cn != nullptr, "CompiledNode a exists");
        if (cn) {
            // TestOp is not a GPU operator, so gpu sub-struct is not present.
            // The resolution is stored on the NodeDef (verified above) and will
            // take effect when the graph is recompiled with a GPU node.
            check(!cn->gpu, "non-GPU node has no gpu state");
        }
        check(api.needs_gpu_realloc(), "needs_gpu_realloc set");
    }

    // --- Test clear_gpu_realloc ---
    {
        api.clear_gpu_realloc();
        check(!api.needs_gpu_realloc(), "needs_gpu_realloc cleared");
    }

    // --- Test set_resolution zero error ---
    {
        auto r = api.set_resolution("a", 0, 1080);
        check(!r.ok, "set_resolution width=0 fails");
        check(r.message.find("non-zero") != std::string::npos, "error mentions non-zero");
    }

    // --- Test set_resolution exceeds limit ---
    {
        auto r = api.set_resolution("a", 8193, 1080);
        check(!r.ok, "set_resolution width=8193 fails");
        check(r.message.find("8192") != std::string::npos, "error mentions 8192 limit");
    }

    // --- Test set_resolution boundary ---
    {
        auto r = api.set_resolution("a", 8192, 8192);
        check(r.ok, "set_resolution 8192x8192 boundary OK");
    }

    // --- Test set_resolution unknown node ---
    {
        auto r = api.set_resolution("nonexistent", 1920, 1080);
        check(!r.ok, "set_resolution unknown node fails");
        check(r.message.find("unknown node") != std::string::npos, "error mentions unknown node");
    }

    // --- Test has_pending flag ---
    std::fprintf(stderr, "\n--- has_pending / apply_pending ---\n");
    {
        check(!api.has_pending(), "no pending initially");
        api.add_node("TestOp", "pending_test");
        check(api.has_pending(), "has pending after add_node");
        api.apply_pending(has_gpu_ops, has_audio);
        check(!api.has_pending(), "no pending after apply");
        // Clean up added node
        api.remove_node("pending_test");
        api.apply_pending(has_gpu_ops, has_audio);
    }

    // --- Test apply_pending no changes ---
    {
        check(!api.apply_pending(has_gpu_ops, has_audio), "apply_pending false with no changes");
    }

    // --- Test save() success ---
    std::fprintf(stderr, "\n--- save / reload ---\n");
    {
        std::string tmp_path = build_dir + "/test_save_success.json";
        // Save current graph to create a file on disk
        api.save_as(tmp_path);

        // Create auxiliary objects loaded from that file (sets source_path_)
        vivid::Graph g2;
        check(g2.load(tmp_path.c_str()), "save test: load graph");
        vivid::RuntimeCore s2;
        check(s2.build(g2, registry), "save test: build runtime");
        vivid::AudioEngine ae2;
        vivid::RuntimeAPI api2(g2, s2, ae2, registry);

        // Modify a param
        api2.set_param("a", "scale", 42.0f);

        // save() should work because source_path is set
        auto r = api2.save();
        check(r.ok, "save() succeeds");

        // Reload and verify param persisted
        vivid::Graph g3;
        check(g3.load(tmp_path.c_str()), "save test: reload");
        const auto* ndef = g3.find_node("a");
        check(ndef != nullptr, "save test: node a exists");
        if (ndef) {
            auto it = ndef->params.find("scale");
            check(it != ndef->params.end(), "save test: scale param exists");
            if (it != ndef->params.end()) {
                check_float(it->second, 42.0f, "save test: scale = 42.0");
            }
        }

        s2.shutdown();
        std::filesystem::remove(tmp_path);
    }

    // --- Test save() no source_path ---
    {
        vivid::Graph g_empty;
        g_empty.add_node("x", "TestOp");
        vivid::RuntimeCore s_empty;
        vivid::AudioEngine ae_empty;
        vivid::RuntimeAPI api_empty(g_empty, s_empty, ae_empty, registry);
        auto r = api_empty.save();
        check(!r.ok, "save() fails with no source_path");
        check(r.message.find("no source path") != std::string::npos, "error mentions no source path");
    }

    // --- Test reload() success ---
    {
        std::string tmp_path = build_dir + "/test_reload_success.json";
        api.save_as(tmp_path);  // Save the 2-node graph

        vivid::Graph g2;
        check(g2.load(tmp_path.c_str()), "reload test: load graph");
        vivid::RuntimeCore s2;
        check(s2.build(g2, registry), "reload test: build runtime");
        vivid::AudioEngine ae2;
        vivid::RuntimeAPI api2(g2, s2, ae2, registry);

        // Add a node to make 3
        api2.add_node("TestOp", "extra");
        bool hgpu = false, haudio = false;
        api2.apply_pending(hgpu, haudio);
        check(s2.compiled_graph()->nodes.size() == 3, "reload test: 3 nodes after add");

        // Reload from disk → back to 2
        auto r = api2.reload(hgpu, haudio);
        check(r.ok, "reload() succeeds");
        check(s2.compiled_graph()->nodes.size() == 2, "reload test: back to 2 nodes");

        s2.shutdown();
        std::filesystem::remove(tmp_path);
    }

    // --- Test reload() no source_path ---
    {
        vivid::Graph g_empty;
        vivid::RuntimeCore s_empty;
        vivid::AudioEngine ae_empty;
        vivid::RuntimeAPI api_empty(g_empty, s_empty, ae_empty, registry);
        bool hgpu = false, haudio = false;
        auto r = api_empty.reload(hgpu, haudio);
        check(!r.ok, "reload() fails with no source_path");
        check(r.message.find("no source path") != std::string::npos, "error mentions no source path");
    }

    // --- Regression: reload failure restores previous runtime state ---
    {
        const std::string tmp_path = build_dir + "/test_reload_failure_restore.json";
        const std::string invalid_json = R"({ "nodes": { "broken": { "type": "TestOp", )";

        check(api.save_as(tmp_path).ok, "reload failure regression: save current graph");
        check(api.set_param("a", "scale", 33.0f).ok,
              "reload failure regression: mutate live param before failure");
        runtime.tick(0.0, 0.016, 3);

        {
            std::ofstream ofs(tmp_path, std::ios::trunc);
            ofs << invalid_json;
        }

        bool hgpu = false, haudio = false;
        auto rr = api.reload(hgpu, haudio);
        check(!rr.ok, "reload failure regression: reload fails on malformed file");
        check(graph.source_path() == tmp_path,
              "reload failure regression: graph source_path preserved after failure");
        check(graph.nodes().size() == 2,
              "reload failure regression: graph restored after failed reload");
        check(runtime.compiled_graph()->nodes.size() == 2,
              "reload failure regression: runtime restored after failed reload");

        const vivid::CompiledNode* a_node = nullptr;
        for (const auto& ns : runtime.compiled_graph()->nodes) {
            if (ns.node_id == "a") {
                a_node = &ns;
                break;
            }
        }
        check(a_node != nullptr, "reload failure regression: node a still exists");
        if (a_node) {
            auto pi = a_node->param_indices.find("scale");
            check(pi != a_node->param_indices.end(),
                  "reload failure regression: scale param still exists");
            if (pi != a_node->param_indices.end()) {
                check_float(a_node->param_values[pi->second], 33.0f,
                            "reload failure regression: live param restored after failed reload");
            }
        }

        std::filesystem::remove(tmp_path);
    }

    // --- Regression: graph switch must not restore runtime state by node ID ---
    std::fprintf(stderr, "\n--- reload graph switch regression ---\n");
    {
        const std::string graph_a_path = build_dir + "/test_reload_graph_a.json";
        const std::string graph_b_path = build_dir + "/test_reload_graph_b.json";

        {
            std::ofstream ofs(graph_a_path);
            ofs << R"({
  "nodes": {
    "a": { "type": "TestStateCarryOp", "params": { "scale": 1.0, "label": "graph-a" } }
  }
}
)";
        }
        {
            std::ofstream ofs(graph_b_path);
            ofs << R"({
  "nodes": {
    "a": { "type": "TestStateCarryOp", "params": { "scale": 5.0, "label": "graph-b" } }
  }
}
)";
        }

        vivid::Graph g_switch;
        check(g_switch.load(graph_a_path.c_str()), "switch regression: load graph A");
        vivid::RuntimeCore s_switch;
        check(s_switch.build(g_switch, registry), "switch regression: build graph A");
        vivid::AudioEngine ae_switch;
        vivid::RuntimeAPI api_switch(g_switch, s_switch, ae_switch, registry);

        auto r1 = api_switch.set_param("a", "scale", 77.0f);
        check(r1.ok, "switch regression: mutate numeric param on graph A");
        auto r2 = api_switch.set_string_param("a", "label", "runtime-value");
        check(r2.ok, "switch regression: mutate string param on graph A");
        auto r3 = api_switch.set_param_lock("a", "scale", vivid::PARAM_LOCK_ALL);
        check(r3.ok, "switch regression: mutate lock flags on graph A");

        check(g_switch.load(graph_b_path.c_str()),
              "switch regression: load graph B into same Graph object");
        bool hgpu = false, haudio = false;
        auto rr = api_switch.reload(hgpu, haudio);
        check(rr.ok, "switch regression: reload after graph switch");

        const vivid::CompiledNode* a_node = nullptr;
        for (const auto& ns : s_switch.compiled_graph()->nodes) {
            if (ns.node_id == "a") {
                a_node = &ns;
                break;
            }
        }
        check(a_node != nullptr, "switch regression: node a exists in graph B");
        if (a_node) {
            auto pi = a_node->param_indices.find("scale");
            check(pi != a_node->param_indices.end(), "switch regression: scale param present");
            if (pi != a_node->param_indices.end()) {
                check_float(a_node->param_values[pi->second], 5.0f,
                            "switch regression: numeric param reset from graph B");
                check(a_node->param_lock_flags[pi->second] == vivid::PARAM_LOCK_NONE,
                      "switch regression: lock flags reset from graph B");
            }

            auto fi = a_node->file_param_indices.find("label");
            check(fi != a_node->file_param_indices.end(), "switch regression: label string param present");
            if (fi != a_node->file_param_indices.end()) {
                check(a_node->file_param_storage[fi->second] == "graph-b",
                      "switch regression: string param reset from graph B");
            }
        }

        s_switch.shutdown();
        std::filesystem::remove(graph_a_path);
        std::filesystem::remove(graph_b_path);
    }

    // --- Regression: external graph-load helpers preserve same-graph live state ---
    std::fprintf(stderr, "\n--- external graph load helper regression ---\n");
    {
        const std::string graph_path = build_dir + "/test_external_graph_load.json";
        {
            std::ofstream ofs(graph_path);
            ofs << R"({
  "nodes": {
    "a": { "type": "TestStateCarryOp", "params": { "scale": 2.0, "label": "graph-file" } }
  }
}
)";
        }

        vivid::Graph g_ext;
        check(g_ext.load(graph_path.c_str()), "external graph helper: load graph");
        vivid::RuntimeCore s_ext;
        check(s_ext.build(g_ext, registry), "external graph helper: build runtime");
        vivid::AudioEngine ae_ext;
        vivid::RuntimeAPI api_ext(g_ext, s_ext, ae_ext, registry);
        api_ext.finalize_external_graph_load();

        check(api_ext.set_param("a", "scale", 31.0f).ok,
              "external graph helper: mutate live numeric param");
        check(api_ext.set_string_param("a", "label", "runtime-label").ok,
              "external graph helper: mutate live string param");
        check(api_ext.set_param_lock("a", "scale", vivid::PARAM_LOCK_ALL).ok,
              "external graph helper: mutate lock flags");

        auto preserved = api_ext.capture_preserved_runtime_state_for_path(graph_path);
        check(preserved.active, "external graph helper: same-path capture is active");

        check(g_ext.load(graph_path.c_str()), "external graph helper: reload file definition");
        check(s_ext.build(g_ext, registry), "external graph helper: rebuild runtime from file");
        api_ext.apply_preserved_runtime_state(preserved);
        api_ext.finalize_external_graph_load();

        const vivid::CompiledNode* a_node = nullptr;
        for (const auto& ns : s_ext.compiled_graph()->nodes) {
            if (ns.node_id == "a") {
                a_node = &ns;
                break;
            }
        }
        check(a_node != nullptr, "external graph helper: node a exists after rebuild");
        if (a_node) {
            auto pi = a_node->param_indices.find("scale");
            check(pi != a_node->param_indices.end(), "external graph helper: scale param present");
            if (pi != a_node->param_indices.end()) {
                check_float(a_node->param_values[pi->second], 31.0f,
                            "external graph helper: live numeric param restored");
                check(a_node->param_lock_flags[pi->second] == vivid::PARAM_LOCK_ALL,
                      "external graph helper: lock flags restored");
            }

            auto fi = a_node->file_param_indices.find("label");
            check(fi != a_node->file_param_indices.end(), "external graph helper: label param present");
            if (fi != a_node->file_param_indices.end()) {
                check(a_node->file_param_storage[fi->second] == "runtime-label",
                      "external graph helper: live string param restored");
            }
        }
        check(!api_ext.graph_dirty(),
              "external graph helper: finalize_external_graph_load captures a clean snapshot");

        s_ext.shutdown();
        std::filesystem::remove(graph_path);
    }

    // --- Regression: apply_snapshot_json malformed input restores graph + source identity ---
    std::fprintf(stderr, "\n--- apply_snapshot_json malformed regression ---\n");
    {
        const std::string tmp_path = build_dir + "/test_apply_snapshot_restore.json";
        const std::string invalid_json = R"({ "nodes": { "broken": { "type": "TestOp" )";

        check(api.save_as(tmp_path).ok, "snapshot malformed regression: save current graph");
        check(!api.graph_dirty(), "snapshot malformed regression: clean after save");
        check(api.set_param("a", "scale", 44.0f).ok,
              "snapshot malformed regression: mutate live param before failure");
        check(api.graph_dirty(), "snapshot malformed regression: dirty after mutation");

        bool hgpu = false, haudio = false;
        auto r = api.apply_snapshot_json(invalid_json, hgpu, haudio);
        check(!r.ok, "snapshot malformed regression: apply fails");
        check(graph.source_path() == tmp_path,
              "snapshot malformed regression: source_path preserved");
        check(graph.nodes().size() == 2,
              "snapshot malformed regression: graph restored after failed apply");
        check(runtime.compiled_graph()->nodes.size() == 2,
              "snapshot malformed regression: runtime restored after failed apply");
        check(api.graph_dirty(),
              "snapshot malformed regression: dirty state preserved after failed apply");

        const vivid::CompiledNode* a_node = nullptr;
        for (const auto& ns : runtime.compiled_graph()->nodes) {
            if (ns.node_id == "a") {
                a_node = &ns;
                break;
            }
        }
        check(a_node != nullptr, "snapshot malformed regression: node a still exists");
        if (a_node) {
            auto pi = a_node->param_indices.find("scale");
            check(pi != a_node->param_indices.end(),
                  "snapshot malformed regression: scale param still exists");
            if (pi != a_node->param_indices.end()) {
                check_float(a_node->param_values[pi->second], 44.0f,
                            "snapshot malformed regression: live param restored");
            }
        }

        std::filesystem::remove(tmp_path);
    }

    // --- Regression: failed snapshot apply preserves dirty graph in undo-style flow ---
    {
        vivid::Graph g_dirty;
        check(g_dirty.add_node("a", "TestOp"), "undo-style snapshot regression: add node a");
        vivid::RuntimeCore s_dirty;
        check(s_dirty.build(g_dirty, registry), "undo-style snapshot regression: build runtime");
        vivid::AudioEngine ae_dirty;
        vivid::RuntimeAPI api_dirty(g_dirty, s_dirty, ae_dirty, registry);

        check(api_dirty.set_param("a", "scale", 12.0f).ok,
              "undo-style snapshot regression: mutate unsaved graph");
        check(api_dirty.graph_dirty(),
              "undo-style snapshot regression: unsaved mutation marks graph dirty");

        std::string before_json;
        check(g_dirty.save_to_string(before_json),
              "undo-style snapshot regression: serialize current graph");

        bool hgpu = false, haudio = false;
        auto r = api_dirty.apply_snapshot_json("{ not valid json", hgpu, haudio);
        check(!r.ok, "undo-style snapshot regression: malformed apply fails");
        check(api_dirty.graph_dirty(),
              "undo-style snapshot regression: dirty state preserved after failed apply");

        std::string after_json;
        check(g_dirty.save_to_string(after_json),
              "undo-style snapshot regression: serialize restored graph");
        check(before_json == after_json,
              "undo-style snapshot regression: graph content unchanged after failed apply");
        check(g_dirty.source_path().empty(),
              "undo-style snapshot regression: unsaved graph remains unsaved");

        s_dirty.shutdown();
    }

    // --- Regression: apply_snapshot_json preserves source_path for saved and unsaved graphs ---
    std::fprintf(stderr, "\n--- apply_snapshot_json source_path regression ---\n");
    {
        const std::string saved_path = build_dir + "/test_apply_snapshot_source_path.json";

        vivid::Graph g_saved;
        check(g_saved.add_node("a", "TestOp"), "snapshot source_path regression: add node a");
        vivid::RuntimeCore s_saved;
        check(s_saved.build(g_saved, registry), "snapshot source_path regression: build saved runtime");
        vivid::AudioEngine ae_saved;
        vivid::RuntimeAPI api_saved(g_saved, s_saved, ae_saved, registry);

        check(api_saved.save_as(saved_path).ok, "snapshot source_path regression: save saved graph");
        check(g_saved.source_path() == saved_path,
              "snapshot source_path regression: saved graph source_path set");

        const std::string saved_snapshot = R"({
  "nodes": {
    "a": { "type": "TestOp", "params": { "scale": 9.0 } }
  }
}
)";
        bool hgpu_saved = false, haudio_saved = false;
        auto saved_apply = api_saved.apply_snapshot_json(saved_snapshot, hgpu_saved, haudio_saved);
        check(saved_apply.ok, "snapshot source_path regression: apply succeeds for saved graph");
        check(g_saved.source_path() == saved_path,
              "snapshot source_path regression: saved graph source_path preserved");
        check(api_saved.graph_dirty(),
              "snapshot source_path regression: saved graph becomes dirty after changed apply");

        vivid::Graph g_unsaved;
        check(g_unsaved.add_node("a", "TestOp"), "snapshot source_path regression: add node a to unsaved graph");
        vivid::RuntimeCore s_unsaved;
        check(s_unsaved.build(g_unsaved, registry), "snapshot source_path regression: build unsaved runtime");
        vivid::AudioEngine ae_unsaved;
        vivid::RuntimeAPI api_unsaved(g_unsaved, s_unsaved, ae_unsaved, registry);

        const std::string unsaved_snapshot = R"({
  "nodes": {
    "a": { "type": "TestOp", "params": { "scale": 5.0 } },
    "b": { "type": "TestOp", "params": { "scale": 6.0 } }
  },
  "connections": [
    { "from": "a/out", "to": "b/scale" }
  ]
}
)";
        bool hgpu_unsaved = false, haudio_unsaved = false;
        auto unsaved_apply = api_unsaved.apply_snapshot_json(unsaved_snapshot, hgpu_unsaved, haudio_unsaved);
        check(unsaved_apply.ok, "snapshot source_path regression: apply succeeds for unsaved graph");
        check(g_unsaved.source_path().empty(),
              "snapshot source_path regression: unsaved graph source_path remains empty");
        check(api_unsaved.graph_dirty(),
              "snapshot source_path regression: unsaved graph remains dirty after apply");

        s_saved.shutdown();
        s_unsaved.shutdown();
        std::filesystem::remove(saved_path);
    }

    // --- Test set_node_layout persists through save/reload ---
    std::fprintf(stderr, "\n--- persistence ---\n");
    {
        std::string tmp_path = build_dir + "/test_layout_persist.json";

        vivid::Graph g2;
        g2.add_node("a", "TestOp");
        vivid::RuntimeCore s2;
        s2.build(g2, registry);
        vivid::AudioEngine ae2;
        vivid::RuntimeAPI api2(g2, s2, ae2, registry);

        auto save_as_result = api2.save_as(tmp_path);
        check(save_as_result.ok, "save_as on unsaved graph succeeds");
        check(g2.source_path() == tmp_path, "save_as retargets graph source_path");

        api2.set_node_layout("a", 42.0f, 99.0f);
        auto save_result = api2.save();
        check(save_result.ok, "save() uses retargeted source_path after save_as");

        // Reload and verify
        vivid::Graph g3;
        check(g3.load(tmp_path.c_str()), "layout persist: reload");
        const auto* ndef = g3.find_node("a");
        check(ndef != nullptr && ndef->has_layout(), "layout persist: has layout");
        if (ndef) {
            check_float(ndef->layout_x, 42.0f, "layout persist: x = 42.0");
            check_float(ndef->layout_y, 99.0f, "layout persist: y = 99.0");
        }

        s2.shutdown();
        std::filesystem::remove(tmp_path);
    }

    // --- Test set_resolution persists through save/reload ---
    {
        std::string tmp_path = build_dir + "/test_resolution_persist.json";

        vivid::Graph g2;
        g2.add_node("a", "TestOp");
        vivid::RuntimeCore s2;
        s2.build(g2, registry);
        vivid::AudioEngine ae2;
        vivid::RuntimeAPI api2(g2, s2, ae2, registry);

        api2.set_resolution("a", 1920, 1080);
        api2.save_as(tmp_path);

        vivid::Graph g3;
        check(g3.load(tmp_path.c_str()), "resolution persist: reload");
        const auto* ndef = g3.find_node("a");
        check(ndef != nullptr, "resolution persist: node a exists");
        if (ndef) {
            check(ndef->tex_width == 1920, "resolution persist: width = 1920");
            check(ndef->tex_height == 1080, "resolution persist: height = 1080");
        }

        s2.shutdown();
        std::filesystem::remove(tmp_path);
    }

    // save_variation, recall_variation, list_variations, update_variation, rename_variation,
    // duplicate_variation, move_variation, remove_variation, queue_variation tests removed
    // in Phase 1: RuntimeAPI variation methods removed.

    // --- Test metronome-backed quantized switching ---
    std::fprintf(stderr, "\n--- metronome-backed quantized switching ---\n");
    {
        auto m = api.set_graph_metronome(120.0f, 3);
        check(m.ok, "set graph metronome in 3/4 ok");
        check(!api.has_pending(), "metronome change does not queue a rebuild");
        check_float(graph.metronome().bpm, 120.0f, "graph metadata metronome bpm updated");
        check(graph.metronome().beats_per_bar == 3, "graph metadata meter updated");
        auto enabled_sample = api.current_metronome_sample();
        check(enabled_sample.beats_per_bar == 3, "live metronome sample uses the new meter");

        // Tick to establish a known time base, then change BPM.
        runtime.tick(10.00, 0.016, 31);
        const auto before_bpm_change = api.current_metronome_sample();
        auto bpm_change = api.set_graph_metronome(60.0f, 3);
        check(bpm_change.ok, "live bpm change ok");
        const auto after_bpm_change = api.current_metronome_sample();
        check_float(static_cast<float>(after_bpm_change.beats_elapsed),
                    static_cast<float>(before_bpm_change.beats_elapsed), 1e-5f,
                    "bpm change preserves beat continuity");
        check_float(after_bpm_change.bpm, 60.0f, 1e-5f, "live metronome bpm updates immediately");

        // 1 second later at 60 BPM = 1 additional beat.
        runtime.tick(11.00, 0.016, 32);
        const auto slowed_sample = api.current_metronome_sample();
        const float expected_beats = static_cast<float>(before_bpm_change.beats_elapsed) + 1.0f;
        check_float(static_cast<float>(slowed_sample.beats_elapsed), expected_beats, 0.02f,
                    "slower bpm advances from the preserved beat anchor");

        auto meter_change = api.set_graph_metronome(60.0f, 5);
        check(meter_change.ok, "meter change ok");
        const auto reset_sample = api.current_metronome_sample();
        check(reset_sample.beats_per_bar == 5, "live metronome adopts new meter");
        check_float(static_cast<float>(reset_sample.beats_elapsed), 0.0f, 1e-5f,
                    "meter change resets beat count immediately");

        // Switch to 120 BPM, 3/4
        auto meter_update = api.set_graph_metronome(120.0f, 3);
        check(meter_update.ok, "update metronome to 3/4 ok");

        // Quantized variation switching tests removed in Phase 1 (queue_variation,
        // tick_quantized_switch, pending_variation_idx removed from RuntimeAPI).
    }

    // --- Test ensure_state_mapping ---
    std::fprintf(stderr, "\n--- ensure_state_mapping ---\n");
    {
        // inspect_state_presets returns "no state-preset mappings" when none registered
        auto before = api.inspect_state_presets("new_sm");
        check(before.ok, "inspect before: ok");
        check(before.message.find("no state-preset") != std::string::npos,
              "inspect before: no mapping message");

        auto r = api.ensure_state_mapping("new_sm");
        check(r.ok, "ensure_state_mapping ok");

        // After ensure, inspect returns something other than "no state-preset mappings"
        auto after = api.inspect_state_presets("new_sm");
        check(after.ok, "inspect after: ok");
        check(after.message.find("no state-preset") == std::string::npos,
              "inspect after: mapping exists");

        // Idempotent: calling again doesn't create a duplicate
        auto r2 = api.ensure_state_mapping("new_sm");
        check(r2.ok, "ensure_state_mapping idempotent ok");
        auto after2 = api.inspect_state_presets("new_sm");
        check(after2.message == after.message, "idempotent: state unchanged");
    }

    // --- Test queue_state_transition ---
    std::fprintf(stderr, "\n--- queue_state_transition ---\n");
    {
        // Out-of-range state index
        auto r_bad = api.queue_state_transition("a", 9, "instant");
        check(!r_bad.ok, "state 9 out of range");
        auto r_neg = api.queue_state_transition("a", -1, "instant");
        check(!r_neg.ok, "state -1 out of range");

        // Bar-quantized: enqueues without requiring a compiled node
        auto r_bar = api.queue_state_transition("a", 1, "bar");
        check(r_bar.ok, "queue_state_transition bar ok");
        check(api.queued_state_for("a") == 1, "pending state = 1");

        // Replacing a pending transition
        auto r_bar2 = api.queue_state_transition("a", 3, "bar");
        check(r_bar2.ok, "replace pending transition ok");
        check(api.queued_state_for("a") == 3, "pending state replaced to 3");

        // queued_state_for returns -1 for a node with no pending transition
        check(api.queued_state_for("b") == -1, "no pending for unqueued node");
        check(api.queued_state_for("nonexistent") == -1, "no pending for unknown node");
    }

    // --- Test set_quantize_clock ---
    std::fprintf(stderr, "\n--- set_quantize_clock ---\n");
    {
        auto r = api.set_quantize_clock("a");
        check(r.ok, "set_quantize_clock a ok");
    }

    // "recall after node removal" variation test removed in Phase 1
    // (RuntimeAPI variation methods removed).

    // =========================================================================
    // Phase 2: Session Track/Clip CRUD + capture/apply
    // =========================================================================

    // Add two fresh nodes for Session tests to avoid state bleed from earlier tests.
    std::fprintf(stderr, "\n--- session: setup ---\n");
    {
        auto r1 = api.add_node("TestOp", "s1");
        check(r1.ok, "add s1");
        auto r2 = api.add_node("TestOp", "s2");
        check(r2.ok, "add s2");
        check(api.has_pending(), "topology pending");
        api.apply_pending(has_gpu_ops, has_audio);
        api.set_param("s1", "scale", 5.0f);
        api.set_param("s2", "scale", 9.0f);
        runtime.tick(0.0, 0.016, 2);
    }

    // --- Track CRUD ---
    std::fprintf(stderr, "\n--- session: track CRUD ---\n");
    {
        // create_track
        auto r = api.create_track("Bass");
        check(r.ok, "create_track Bass");
        check(!r.message.empty(), "create_track returns id");
        std::string tid = r.message;

        // rename_track
        auto rn = api.rename_track(tid, "Drums");
        check(rn.ok, "rename_track to Drums");
        check(graph.find_track(tid)->name == "Drums", "graph reflects rename");

        // unknown track rename fails
        auto bad = api.rename_track("no_such_track", "X");
        check(!bad.ok, "rename unknown track fails");

        // move_track: create a second track then move
        auto t2r = api.create_track("Lead");
        check(t2r.ok, "create second track Lead");
        std::string tid2 = t2r.message;
        check(graph.session().tracks[0].id == tid, "Drums first before move");
        auto mv = api.move_track(tid, 1);
        check(mv.ok, "move_track Drums to index 1");
        check(graph.session().tracks[1].id == tid, "Drums at index 1");

        // assign_nodes_to_track
        auto ar = api.assign_nodes_to_track(tid, {"s1", "s2"});
        check(ar.ok, "assign s1, s2 to track");
        check(graph.find_track(tid)->owned_node_ids.size() == 2, "track owns 2 nodes");

        // unassign_nodes_from_track
        auto ur = api.unassign_nodes_from_track(tid, {"s2"});
        check(ur.ok, "unassign s2");
        check(graph.find_track(tid)->owned_node_ids.size() == 1, "track owns 1 node");
        auto re_ar = api.assign_nodes_to_track(tid, {"s2"});
        check(re_ar.ok, "re-assign s2");

        // remove_track
        auto rmr = api.remove_track(tid2);
        check(rmr.ok, "remove_track Lead");
        check(graph.find_track(tid2) == nullptr, "Lead removed from graph");
        // tid (Drums) still present
        check(graph.find_track(tid) != nullptr, "Drums track still present");
    }

    // --- save_clip captures owned-node params ---
    std::fprintf(stderr, "\n--- session: save_clip captures params ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        // s1.scale=5, s2.scale=9 at this point
        auto r = api.save_clip(tid, "Verse");
        check(r.ok, "save_clip Verse");
        std::string cid = r.message;
        check(!cid.empty(), "save_clip returns clip id");
        check(graph.find_track(tid)->clips.size() == 1, "1 clip after save");

        const auto* clip = graph.find_clip(tid, cid);
        check(clip != nullptr, "clip found by id");
        if (clip) {
            check(clip->params.count("s1") == 1, "s1 captured in clip");
            check(clip->params.count("s2") == 1, "s2 captured in clip");
            check(clip->params.at("s1").count("scale") == 1, "s1.scale in clip");
            check_float(clip->params.at("s1").at("scale"), 5.0f, "s1.scale=5.0 captured");
            check_float(clip->params.at("s2").at("scale"), 9.0f, "s2.scale=9.0 captured");
        }
    }

    // --- save_clip skips wire-driven params ---
    std::fprintf(stderr, "\n--- session: save_clip skips wired params ---\n");
    {
        // Re-establish a/out -> b/scale wire (it was disconnected in an earlier section).
        api.connect("a/out", "b/scale");
        api.apply_pending(has_gpu_ops, has_audio);

        auto tr = api.create_track("WireTrack");
        check(tr.ok, "create WireTrack");
        std::string wire_tid = tr.message;
        api.assign_nodes_to_track(wire_tid, {"b"});

        auto r = api.save_clip(wire_tid, "WireClip");
        check(r.ok, "save_clip on wire-driven track");
        std::string cid = r.message;
        const auto* clip = graph.find_clip(wire_tid, cid);
        check(clip != nullptr, "wire clip found");
        if (clip) {
            // b/scale should be ABSENT because it's wire-driven
            bool has_scale = clip->params.count("b") &&
                             clip->params.at("b").count("scale");
            check(!has_scale, "b.scale absent from clip (wire-driven)");
        }
        api.remove_track(wire_tid);

        // Disconnect again to keep graph state clean for subsequent tests.
        api.disconnect("a/out", "b/scale");
        api.apply_pending(has_gpu_ops, has_audio);
    }

    // --- save_clip skips PARAM_LOCK_WIRES params ---
    std::fprintf(stderr, "\n--- session: save_clip skips PARAM_LOCK_WIRES ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        // Lock s1.scale with PARAM_LOCK_WIRES
        api.set_param_lock("s1", "scale", vivid::PARAM_LOCK_WIRES);
        auto r = api.save_clip(tid, "LockedClip");
        check(r.ok, "save_clip with locked param");
        std::string cid = r.message;
        const auto* clip = graph.find_clip(tid, cid);
        check(clip != nullptr, "locked clip found");
        if (clip) {
            bool has_s1_scale = clip->params.count("s1") &&
                                clip->params.at("s1").count("scale");
            check(!has_s1_scale, "s1.scale absent (PARAM_LOCK_WIRES)");
        }
        // Restore: remove lock
        api.set_param_lock("s1", "scale", vivid::PARAM_LOCK_NONE);
    }

    // --- save_clip does NOT capture non-owned nodes ---
    std::fprintf(stderr, "\n--- session: save_clip scope is track-owned only ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        auto r = api.save_clip(tid, "ScopeClip");
        check(r.ok, "save ScopeClip");
        const auto* clip = graph.find_clip(tid, r.message);
        check(clip != nullptr, "ScopeClip found");
        if (clip) {
            // "a" is not owned by this track — should NOT be captured
            check(clip->params.count("a") == 0, "non-owned node 'a' absent from clip");
        }
    }

    // --- launch_clip restores stored params ---
    std::fprintf(stderr, "\n--- session: launch_clip restores params ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        // Find the "Verse" clip saved earlier (first clip)
        std::string cid = graph.find_track(tid)->clips[0].id;

        // Mutate live params
        api.set_param("s1", "scale", 99.0f);
        api.set_param("s2", "scale", 77.0f);
        runtime.tick(0.0, 0.016, 3);

        // Verify they changed
        {
            auto* cn = runtime.compiled_graph()->find_node("s1");
            check(cn != nullptr, "s1 in compiled graph");
            if (cn) check_float(cn->param_values[cn->param_indices.at("scale")], 99.0f, "s1.scale=99 before launch");
        }

        // Launch the Verse clip
        auto r = api.launch_clip(tid, cid);
        check(r.ok, "launch_clip Verse");
        check(r.message == cid, "launch_clip returns clip_id");
        check(api.active_clip(tid) == cid, "active_clip returns launched clip");

        // Verify params restored in compiled graph and nodes marked dirty
        // (dirty flag ensures non-leaf nodes re-process on next tick)
        {
            auto* cn1 = runtime.compiled_graph()->find_node("s1");
            auto* cn2 = runtime.compiled_graph()->find_node("s2");
            check(cn1 != nullptr && cn2 != nullptr, "s1 and s2 in compiled graph");
            if (cn1) {
                check_float(cn1->param_values[cn1->param_indices.at("scale")], 5.0f,
                            "s1.scale restored to 5.0");
                check(cn1->dirty, "s1 marked dirty after launch_clip");
            }
            if (cn2) {
                check_float(cn2->param_values[cn2->param_indices.at("scale")], 9.0f,
                            "s2.scale restored to 9.0");
                check(cn2->dirty, "s2 marked dirty after launch_clip");
            }
        }
        // Verify synced to graph NodeDef
        {
            auto* nd = graph.find_node("s1");
            check(nd != nullptr, "s1 NodeDef exists");
            if (nd) check_float(nd->params.at("scale"), 5.0f, "s1 NodeDef.scale=5.0");
        }
    }

    // --- launch_clip does NOT touch non-Track-owned nodes ---
    std::fprintf(stderr, "\n--- session: launch_clip scope isolation ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        std::string cid = graph.find_track(tid)->clips[0].id;

        // Set a.scale to a known value
        api.set_param("a", "scale", 42.0f);
        runtime.tick(0.0, 0.016, 4);

        // Launch clip (track owns s1, s2 — not 'a')
        api.launch_clip(tid, cid);
        runtime.tick(0.0, 0.016, 5);

        // a.scale must be untouched
        auto* cn_a = runtime.compiled_graph()->find_node("a");
        check(cn_a != nullptr, "a in compiled graph");
        if (cn_a)
            check_float(cn_a->param_values[cn_a->param_indices.at("scale")], 42.0f,
                        "a.scale untouched by launch_clip");
    }

    // --- launch_clip respects PARAM_LOCK_PRESETS ---
    std::fprintf(stderr, "\n--- session: launch_clip respects PARAM_LOCK_PRESETS ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        std::string cid = graph.find_track(tid)->clips[0].id;  // Verse: s1.scale=5

        // Set s1.scale to a known value, then lock it against presets
        api.set_param("s1", "scale", 33.0f);
        api.set_param_lock("s1", "scale", vivid::PARAM_LOCK_PRESETS);

        api.launch_clip(tid, cid);

        // s1.scale should NOT be restored (locked against presets/clips)
        auto* cn1 = runtime.compiled_graph()->find_node("s1");
        check(cn1 != nullptr, "s1 in graph");
        if (cn1)
            check_float(cn1->param_values[cn1->param_indices.at("scale")], 33.0f,
                        "s1.scale untouched (PARAM_LOCK_PRESETS)");

        // Restore lock
        api.set_param_lock("s1", "scale", vivid::PARAM_LOCK_NONE);
    }

    // --- launch_clip with bad track/clip returns error ---
    std::fprintf(stderr, "\n--- session: launch_clip error handling ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        std::string cid = graph.find_track(tid)->clips[0].id;

        auto bad_track = api.launch_clip("no_such_track", cid);
        check(!bad_track.ok, "launch_clip unknown track fails");

        auto bad_clip = api.launch_clip(tid, "no_such_clip");
        check(!bad_clip.ok, "launch_clip unknown clip fails");
    }

    // --- save_clip without compiled graph returns error ---
    std::fprintf(stderr, "\n--- session: save_clip no-cg guard ---\n");
    {
        vivid::Graph g2;
        check(g2.load(graph_path.c_str()), "load fixture for no-cg test");
        std::string tid2 = g2.create_track("T");
        vivid::RuntimeCore rt2;
        vivid::AudioEngine ae2;
        vivid::RuntimeAPI api2(g2, rt2, ae2, registry);
        auto r = api2.save_clip(tid2, "NoGraph");
        check(!r.ok, "save_clip fails without compiled graph");
        check(r.message == "no compiled graph", "error is no compiled graph");
        auto lr = api2.launch_clip(tid2, "x");
        check(!lr.ok, "launch_clip fails without compiled graph");
    }

    // --- Clip rename, move, remove ---
    std::fprintf(stderr, "\n--- session: clip rename/move/remove ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        std::string cid0 = graph.find_track(tid)->clips[0].id;

        // rename
        auto rn = api.rename_clip(tid, cid0, "Chorus");
        check(rn.ok, "rename_clip ok");
        check(graph.find_clip(tid, cid0)->name == "Chorus", "clip renamed");

        // add a second clip then move
        auto sc2 = api.save_clip(tid, "Bridge");
        check(sc2.ok, "save Bridge clip");
        std::string cid1 = sc2.message;
        check(graph.find_track(tid)->clips.size() >= 2, "2+ clips");

        auto mv = api.move_clip(tid, cid0, 1);
        check(mv.ok, "move_clip cid0 to index 1");
        check(graph.find_track(tid)->clips[1].id == cid0, "cid0 at index 1");

        // remove
        auto rm = api.remove_clip(tid, cid0);
        check(rm.ok, "remove_clip cid0");
        check(graph.find_clip(tid, cid0) == nullptr, "cid0 gone");
        check(api.active_clip(tid).empty(), "active_clip cleared after remove");
    }

    // --- update_clip re-captures live state ---
    std::fprintf(stderr, "\n--- session: update_clip re-captures ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        api.set_param("s1", "scale", 11.0f);
        api.set_param("s2", "scale", 22.0f);
        auto sc = api.save_clip(tid, "UpdateTest");
        check(sc.ok, "save UpdateTest clip");
        std::string cid = sc.message;

        // Change params and update
        api.set_param("s1", "scale", 55.0f);
        api.set_param("s2", "scale", 66.0f);
        auto ur = api.update_clip(tid, cid);
        check(ur.ok, "update_clip succeeds");

        const auto* clip = graph.find_clip(tid, cid);
        check(clip != nullptr, "updated clip found");
        if (clip) {
            check_float(clip->params.at("s1").at("scale"), 55.0f, "s1.scale=55 after update");
            check_float(clip->params.at("s2").at("scale"), 66.0f, "s2.scale=66 after update");
        }
    }

    // ==========================================================================
    // Phase 3: Scene CRUD + Quantized Clip/Scene Launch
    // ==========================================================================
    // Phase 2 tests left s1.scale with PARAM_LOCK_PRESETS; clear all locks first.
    std::fprintf(stderr, "\n--- session: Phase 3 setup ---\n");
    {
        api.set_param_lock("s1", "scale", vivid::PARAM_LOCK_NONE);
        api.set_param_lock("s2", "scale", vivid::PARAM_LOCK_NONE);
        api.set_param("s1", "scale", 5.0f);
        api.set_param("s2", "scale", 9.0f);
        // Rebuild the Verse clip with clean params (s1=5, s2=9)
        std::string tid = graph.session().tracks[0].id;
        std::string verse_cid = graph.find_track(tid)->clips[0].id;
        api.update_clip(tid, verse_cid);
        api.launch_clip(tid, verse_cid);
        check(api.active_clip(tid) == verse_cid, "Phase 3 setup: active_clip set");
    }

    // --- session: save_scene captures active_clips ---
    std::fprintf(stderr, "\n--- session: save_scene captures active_clips ---\n");
    std::string verse_sid;
    {
        std::string tid = graph.session().tracks[0].id;
        std::string verse_cid = graph.find_track(tid)->clips[0].id;

        auto r = api.save_scene("Verse");
        check(r.ok, "save_scene Verse");
        verse_sid = r.message;
        check(!verse_sid.empty(), "save_scene returns scene_id");

        const auto* scene = graph.find_scene(verse_sid);
        check(scene != nullptr, "scene found by id");
        if (scene) {
            bool has_tid = scene->assignments.count(tid) > 0;
            check(has_tid, "scene has assignment for track");
            if (has_tid)
                check(scene->assignments.at(tid) == verse_cid, "scene assignment = Verse clip");
        }
    }

    // --- session: save_scene with no active clips ---
    std::fprintf(stderr, "\n--- session: save_scene empty active_clips ---\n");
    {
        // Create a fresh Graph+API with no clips launched (active_clips_ is empty)
        vivid::Graph g_sc;
        vivid::RuntimeCore s_sc;
        vivid::AudioEngine ae_sc;
        vivid::RuntimeAPI api_sc(g_sc, s_sc, ae_sc, registry);
        auto r = api_sc.save_scene("Empty");
        check(r.ok, "save_scene with empty active_clips succeeds");
        check(!r.message.empty(), "returns scene_id even when empty");
        const auto* scene = g_sc.find_scene(r.message);
        check(scene != nullptr, "empty scene created");
        if (scene) check(scene->assignments.empty(), "no assignments in empty scene");
    }

    // --- session: scene rename / move / remove ---
    std::fprintf(stderr, "\n--- session: scene rename/move/remove ---\n");
    {
        auto r2 = api.save_scene("Chorus");
        check(r2.ok, "save_scene Chorus");
        std::string chorus_sid = r2.message;

        check(api.rename_scene(verse_sid, "Intro").ok, "rename_scene ok");
        check(graph.find_scene(verse_sid)->name == "Intro", "scene renamed");

        // Two scenes: move Chorus to index 0
        check(api.move_scene(chorus_sid, 0).ok, "move_scene ok");
        check(graph.session().scenes[0].id == chorus_sid, "Chorus at index 0");

        check(api.remove_scene(chorus_sid).ok, "remove_scene ok");
        check(graph.find_scene(chorus_sid) == nullptr, "Chorus removed");

        // Rename back to Verse for subsequent tests
        api.rename_scene(verse_sid, "Verse");
    }

    // --- session: set_scene_assignment / set_scene_leave_unchanged / clear_scene_assignment ---
    std::fprintf(stderr, "\n--- session: scene assignment manipulation ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        std::string cid = graph.find_track(tid)->clips[0].id;

        // Manually clear and re-add assignment
        check(api.clear_scene_assignment(verse_sid, tid).ok, "clear_scene_assignment ok");
        {
            const auto* s = graph.find_scene(verse_sid);
            check(s && s->assignments.count(tid) == 0, "assignment cleared");
            check(s && s->leave_unchanged.count(tid) == 0, "not in leave_unchanged either");
        }

        check(api.set_scene_assignment(verse_sid, tid, cid).ok, "set_scene_assignment ok");
        check(graph.find_scene(verse_sid)->assignments.count(tid) > 0, "assignment restored");

        check(api.set_scene_leave_unchanged(verse_sid, tid).ok, "set_scene_leave_unchanged ok");
        {
            const auto* s = graph.find_scene(verse_sid);
            check(s && s->assignments.count(tid) == 0, "not in assignments after leave_unchanged");
            check(s && s->leave_unchanged.count(tid) > 0, "in leave_unchanged set");
        }

        // Restore correct assignment for launch tests
        check(api.set_scene_assignment(verse_sid, tid, cid).ok, "restore assignment");
    }

    // --- session: update_scene re-captures active_clips ---
    std::fprintf(stderr, "\n--- session: update_scene re-captures ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        // Save a second clip and launch it
        api.set_param("s1", "scale", 20.0f);
        api.set_param("s2", "scale", 30.0f);
        auto r = api.save_clip(tid, "Chorus");
        check(r.ok, "save Chorus clip");
        std::string chorus_cid = r.message;
        api.launch_clip(tid, chorus_cid);

        // Now update the scene — it should capture chorus_cid
        check(api.update_scene(verse_sid).ok, "update_scene ok");
        const auto* s = graph.find_scene(verse_sid);
        check(s != nullptr, "scene still exists");
        if (s) {
            bool has_tid = s->assignments.count(tid) > 0;
            check(has_tid, "update_scene: assignment exists");
            if (has_tid)
                check(s->assignments.at(tid) == chorus_cid, "update_scene: captures chorus clip");
        }

        // Restore Verse state for subsequent tests
        std::string verse_cid = graph.find_track(tid)->clips[0].id;
        api.launch_clip(tid, verse_cid);
        api.update_scene(verse_sid);
    }

    // --- session: queue_clip instant ---
    std::fprintf(stderr, "\n--- session: queue_clip instant ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        std::string chorus_cid = graph.find_track(tid)->clips.back().id; // Chorus

        api.set_param("s1", "scale", 99.0f);
        auto r = api.queue_clip(tid, chorus_cid, "instant");
        check(r.ok, "queue_clip instant ok");
        // Should apply immediately like launch_clip
        auto* cn = runtime.compiled_graph()->find_node("s1");
        check(cn != nullptr, "s1 in graph");
        if (cn)
            check_float(cn->param_values[cn->param_indices.at("scale")], 20.0f,
                        "queue_clip instant: s1.scale=20");
        check(api.active_clip(tid) == chorus_cid, "active_clip updated by queue_clip instant");
        check(api.queued_clip_for(tid).empty(), "no pending after instant");
    }

    // --- session: queue_clip beat-aligned ---
    std::fprintf(stderr, "\n--- session: queue_clip beat-aligned ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        std::string verse_cid = graph.find_track(tid)->clips[0].id;

        // Queue the Verse clip at bar boundary (high target beat so it doesn't fire immediately)
        auto r = api.queue_clip(tid, verse_cid, "bar");
        check(r.ok, "queue_clip bar ok");
        check(api.queued_clip_for(tid) == verse_cid, "queued_clip_for returns verse_cid");
        check(api.active_clip(tid) != verse_cid, "clip not yet active");

        // Replacing queue with a new entry for the same track
        std::string chorus_cid = graph.find_track(tid)->clips.back().id;
        auto r2 = api.queue_clip(tid, chorus_cid, "bar");
        check(r2.ok, "replace queued clip ok");
        check(api.queued_clip_for(tid) == chorus_cid, "replaced queued clip");

        // Tick at beat 0 — the bar target is at least beat bpb (>=1 bar), shouldn't fire yet
        // (metronome starts at 0, so target = bpb; at beat 0, current < target)
        runtime.tick(0.0, 0.0, 100);
        api.tick_quantized_clip_scene_launches();
        check(!api.queued_clip_for(tid).empty(), "still queued at beat 0");

        // Tick past one bar (default 4/4 = bar at beat 4; use 100 bars to guarantee fire)
        runtime.tick(400.0, 0.0, 101);  // 400 beats elapsed at 120 bpm -> well past bar boundary
        api.tick_quantized_clip_scene_launches();
        check(api.queued_clip_for(tid).empty(), "clip fired and cleared from queue");
        check(api.active_clip(tid) == chorus_cid, "chorus now active after fire");
    }

    // --- session: queue_scene instant ---
    std::fprintf(stderr, "\n--- session: queue_scene instant ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        std::string verse_cid = graph.find_track(tid)->clips[0].id;
        // Ensure Verse is assigned in verse_sid scene
        api.set_scene_assignment(verse_sid, tid, verse_cid);

        // Mutate to known-different state
        api.set_param("s1", "scale", 99.0f);
        auto r = api.queue_scene(verse_sid, "instant");
        check(r.ok, "queue_scene instant ok");
        // All assignments applied immediately
        auto* cn = runtime.compiled_graph()->find_node("s1");
        check(cn != nullptr, "s1 in graph");
        if (cn)
            check_float(cn->param_values[cn->param_indices.at("scale")], 5.0f,
                        "queue_scene instant: s1.scale=5 (Verse)");
        check(api.active_clip(tid) == verse_cid, "active_clip = Verse after instant scene");
        check(api.queued_scene_id().empty(), "no pending scene after instant");
    }

    // --- session: queue_scene beat-aligned ---
    std::fprintf(stderr, "\n--- session: queue_scene beat-aligned ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        std::string chorus_cid = graph.find_track(tid)->clips.back().id;

        // Build a Chorus scene
        api.launch_clip(tid, chorus_cid);
        auto sc_r = api.save_scene("Drop");
        check(sc_r.ok, "save Drop scene");
        std::string drop_sid = sc_r.message;

        // Queue it at bar
        api.launch_clip(tid, graph.find_track(tid)->clips[0].id); // back to Verse
        auto r = api.queue_scene(drop_sid, "bar");
        check(r.ok, "queue_scene bar ok");
        check(api.queued_scene_id() == drop_sid, "queued_scene_id returns drop_sid");

        // Doesn't fire at beat 0
        runtime.tick(0.0, 0.0, 200);
        api.tick_quantized_clip_scene_launches();
        check(!api.queued_scene_id().empty(), "scene still queued at beat 0");

        // Fires past bar boundary
        runtime.tick(800.0, 0.0, 201);
        api.tick_quantized_clip_scene_launches();
        check(api.queued_scene_id().empty(), "scene fired and cleared");
        check(api.active_clip(tid) == chorus_cid, "Drop scene applied: chorus active");
    }

    // --- session: queue_scene leaves leave_unchanged tracks alone ---
    std::fprintf(stderr, "\n--- session: queue_scene respects leave_unchanged ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        // Build a scene with leave_unchanged for the track
        auto sc_r = api.save_scene("SilentScene");
        check(sc_r.ok, "save SilentScene");
        std::string silent_sid = sc_r.message;
        api.set_scene_leave_unchanged(silent_sid, tid);

        // Set a known param value
        api.set_param("s1", "scale", 77.0f);
        auto* cn = runtime.compiled_graph()->find_node("s1");

        api.queue_scene(silent_sid, "instant");
        // s1.scale must be untouched — track is leave_unchanged
        if (cn)
            check_float(cn->param_values[cn->param_indices.at("scale")], 77.0f,
                        "leave_unchanged: s1.scale untouched");
    }

    // --- session: queue_scene cancels individual pending queue_clip for assigned track ---
    std::fprintf(stderr, "\n--- session: queue_scene cancels conflicting queue_clip ---\n");
    {
        std::string tid = graph.session().tracks[0].id;
        std::string verse_cid = graph.find_track(tid)->clips[0].id;
        std::string chorus_cid = graph.find_track(tid)->clips.back().id;
        api.set_scene_assignment(verse_sid, tid, verse_cid);

        // Queue a clip for the same track
        api.queue_clip(tid, chorus_cid, "bar");
        check(!api.queued_clip_for(tid).empty(), "clip queued");

        // Fire a scene that also assigns this track — the scene should cancel the clip queue
        api.queue_scene(verse_sid, "instant");
        check(api.queued_clip_for(tid).empty(), "queue_clip cancelled by scene fire");
    }

    // --- session: queue_scene error handling ---
    std::fprintf(stderr, "\n--- session: queue_scene error handling ---\n");
    {
        auto r1 = api.queue_scene("sc_unknown_xxx", "instant");
        check(!r1.ok, "queue_scene unknown scene fails");
        auto r2 = api.queue_scene(verse_sid, "unknown_quantize");
        check(!r2.ok, "queue_scene bad quantize fails");
        std::string tid = graph.session().tracks[0].id;
        std::string verse_cid = graph.find_track(tid)->clips[0].id;
        auto r3 = api.queue_clip(tid, "c_unknown_xxx", "instant");
        check(!r3.ok, "queue_clip unknown clip fails");
        auto r4 = api.queue_clip("tr_unknown_xxx", verse_cid, "instant");
        check(!r4.ok, "queue_clip unknown track fails");
    }

    // --- Cleanup ---
    runtime.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
