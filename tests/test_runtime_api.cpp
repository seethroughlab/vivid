#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/runtime_api.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, const char* msg) {
    if (std::fabs(actual - expected) > 1e-4f) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%f)\n", msg, actual);
    }
}

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

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");

    vivid::AudioEngine audio_engine;
    bool has_gpu_ops = false;
    bool has_audio = false;

    vivid::RuntimeAPI api(graph, scheduler, audio_engine, registry);

    // --- Test set_param ---
    std::fprintf(stderr, "\n--- set_param ---\n");
    {
        auto r = api.set_param("a", "scale", 7.0f);
        check(r.ok, "set a/scale = 7.0");
        scheduler.tick(0.0, 0.016, 0);
        // a: output = scale * 2.0 = 7.0 * 2.0 = 14.0
        check_float(scheduler.nodes()[0].output_values[0], 14.0f, "a output = 14.0");
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
        scheduler.tick(0.0, 0.016, 1);

        // Find node c in rebuilt scheduler
        const vivid::NodeState* c_node = nullptr;
        for (const auto& ns : scheduler.nodes()) {
            if (ns.node_id == "c") { c_node = &ns; break; }
        }
        check(c_node != nullptr, "node c exists after rebuild");
        if (c_node) {
            check_float(c_node->output_values[0], 28.0f, "c output = 14 * 2 = 28");
        }

        // Verify a's param was preserved across rebuild
        for (const auto& ns : scheduler.nodes()) {
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
        check(scheduler.nodes().size() == 2, "back to 2 nodes");
    }

    // --- Test disconnect ---
    std::fprintf(stderr, "\n--- disconnect ---\n");
    {
        auto r = api.disconnect("a/out", "b/scale");
        check(r.ok, "disconnect a/out -> b/scale");
        api.apply_pending(has_gpu_ops, has_audio);
        // b is now standalone with scale=2.0 (its original JSON value)
        scheduler.tick(0.0, 0.016, 2);
    }

    // --- Test save + reload ---
    std::fprintf(stderr, "\n--- save + reload ---\n");
    {
        std::string save_path = build_dir + "/test_api_saved.json";
        auto r1 = api.save_as(save_path);
        check(r1.ok, "save_as");
        check(graph.source_path() == save_path, "save_as updates graph source_path");

        // Verify the saved file can be loaded
        vivid::Graph g2;
        check(g2.load(save_path.c_str()), "reload saved graph");
        check(g2.nodes().size() == 2, "saved graph has 2 nodes");

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
        auto* ns = scheduler.find_node_mut("a");
        check(ns != nullptr, "scheduler has node a");
        if (ns) {
            check(ns->gpu_tex_width == 1920, "NodeState gpu_tex_width = 1920");
            check(ns->gpu_tex_height == 1080, "NodeState gpu_tex_height = 1080");
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
        vivid::Scheduler s2;
        check(s2.build(g2, registry), "save test: build scheduler");
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
        vivid::Scheduler s_empty;
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
        vivid::Scheduler s2;
        check(s2.build(g2, registry), "reload test: build scheduler");
        vivid::AudioEngine ae2;
        vivid::RuntimeAPI api2(g2, s2, ae2, registry);

        // Add a node to make 3
        api2.add_node("TestOp", "extra");
        bool hgpu = false, haudio = false;
        api2.apply_pending(hgpu, haudio);
        check(s2.nodes().size() == 3, "reload test: 3 nodes after add");

        // Reload from disk → back to 2
        auto r = api2.reload(hgpu, haudio);
        check(r.ok, "reload() succeeds");
        check(s2.nodes().size() == 2, "reload test: back to 2 nodes");

        s2.shutdown();
        std::filesystem::remove(tmp_path);
    }

    // --- Test reload() no source_path ---
    {
        vivid::Graph g_empty;
        vivid::Scheduler s_empty;
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
        scheduler.tick(0.0, 0.016, 3);

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
        check(scheduler.nodes().size() == 2,
              "reload failure regression: scheduler restored after failed reload");

        const vivid::NodeState* a_node = nullptr;
        for (const auto& ns : scheduler.nodes()) {
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
        vivid::Scheduler s_switch;
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

        const vivid::NodeState* a_node = nullptr;
        for (const auto& ns : s_switch.nodes()) {
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
        check(scheduler.nodes().size() == 2,
              "snapshot malformed regression: scheduler restored after failed apply");
        check(api.graph_dirty(),
              "snapshot malformed regression: dirty state preserved after failed apply");

        const vivid::NodeState* a_node = nullptr;
        for (const auto& ns : scheduler.nodes()) {
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
        vivid::Scheduler s_dirty;
        check(s_dirty.build(g_dirty, registry), "undo-style snapshot regression: build scheduler");
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
        vivid::Scheduler s_saved;
        check(s_saved.build(g_saved, registry), "snapshot source_path regression: build saved scheduler");
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
        vivid::Scheduler s_unsaved;
        check(s_unsaved.build(g_unsaved, registry), "snapshot source_path regression: build unsaved scheduler");
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
        vivid::Scheduler s2;
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
        vivid::Scheduler s2;
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

    // --- Test save_variation + recall round-trip ---
    std::fprintf(stderr, "\n--- save_variation + recall ---\n");
    float original_a_scale;
    {
        // Read current a/scale value
        auto r0 = api.get_param("a", "scale");
        check(r0.ok, "get a/scale before save");
        original_a_scale = std::stof(r0.message);

        auto r1 = api.save_variation("A");
        check(r1.ok, "save_variation A");
        check(!api.variation_dirty(), "not dirty after save");

        // Modify a param
        api.set_param("a", "scale", 99.0f);
        check(api.variation_dirty(), "dirty after set_param");

        auto r2 = api.save_variation("B");
        check(r2.ok, "save_variation B");

        // Recall A — should restore original value
        auto r3 = api.recall_variation("A");
        check(r3.ok, "recall_variation A");
        auto r4 = api.get_param("a", "scale");
        check(r4.ok, "get a/scale after recall A");
        check_float(std::stof(r4.message), original_a_scale, "a/scale restored to original");

        // Recall B — should have 99.0
        auto r5 = api.recall_variation("B");
        check(r5.ok, "recall_variation B");
        auto r6 = api.get_param("a", "scale");
        check(r6.ok, "get a/scale after recall B");
        check_float(std::stof(r6.message), 99.0f, "a/scale = 99.0 from variation B");
    }

    // --- Test recall_variation_idx ---
    std::fprintf(stderr, "\n--- recall_variation_idx ---\n");
    {
        auto r1 = api.recall_variation_idx(0);
        check(r1.ok, "recall_variation_idx(0) ok");

        auto r2 = api.recall_variation_idx(99);
        check(!r2.ok, "recall_variation_idx(99) fails");
    }

    // --- Test list_variations ---
    std::fprintf(stderr, "\n--- list_variations ---\n");
    {
        auto r = api.list_variations();
        check(r.ok, "list_variations ok");
        check(r.message.find("A") != std::string::npos, "list contains A");
        check(r.message.find("B") != std::string::npos, "list contains B");
    }

    // --- Test update_variation ---
    std::fprintf(stderr, "\n--- update_variation ---\n");
    {
        // Recall A, modify, update
        api.recall_variation("A");
        api.set_param("a", "scale", 55.0f);
        auto r1 = api.update_variation("A");
        check(r1.ok, "update_variation A ok");

        // Recall B, then A — verify A has 55.0
        api.recall_variation("B");
        api.recall_variation("A");
        auto r2 = api.get_param("a", "scale");
        check(r2.ok, "get a/scale after update+recall A");
        check_float(std::stof(r2.message), 55.0f, "a/scale = 55.0 after update");
    }

    // --- Test rename_variation ---
    std::fprintf(stderr, "\n--- rename_variation ---\n");
    {
        auto r1 = api.rename_variation("A", "Intro");
        check(r1.ok, "rename A -> Intro ok");

        auto r2 = api.recall_variation("Intro");
        check(r2.ok, "recall Intro (renamed from A) ok");

        auto r3 = api.recall_variation("A");
        check(!r3.ok, "recall A (old name) fails");

        auto r4 = api.rename_variation("nope", "x");
        check(!r4.ok, "rename non-existent fails");
    }

    // --- Test remove_variation ---
    std::fprintf(stderr, "\n--- remove_variation ---\n");
    {
        auto r1 = api.remove_variation("B");
        check(r1.ok, "remove_variation B ok");

        auto r2 = api.remove_variation("B");
        check(!r2.ok, "remove_variation B again fails");
    }

    // --- Test queue_variation (instant) ---
    std::fprintf(stderr, "\n--- queue_variation ---\n");
    {
        auto r = api.queue_variation("Intro", "instant");
        check(r.ok, "queue_variation Intro instant ok");
    }

    // --- Test set_quantize_clock ---
    std::fprintf(stderr, "\n--- set_quantize_clock ---\n");
    {
        auto r = api.set_quantize_clock("a");
        check(r.ok, "set_quantize_clock a ok");
    }

    // --- Test recall after node removal ---
    std::fprintf(stderr, "\n--- recall after node removal ---\n");
    {
        // Save a variation capturing both nodes
        api.save_variation("PreRemove");

        // Add a temporary node, save variation with it, then remove it
        api.add_node("TestOp", "tmp_var_node");
        api.apply_pending(has_gpu_ops, has_audio);
        api.save_variation("WithTmp");
        api.remove_node("tmp_var_node");
        api.apply_pending(has_gpu_ops, has_audio);

        // Recall the variation that included the now-removed node — should not crash
        auto r = api.recall_variation("WithTmp");
        check(r.ok, "recall variation with missing node does not crash");

        // Cleanup: remove test variations
        api.remove_variation("PreRemove");
        api.remove_variation("WithTmp");
    }

    // --- Cleanup ---
    scheduler.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
