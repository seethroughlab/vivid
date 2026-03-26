#include "runtime/audio_engine.h"
#include "runtime/cadence_bridge.h"
#include "runtime/compiled_graph.h"

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

    std::string staging = build_dir + "/.test_audio_float_snapshot_staging";
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
                               staging + "/test_op_v1.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/audio_float_cv_op.dylib",
                               staging + "/audio_float_cv_op.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Audio FLOAT Snapshot Contract ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");

    vivid::Graph graph;
    check(graph.add_node("ctrl", "TestOp", {{"scale", 1.0f}}), "graph.add_node(ctrl)");
    check(graph.add_node("audio", "AudioFloatCvOp"), "graph.add_node(audio)");
    check(graph.add_connection("ctrl", "out", "audio", "cv"), "graph.add_connection(ctrl/out -> audio/cv)");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");

    vivid::AudioEngine audio_engine;
    check(audio_engine.build(graph, registry, scheduler), "audio_engine.build()");

    scheduler.tick(0.0, 0.016, 0);
    scheduler.cadence_bridge().push_to_audio(*scheduler.compiled_graph());

    const int audio_idx = audio_engine.audio_node_index("audio");
    check(audio_idx >= 0, "audio node exists");

    if (audio_idx >= 0) {
        check_float(audio_engine.float_input_value_for_test(audio_idx, 0), 0.0f, 1e-5f,
                    "push_to_audio does not mutate live float input state before callback");

        float output[vivid::AudioEngine::kBufferSize * 2] = {};
        audio_engine.process_audio_for_test(output, vivid::AudioEngine::kBufferSize);

        check_float(audio_engine.float_input_value_for_test(audio_idx, 0), 2.0f, 1e-5f,
                    "audio callback applies snapshotted float input");
        check_float(audio_engine.analysis_read().rms[audio_idx], 2.0f, 0.1f,
                    "audio analysis reflects snapshotted cv");

        auto* ctrl_ns = scheduler.compiled_graph()->find_node("ctrl");
        check(ctrl_ns != nullptr, "find ctrl node");
        if (ctrl_ns) {
            ctrl_ns->output_values[0] = 9.0f;
        }

        std::fill(std::begin(output), std::end(output), 0.0f);
        audio_engine.process_audio_for_test(output, vivid::AudioEngine::kBufferSize);
        check_float(audio_engine.analysis_read().rms[audio_idx], 2.0f, 0.1f,
                    "audio analysis remains at previous snapshot without push_to_audio");

        scheduler.cadence_bridge().push_to_audio(*scheduler.compiled_graph());
        check_float(audio_engine.float_input_value_for_test(audio_idx, 0), 2.0f, 1e-5f,
                    "push_to_audio still leaves live float state untouched until callback");

        std::fill(std::begin(output), std::end(output), 0.0f);
        audio_engine.process_audio_for_test(output, vivid::AudioEngine::kBufferSize);
        check_float(audio_engine.analysis_read().rms[audio_idx], 9.0f, 0.1f,
                    "audio analysis updates after new float snapshot is published");
    }

    audio_engine.shutdown();
    scheduler.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
