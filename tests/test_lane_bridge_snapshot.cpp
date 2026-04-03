#include "runtime/operators/operator_registry.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/audio/audio_frame_bridge.h"
#include "runtime/graph/compiled_graph.h"
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

    std::string graph_path = build_dir + "/test_lane_bridge_snapshot.json";

    // Setup: staging dir with required operators
    std::string staging = build_dir + "/.test_lane_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/lane_source_op.dylib",
        staging + "/lane_source_op.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/audio_lane_op.dylib",
        staging + "/audio_lane_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Lane Bridge Snapshot ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.load(graph_path.c_str()), "graph.load()");

    vivid::RuntimeCore runtime;
    check(runtime.build(graph, registry), "runtime.build()");

    vivid::AudioEngine audio_engine;

    // --- Test 1: Build succeeds ---
    std::fprintf(stderr, "\n--- build ---\n");
    check(audio_engine.build(runtime), "audio_engine.build()");

    int audio_idx = audio_engine.audio_node_index("audio");
    check(audio_idx >= 0, "audio node found in engine");

    // --- Test 2: Start and verify lane array arrives through the bridge ---
    std::fprintf(stderr, "\n--- lane array arrives through the audio-frame bridge ---\n");
    check(audio_engine.start(true), "audio_engine.start(null)");

    // Tick runtime so LaneSourceOp produces lane array [1,2,3]
    runtime.tick(0.0, 0.016, 0);
    runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());

    // Poll for audio signal: RMS should be ~6.0 (sum of [1,2,3])
    bool got_signal = false;
    for (int i = 0; i < 200; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        runtime.audio_frame_bridge().pull_from_audio(*runtime.compiled_graph());
            const auto& snap = audio_engine.analysis_read();
            if (audio_idx >= 0 && snap.rms[audio_idx] > 5.0f) {
            got_signal = true;
            break;
        }
    }
    check(got_signal, "audio callback produced signal from lane array");
    {
        const auto& snap = audio_engine.analysis_read();
        check_float(snap.rms[audio_idx], 6.0f, 0.5f, "RMS ≈ 6.0 (sum of [1,2,3])");
    }

    // --- Test 3: Lane array echoed back to the frame world ---
    std::fprintf(stderr, "\n--- lane array echoed back ---\n");
    {
        // Find the runtime node for "audio" and check its "echo" output lane array
        const vivid::CompiledNode* audio_ns = runtime.compiled_graph()->find_node("audio");
        check(audio_ns != nullptr, "audio node found in runtime");

        if (audio_ns) {
            auto echo_it = audio_ns->output_port_indices.find("echo");
            check(echo_it != audio_ns->output_port_indices.end(), "echo port exists");

            if (echo_it != audio_ns->output_port_indices.end()) {
                uint32_t echo_idx = echo_it->second;
                const auto& lane_array = audio_ns->output_lanes[echo_idx];
                check(lane_array.size() == 3, "echo lane array has 3 elements");
                if (lane_array.size() >= 3) {
                    check_float(lane_array[0], 1.0f, 0.01f, "echo[0] = 1.0");
                    check_float(lane_array[1], 2.0f, 0.01f, "echo[1] = 2.0");
                    check_float(lane_array[2], 3.0f, 0.01f, "echo[2] = 3.0");
                }
            }
        }
    }

    // --- Test 4: Lane-array update propagates ---
    std::fprintf(stderr, "\n--- lane-array update propagates ---\n");
    {
        // Change base to 2.0 → lane array should become [2,4,6], sum=12
        auto* src_ns = runtime.compiled_graph()->find_node("src");
        check(src_ns != nullptr, "find src node");
        if (src_ns) {
            auto pi = src_ns->param_indices.find("base");
            if (pi != src_ns->param_indices.end()) {
                src_ns->param_values[pi->second] = 2.0f;
            }
        }
        runtime.tick(0.0, 0.016, 1);
        runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());

        // Poll for updated RMS
        bool updated = false;
        for (int i = 0; i < 200; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            runtime.audio_frame_bridge().pull_from_audio(*runtime.compiled_graph());
            const auto& snap = audio_engine.analysis_read();
            if (audio_idx >= 0 && snap.rms[audio_idx] > 11.0f) {
                updated = true;
                break;
            }
        }
        check(updated, "RMS updated after base change");

        const auto& snap = audio_engine.analysis_read();
        check_float(snap.rms[audio_idx], 12.0f, 1.0f, "RMS ≈ 12.0 (sum of [2,4,6])");

        // Check echo lane array updated
        const vivid::CompiledNode* audio_ns = runtime.compiled_graph()->find_node("audio");
        if (audio_ns) {
            auto echo_it = audio_ns->output_port_indices.find("echo");
            if (echo_it != audio_ns->output_port_indices.end()) {
                const auto& lane_array = audio_ns->output_lanes[echo_it->second];
                check(lane_array.size() == 3, "echo lane array still has 3 elements");
                if (lane_array.size() >= 3) {
                    check_float(lane_array[0], 2.0f, 0.01f, "echo[0] = 2.0");
                    check_float(lane_array[1], 4.0f, 0.01f, "echo[1] = 4.0");
                    check_float(lane_array[2], 6.0f, 0.01f, "echo[2] = 6.0");
                }
            }
        }
    }

    // --- Test 5: Empty lane array ---
    std::fprintf(stderr, "\n--- empty lane array ---\n");
    {
        // Set count to 0 → empty lane array, sum=0
        auto* src_ns = runtime.compiled_graph()->find_node("src");
        if (src_ns) {
            auto pi = src_ns->param_indices.find("count");
            if (pi != src_ns->param_indices.end()) {
                src_ns->param_values[pi->second] = 0.0f;
            }
        }
        runtime.tick(0.0, 0.016, 2);
        runtime.audio_frame_bridge().push_to_audio(*runtime.compiled_graph());

        // Poll for RMS to drop to ~0
        bool zeroed = false;
        for (int i = 0; i < 200; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            runtime.audio_frame_bridge().pull_from_audio(*runtime.compiled_graph());
            const auto& snap = audio_engine.analysis_read();
            if (audio_idx >= 0 && snap.rms[audio_idx] < 0.5f) {
                zeroed = true;
                break;
            }
        }
        check(zeroed, "RMS dropped to ~0 with empty lane array");

        const auto& snap = audio_engine.analysis_read();
        check_float(snap.rms[audio_idx], 0.0f, 0.5f, "RMS ≈ 0.0 (empty lane array)");
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
