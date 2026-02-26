#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <thread>
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

static void check_float(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%f)\n", msg, actual);
    }
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string graph_path = build_dir + "/test_cross_domain_spread.json";

    // Setup: staging dir with required operators
    std::string staging = build_dir + "/.test_spread_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/spread_source_op.dylib",
        staging + "/spread_source_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/audio_spread_op.dylib",
        staging + "/audio_spread_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Cross-Domain Spread ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");

    vivid::AudioEngine audio_engine;

    // --- Test 1: Build succeeds ---
    std::fprintf(stderr, "\n--- build ---\n");
    check(audio_engine.build(graph, registry, scheduler), "audio_engine.build()");

    int audio_idx = audio_engine.audio_node_index("audio");
    check(audio_idx >= 0, "audio node found in engine");

    // --- Test 2: Start and verify spread arrives in audio domain ---
    std::fprintf(stderr, "\n--- spread arrives in audio domain ---\n");
    check(audio_engine.start(true), "audio_engine.start(null)");

    // Tick scheduler so SpreadSourceOp produces spread [1,2,3]
    scheduler.tick(0.0, 0.016, 0);
    audio_engine.push_params(scheduler);

    // Poll for audio signal: RMS should be ~6.0 (sum of [1,2,3])
    bool got_signal = false;
    for (int i = 0; i < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        audio_engine.inject_analysis(scheduler);
        const auto& snap = audio_engine.analysis_read();
        if (audio_idx >= 0 && snap.rms[audio_idx] > 5.0f) {
            got_signal = true;
            break;
        }
    }
    check(got_signal, "audio callback produced signal from spread");
    {
        const auto& snap = audio_engine.analysis_read();
        check_float(snap.rms[audio_idx], 6.0f, 0.5f, "RMS ≈ 6.0 (sum of [1,2,3])");
    }

    // --- Test 3: Spread echoed back to control domain ---
    std::fprintf(stderr, "\n--- spread echoed back ---\n");
    {
        // Find the scheduler node for "audio" and check its "echo" output spread
        const vivid::NodeState* audio_ns = nullptr;
        for (const auto& ns : scheduler.nodes()) {
            if (ns.node_id == "audio") { audio_ns = &ns; break; }
        }
        check(audio_ns != nullptr, "audio node found in scheduler");

        if (audio_ns) {
            auto echo_it = audio_ns->output_port_indices.find("echo");
            check(echo_it != audio_ns->output_port_indices.end(), "echo port exists");

            if (echo_it != audio_ns->output_port_indices.end()) {
                uint32_t echo_idx = echo_it->second;
                const auto& spread = audio_ns->output_spreads[echo_idx];
                check(spread.size() == 3, "echo spread has 3 elements");
                if (spread.size() >= 3) {
                    check_float(spread[0], 1.0f, 0.01f, "echo[0] = 1.0");
                    check_float(spread[1], 2.0f, 0.01f, "echo[1] = 2.0");
                    check_float(spread[2], 3.0f, 0.01f, "echo[2] = 3.0");
                }
            }
        }
    }

    // --- Test 4: Spread update propagates ---
    std::fprintf(stderr, "\n--- spread update propagates ---\n");
    {
        // Change base to 2.0 → spread should become [2,4,6], sum=12
        auto* src_ns = scheduler.find_node_mut("src");
        check(src_ns != nullptr, "find src node");
        if (src_ns) {
            auto pi = src_ns->param_indices.find("base");
            if (pi != src_ns->param_indices.end()) {
                src_ns->param_values[pi->second] = 2.0f;
            }
        }
        scheduler.tick(0.0, 0.016, 1);
        audio_engine.push_params(scheduler);

        // Poll for updated RMS
        bool updated = false;
        for (int i = 0; i < 200; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            audio_engine.inject_analysis(scheduler);
            const auto& snap = audio_engine.analysis_read();
            if (audio_idx >= 0 && snap.rms[audio_idx] > 11.0f) {
                updated = true;
                break;
            }
        }
        check(updated, "RMS updated after base change");

        const auto& snap = audio_engine.analysis_read();
        check_float(snap.rms[audio_idx], 12.0f, 1.0f, "RMS ≈ 12.0 (sum of [2,4,6])");

        // Check echo spread updated
        const vivid::NodeState* audio_ns = nullptr;
        for (const auto& ns : scheduler.nodes()) {
            if (ns.node_id == "audio") { audio_ns = &ns; break; }
        }
        if (audio_ns) {
            auto echo_it = audio_ns->output_port_indices.find("echo");
            if (echo_it != audio_ns->output_port_indices.end()) {
                const auto& spread = audio_ns->output_spreads[echo_it->second];
                check(spread.size() == 3, "echo spread still has 3 elements");
                if (spread.size() >= 3) {
                    check_float(spread[0], 2.0f, 0.01f, "echo[0] = 2.0");
                    check_float(spread[1], 4.0f, 0.01f, "echo[1] = 4.0");
                    check_float(spread[2], 6.0f, 0.01f, "echo[2] = 6.0");
                }
            }
        }
    }

    // --- Test 5: Empty spread ---
    std::fprintf(stderr, "\n--- empty spread ---\n");
    {
        // Set count to 0 → empty spread, sum=0
        auto* src_ns = scheduler.find_node_mut("src");
        if (src_ns) {
            auto pi = src_ns->param_indices.find("count");
            if (pi != src_ns->param_indices.end()) {
                src_ns->param_values[pi->second] = 0.0f;
            }
        }
        scheduler.tick(0.0, 0.016, 2);
        audio_engine.push_params(scheduler);

        // Poll for RMS to drop to ~0
        bool zeroed = false;
        for (int i = 0; i < 200; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            audio_engine.inject_analysis(scheduler);
            const auto& snap = audio_engine.analysis_read();
            if (audio_idx >= 0 && snap.rms[audio_idx] < 0.5f) {
                zeroed = true;
                break;
            }
        }
        check(zeroed, "RMS dropped to ~0 with empty spread");

        const auto& snap = audio_engine.analysis_read();
        check_float(snap.rms[audio_idx], 0.0f, 0.5f, "RMS ≈ 0.0 (empty spread)");
    }

    // --- Cleanup ---
    std::fprintf(stderr, "\n--- shutdown ---\n");
    audio_engine.shutdown();
    check(true, "shutdown() no crash");
    scheduler.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
