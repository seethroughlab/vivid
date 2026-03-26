#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/cadence_bridge.h"
#include "runtime/compiled_graph.h"
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

    std::string graph_path = build_dir + "/test_audio_engine.json";

    // Setup: staging dir with test_op_v1 and audio_test_op
    std::string staging = build_dir + "/.test_audio_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
        staging + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/audio_test_op.dylib",
        staging + "/audio_test_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: AudioEngine ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");

    vivid::AudioEngine audio_engine;

    // --- Test 1: build() ---
    std::fprintf(stderr, "\n--- build ---\n");
    check(audio_engine.build(graph, registry, scheduler), "audio_engine.build()");

    int src_idx = audio_engine.audio_node_index("src");
    int dst_idx = audio_engine.audio_node_index("dst");
    check(src_idx >= 0, "src audio node found");
    check(dst_idx >= 0, "dst audio node found");
    check(src_idx != dst_idx, "src and dst are distinct");

    // --- Test 2: start(null device) ---
    std::fprintf(stderr, "\n--- start (null device) ---\n");
    check(audio_engine.start(true), "audio_engine.start(null)");

    // --- Test 3: Initial audio processing ---
    std::fprintf(stderr, "\n--- initial processing ---\n");
    {
        // Tick the scheduler so ctrl produces output (scale=0.8 → output=1.6)
        scheduler.tick(0.0, 0.016, 0);
        scheduler.cadence_bridge().push_to_audio(*scheduler.compiled_graph());

        // Poll for analysis results
        bool got_signal = false;
        for (int i = 0; i < 200; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            scheduler.cadence_bridge().pull_from_audio(*scheduler.compiled_graph());
            scheduler.sync_to_nodestate();
            const auto& snap = audio_engine.analysis_read();
            if (src_idx >= 0 && snap.rms[src_idx] > 0.01f) {
                got_signal = true;
                break;
            }
        }
        check(got_signal, "audio callback produced signal");

        const auto& snap = audio_engine.analysis_read();
        // src: no input, level=0.5 → output = 0.5 DC → RMS ≈ 0.5
        check_float(snap.rms[src_idx], 0.5f, 0.05f, "src RMS ≈ 0.5");
        check_float(snap.peak[src_idx], 0.5f, 0.05f, "src peak ≈ 0.5");

        // dst: input=0.5 from src, level overridden by cross-cadence wire = 1.6
        //      output = 0.5 + 1.6 = 2.1 DC → RMS ≈ 2.1
        check_float(snap.rms[dst_idx], 2.1f, 0.15f, "dst RMS ≈ 2.1");
        check_float(snap.peak[dst_idx], 2.1f, 0.15f, "dst peak ≈ 2.1");
    }

    // --- Test 4: Parameter update ---
    std::fprintf(stderr, "\n--- parameter update ---\n");
    {
        // Change ctrl/scale to 2.0 → output = 2.0 * 2.0 = 4.0
        // dst output = src(0.5) + ctrl(4.0) = 4.5
        auto* ctrl_ns = scheduler.find_node_mut("ctrl");
        check(ctrl_ns != nullptr, "find ctrl node");
        if (ctrl_ns) {
            auto pi = ctrl_ns->param_indices.find("scale");
            if (pi != ctrl_ns->param_indices.end()) {
                ctrl_ns->param_values[pi->second] = 2.0f;
                scheduler.sync_node_to_compiled("ctrl");
            }
        }
        scheduler.tick(0.0, 0.016, 1);
        scheduler.cadence_bridge().push_to_audio(*scheduler.compiled_graph());

        // Poll for updated analysis
        bool updated = false;
        for (int i = 0; i < 200; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            scheduler.cadence_bridge().pull_from_audio(*scheduler.compiled_graph());
            scheduler.sync_to_nodestate();
            const auto& snap = audio_engine.analysis_read();
            if (dst_idx >= 0 && snap.rms[dst_idx] > 4.0f) {
                updated = true;
                break;
            }
        }
        check(updated, "analysis updated after param change");

        const auto& snap = audio_engine.analysis_read();
        check_float(snap.rms[src_idx], 0.5f, 0.05f, "src RMS still ≈ 0.5");
        check_float(snap.rms[dst_idx], 4.5f, 0.25f, "dst RMS ≈ 4.5 after ctrl change");
    }

    // --- Test 5: pause/resume ---
    std::fprintf(stderr, "\n--- pause/resume ---\n");
    {
        audio_engine.pause();
        check(true, "pause() no crash");

        audio_engine.resume();
        check(true, "resume() no crash");

        // Verify audio resumes by checking for signal
        bool resumed = false;
        for (int i = 0; i < 200; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            scheduler.cadence_bridge().pull_from_audio(*scheduler.compiled_graph());
            scheduler.sync_to_nodestate();
            const auto& snap = audio_engine.analysis_read();
            if (src_idx >= 0 && snap.rms[src_idx] > 0.01f) {
                resumed = true;
                break;
            }
        }
        check(resumed, "audio resumes after pause/resume");
    }

    // --- Test 6: audio_node_index ---
    std::fprintf(stderr, "\n--- audio_node_index ---\n");
    {
        check(audio_engine.audio_node_index("src") >= 0, "index for src exists");
        check(audio_engine.audio_node_index("dst") >= 0, "index for dst exists");
        check(audio_engine.audio_node_index("ctrl") == -1, "ctrl is not an audio node");
        check(audio_engine.audio_node_index("nonexistent") == -1, "nonexistent returns -1");
    }

    // --- Test 7: shutdown ---
    std::fprintf(stderr, "\n--- shutdown ---\n");
    audio_engine.shutdown();
    check(true, "shutdown() no crash");

    // --- Cleanup ---
    scheduler.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
