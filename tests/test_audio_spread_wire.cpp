#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
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

    std::string graph_path = build_dir + "/test_audio_spread_wire.json";

    // Setup: staging dir with required operators
    std::string staging = build_dir + "/.test_audio_spread_wire_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/spread_source_op.dylib",
        staging + "/spread_source_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/audio_spread_op.dylib",
        staging + "/audio_spread_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/spread_adsr.dylib",
        staging + "/spread_adsr.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Audio Spread Wire ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    vivid::AudioEngine audio_engine;

    // --- Test 1: Build succeeds (audio spread wire created) ---
    std::fprintf(stderr, "\n--- build ---\n");
    check(audio_engine.build(runtime), "audio_engine.build()");

    int recv_idx = audio_engine.audio_node_index("recv");
    check(recv_idx >= 0, "recv node found in engine");

    int adsr_idx = audio_engine.audio_node_index("adsr");
    check(adsr_idx >= 0, "adsr node found in engine");

    // --- Test 2: After ~100ms, SpreadADSR envelopes reach sustain ---
    std::fprintf(stderr, "\n--- envelopes reach sustain ---\n");
    check(audio_engine.start(true), "audio_engine.start(null)");

    // Tick runtime so SpreadSourceOp produces spread [1,2,3] (all > 0.5 = gate on)
    runtime.tick(0.0, 0.016, 0);
    runtime.cadence_bridge().push_to_audio(*runtime.compiled_graph());

    // Poll for audio signal: RMS should be ~2.4 (3 * 0.8 sustain)
    bool got_signal = false;
    for (int i = 0; i < 400; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        runtime.cadence_bridge().pull_from_audio(*runtime.compiled_graph());
            const auto& snap = audio_engine.analysis_read();
            if (recv_idx >= 0 && snap.rms[recv_idx] > 2.0f) {
            got_signal = true;
            break;
        }
    }
    check(got_signal, "audio callback produced signal from SpreadADSR → AudioSpreadOp");
    {
        const auto& snap = audio_engine.analysis_read();
        // 3 slots × sustain(0.8) = 2.4, but envelope may still be decaying → allow wider range
        check(snap.rms[recv_idx] > 2.0f && snap.rms[recv_idx] < 3.1f,
              "RMS in range [2.0, 3.1] (3 slots × ~0.8 sustain)");
    }

    // --- Test 3: Set count=0 → gates disappear, envelopes release ---
    std::fprintf(stderr, "\n--- gates disappear → release ---\n");
    {
        auto* src_ns = runtime.compiled_graph()->find_node("src");
        check(src_ns != nullptr, "find src node");
        if (src_ns) {
            auto pi = src_ns->param_indices.find("count");
            if (pi != src_ns->param_indices.end()) {
                src_ns->param_values[pi->second] = 0.0f;
            }
        }
        runtime.tick(0.1, 0.016, 1);
        runtime.cadence_bridge().push_to_audio(*runtime.compiled_graph());

        // Poll for RMS to drop to ~0
        bool zeroed = false;
        for (int i = 0; i < 400; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            runtime.cadence_bridge().pull_from_audio(*runtime.compiled_graph());
            const auto& snap = audio_engine.analysis_read();
            if (recv_idx >= 0 && snap.rms[recv_idx] < 0.3f) {
                zeroed = true;
                break;
            }
        }
        check(zeroed, "RMS dropped to ~0 after gates removed");
    }

    // --- Test 4: Set count=3 again → envelopes re-attack ---
    std::fprintf(stderr, "\n--- re-attack ---\n");
    {
        auto* src_ns = runtime.compiled_graph()->find_node("src");
        if (src_ns) {
            auto pi = src_ns->param_indices.find("count");
            if (pi != src_ns->param_indices.end()) {
                src_ns->param_values[pi->second] = 3.0f;
            }
        }
        runtime.tick(0.5, 0.016, 2);
        runtime.cadence_bridge().push_to_audio(*runtime.compiled_graph());

        // Poll for RMS to recover
        bool recovered = false;
        for (int i = 0; i < 400; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            runtime.cadence_bridge().pull_from_audio(*runtime.compiled_graph());
            const auto& snap = audio_engine.analysis_read();
            if (recv_idx >= 0 && snap.rms[recv_idx] > 2.0f) {
                recovered = true;
                break;
            }
        }
        check(recovered, "RMS recovered after gates restored");
        {
            const auto& snap = audio_engine.analysis_read();
            check(snap.rms[recv_idx] > 2.0f && snap.rms[recv_idx] < 3.1f,
                  "RMS in range [2.0, 3.1] after re-attack");
        }
    }

    // --- Cleanup ---
    std::fprintf(stderr, "\n--- shutdown ---\n");
    audio_engine.shutdown();
    check(true, "shutdown() no crash");
    runtime.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
