#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/runtime_api.h"
#include "runtime/operators/builtin_operators.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include "test_helpers.h"

static bool json_mode = false;

struct LatencyResult {
    std::string name;
    long long duration_us;
    long long threshold_us;
    bool passed;
    bool advisory;
};

static std::vector<LatencyResult> results;

static void check_latency(const char* name, long long duration_us, long long threshold_us, bool advisory = false) {
    bool passed = duration_us < threshold_us;
    results.push_back({name, duration_us, threshold_us, passed, advisory});

    const char* tag = passed ? "PASS" : "FAIL";
    const char* suffix = advisory ? " (advisory)" : "";
    std::fprintf(stderr, "LATENCY: %s | %lldus | threshold=%lldus | %s%s\n",
                 name, duration_us, threshold_us, tag, suffix);

    if (!passed && !advisory) {
        failures++;
    }
}

static void emit_json() {
    std::printf("[\n");
    for (size_t i = 0; i < results.size(); i++) {
        const auto& r = results[i];
        std::printf("  {\"name\":\"%s\",\"duration_us\":%lld,\"threshold_us\":%lld,\"passed\":%s,\"advisory\":%s}%s\n",
                    r.name.c_str(), r.duration_us, r.threshold_us,
                    r.passed ? "true" : "false",
                    r.advisory ? "true" : "false",
                    i + 1 < results.size() ? "," : "");
    }
    std::printf("]\n");
}

// =========================================================================
// Scenario 1: Parameter Responsiveness (threshold: 50ms)
// =========================================================================
static void scenario_param_responsiveness(vivid::OperatorRegistry& registry) {
    std::fprintf(stderr, "\n--- Scenario 1: Parameter Responsiveness ---\n");

    vivid::Graph graph;
    graph.add_node("a", "TestOp");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "s1: runtime.build()");

    // Warm-up tick
    runtime.tick(0.0, 0.016, 0);

    auto t0 = std::chrono::high_resolution_clock::now();

    auto* a_node = runtime.compiled_graph()->find_node("a");
    check(a_node != nullptr, "s1: find node a");
    if (a_node) {
        auto pi = a_node->param_indices.find("scale");
        check(pi != a_node->param_indices.end(), "s1: scale param exists");
        if (pi != a_node->param_indices.end()) {
            a_node->param_values[pi->second] = 42.0f;
        }
    }
    runtime.tick(0.0, 0.016, 1);

    auto t1 = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    if (a_node) {
        check_float(a_node->output_values[0], 84.0f, "s1: output = 42 * 2 = 84");
    }

    check_latency("param_responsiveness", us, 50000);
    runtime.shutdown();
}

// =========================================================================
// Scenario 2: Routing Responsiveness (threshold: 100ms)
// =========================================================================
static void scenario_routing_responsiveness(vivid::OperatorRegistry& registry) {
    std::fprintf(stderr, "\n--- Scenario 2: Routing Responsiveness ---\n");

    vivid::Graph graph;
    graph.add_node("a", "TestOp", {{"scale", 5.0f}});
    graph.add_node("b", "TestOp");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "s2: runtime.build()");
    vivid::AudioEngine audio_engine;
    vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);

    // Initial tick: a outputs 5*2=10
    runtime.tick(0.0, 0.016, 0);

    auto t0 = std::chrono::high_resolution_clock::now();

    auto r = api.connect("a/out", "b/scale");
    check(r.ok, "s2: connect a/out -> b/scale");
    bool has_gpu = false, has_audio = false;
    api.apply_pending(has_gpu, has_audio);
    runtime.tick(0.0, 0.016, 1);

    auto t1 = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    // b receives a's output (10.0) as scale, outputs 10*2=20
    const vivid::CompiledNode* b_node = runtime.compiled_graph()->find_node("b");
    check(b_node != nullptr, "s2: node b exists after rebuild");
    if (b_node) {
        check_float(b_node->output_values[0], 20.0f, "s2: b output = 10 * 2 = 20");
    }

    check_latency("routing_responsiveness", us, 100000);
    runtime.shutdown();
}

// =========================================================================
// Scenario 3: Compatible Hot Reload (threshold: 200ms, advisory)
// =========================================================================
static void scenario_hot_reload(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- Scenario 3: Compatible Hot Reload ---\n");

    const std::string v1_path = build_dir + "/audio_reload_v1.dylib";
    const std::string v2_path = build_dir + "/audio_reload_v2.dylib";
    const std::string audio_staging = build_dir + "/.test_latency_audio_staging";

    std::filesystem::remove_all(audio_staging);
    std::filesystem::create_directories(audio_staging);
    std::filesystem::copy_file(v1_path, audio_staging + "/audio_reload_v1.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    vivid::OperatorRegistry audio_registry;
    register_builtin_operators(audio_registry);
    check(audio_registry.scan(audio_staging.c_str()), "s3: registry.scan()");

    vivid::Graph graph;
    check(graph.add_node("audio", "AudioReloadOp", {{"level", 2.0f}}), "s3: add audio node");
    check(graph.add_node("out", "audio_out"), "s3: add audio_out");
    check(graph.add_connection("audio", "out", "out", "input"), "s3: connect audio -> out");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, audio_registry), "s3: runtime.build()");

    vivid::AudioEngine audio_engine;
    check(audio_engine.build(runtime), "s3: audio_engine.build()");

    // Verify v1 output
    {
        const uint32_t audio_frames = audio_engine.buffer_size();
        std::vector<float> output(audio_frames * 2, 0.0f);
        audio_engine.process_audio_for_test(output.data(), audio_frames);
        check_float(output[0], 4.0f, "s3: v1 output = level * 2.0 = 4.0");
    }

    // Stage v2 and measure reload
    const std::string staged_v2 = audio_staging + "/audio_reload_v2_reload_0.dylib";
    std::filesystem::copy_file(v2_path, staged_v2,
                               std::filesystem::copy_options::overwrite_existing);

    auto t0 = std::chrono::high_resolution_clock::now();

    audio_engine.pre_reload_operator("AudioReloadOp");
    check(runtime.reload_operator("AudioReloadOp", audio_registry, staged_v2),
          "s3: runtime reload");
    check(audio_engine.post_reload_operator("AudioReloadOp", audio_registry),
          "s3: audio engine reload");

    const uint32_t audio_frames = audio_engine.buffer_size();
    std::vector<float> output(audio_frames * 2, 0.0f);
    audio_engine.process_audio_for_test(output.data(), audio_frames);

    auto t1 = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    check_float(output[0], 7.0f, "s3: v2 output = 7.0");
    check_latency("hot_reload", us, 200000, /*advisory=*/true);

    audio_engine.shutdown();
    runtime.shutdown();
    std::filesystem::remove_all(audio_staging);
}

// =========================================================================
// Scenario 4: Post-Change Introspection Refresh (threshold: 100ms)
// =========================================================================
static void scenario_introspection_refresh(vivid::OperatorRegistry& registry) {
    std::fprintf(stderr, "\n--- Scenario 4: Post-Change Introspection Refresh ---\n");

    vivid::Graph graph;
    graph.add_node("a", "TestOp", {{"scale", 5.0f}});
    graph.add_node("b", "TestOp");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "s4: runtime.build()");
    vivid::AudioEngine audio_engine;
    vivid::RuntimeAPI api(graph, runtime, audio_engine, registry);

    runtime.tick(0.0, 0.016, 0);

    auto t0 = std::chrono::high_resolution_clock::now();

    auto r1 = api.add_node("TestOp", "c");
    check(r1.ok, "s4: add node c");
    auto r2 = api.connect("a/out", "c/scale");
    check(r2.ok, "s4: connect a/out -> c/scale");
    bool has_gpu = false, has_audio = false;
    api.apply_pending(has_gpu, has_audio);
    runtime.tick(0.0, 0.016, 1);

    auto inspect_r = api.inspect("c");
    auto list_r = api.list_nodes();

    auto t1 = std::chrono::high_resolution_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    check(inspect_r.ok, "s4: inspect c ok");
    check(inspect_r.message.find("TestOp") != std::string::npos, "s4: inspect shows TestOp");
    check(list_r.ok, "s4: list_nodes ok");
    check(list_r.message.find("c (TestOp)") != std::string::npos, "s4: list shows c");

    check_latency("introspection_refresh", us, 100000);
    runtime.shutdown();
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--json") {
            json_mode = true;
        } else {
            build_dir = arg;
        }
    }

    std::fprintf(stderr, "\n=== Test: Latency Validation ===\n\n");

    // --- Common setup: staging dir with test_op_v1 ---
    std::string staging = build_dir + "/.test_latency_staging";
    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
        staging + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);

    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);
    check(registry.scan(staging.c_str()), "registry.scan()");

    scenario_param_responsiveness(registry);
    scenario_routing_responsiveness(registry);
    scenario_hot_reload(build_dir);
    scenario_introspection_refresh(registry);

    // --- Cleanup ---
    std::filesystem::remove_all(staging);

    // --- Summary ---
    std::fprintf(stderr, "\n--- Latency Summary ---\n");
    for (const auto& r : results) {
        const char* tag = r.passed ? "PASS" : "FAIL";
        const char* suffix = r.advisory ? " (advisory)" : "";
        std::fprintf(stderr, "LATENCY: %s | %lldus | threshold=%lldus | %s%s\n",
                     r.name.c_str(), r.duration_us, r.threshold_us, tag, suffix);
    }

    if (json_mode) {
        emit_json();
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
