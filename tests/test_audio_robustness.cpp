// Test: audio thread exception handling.
// A throwing audio operator should be caught, produce silence, and set the
// error flag — without crashing the engine or affecting other audio nodes.
#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
#include "runtime/audio_engine.h"
#include "runtime/cadence_bridge.h"
#include "runtime/compiled_graph.h"
#include <cstdio>
#include <cmath>
#include <chrono>
#include <thread>
#include <filesystem>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::string graph_path = build_dir + "/test_audio_robustness.json";

    // Setup: staging dir with test operators
    std::string staging = build_dir + "/.test_audio_robustness_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/audio_test_op.dylib",
        staging + "/audio_test_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/audio_throwing_op.dylib",
        staging + "/audio_throwing_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Audio Thread Robustness ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");

    vivid::AudioEngine audio_engine;
    check(audio_engine.build(scheduler), "audio_engine.build()");

    // --- Test 1: Engine starts without crashing despite throwing operator ---
    std::fprintf(stderr, "\n--- start with throwing operator ---\n");
    check(audio_engine.start(true), "audio_engine.start(null device)");

    // Let the audio thread run a few buffers to trigger the exception
    scheduler.tick(0.0, 1.0 / 60.0, 0);
    scheduler.cadence_bridge().push_to_audio(*scheduler.compiled_graph());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // --- Test 2: Error state is propagated ---
    std::fprintf(stderr, "\n--- error state propagation ---\n");
    scheduler.cadence_bridge().pull_from_audio(*scheduler.compiled_graph());

    int bad_idx = audio_engine.audio_node_index("bad");
    int good_idx = audio_engine.audio_node_index("good");
    check(bad_idx >= 0, "bad node found in audio engine");
    check(good_idx >= 0, "good node found in audio engine");

    // Check that the throwing node's error was caught and flagged
    const auto& analysis = audio_engine.analysis_read();
    if (bad_idx >= 0) {
        check(analysis.errored[bad_idx], "throwing node error flag set in analysis");
        check(analysis.error_msgs[bad_idx][0] != '\0', "throwing node has error message");
        std::fprintf(stderr, "  INFO: error message = \"%s\"\n",
                     analysis.error_msgs[bad_idx].data());
    }

    // Check that the good node is NOT errored
    if (good_idx >= 0) {
        check(!analysis.errored[good_idx], "good node is not errored");
    }

    // --- Test 3: Good node still produces audio (RMS > 0) ---
    std::fprintf(stderr, "\n--- good node still produces audio ---\n");
    if (good_idx >= 0) {
        // The good node outputs constant level=0.5 (DC), so RMS should be ~0.5
        float rms = analysis.rms[good_idx];
        std::fprintf(stderr, "  INFO: good node RMS = %f\n", rms);
        check(rms > 0.1f, "good node RMS > 0.1 (still producing audio)");
    }

    // --- Test 4: Throwing node produces silence (RMS ≈ 0) ---
    std::fprintf(stderr, "\n--- throwing node produces silence ---\n");
    if (bad_idx >= 0) {
        float rms = analysis.rms[bad_idx];
        std::fprintf(stderr, "  INFO: bad node RMS = %f\n", rms);
        check(rms < 0.01f, "throwing node RMS ≈ 0 (silence)");
    }

    // --- Test 5: Error state reaches scheduler nodes ---
    std::fprintf(stderr, "\n--- error propagated to scheduler ---\n");
    bool found_bad_sched = false;
    for (const auto& ns : scheduler.compiled_graph()->nodes) {
        if (ns.node_id == "bad") {
            check(ns.errored, "scheduler 'bad' node errored");
            check(!ns.error_message.empty(), "scheduler 'bad' node has error message");
            found_bad_sched = true;
        }
        if (ns.node_id == "good") {
            check(!ns.errored, "scheduler 'good' node not errored");
        }
    }
    check(found_bad_sched, "found 'bad' node in scheduler");

    audio_engine.shutdown();

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
