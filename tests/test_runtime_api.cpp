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

    // --- Cleanup ---
    scheduler.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
