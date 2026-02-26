#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/runtime_api.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
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

        api2.set_node_layout("a", 42.0f, 99.0f);
        api2.save_as(tmp_path);

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

    // --- Cleanup ---
    scheduler.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
