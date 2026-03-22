#include "runtime/audio_engine.h"

#include "runtime/builtin_operators.h"
#include "runtime/graph.h"
#include "runtime/operator_registry.h"
#include "runtime/scheduler.h"
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

static float first_sample_after_process(vivid::AudioEngine& audio_engine) {
    float output[vivid::AudioEngine::kBufferSize * 2] = {};
    audio_engine.process_audio_for_test(output, vivid::AudioEngine::kBufferSize);
    return output[0];
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    const std::string v1_path = build_dir + "/audio_reload_v1.dylib";
    const std::string v2_path = build_dir + "/audio_reload_v2.dylib";
    const std::string bad_path = build_dir + "/audio_reload_incompatible.dylib";
    const std::string staging = build_dir + "/.test_audio_hot_reload_staging";

    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(v1_path, staging + "/audio_reload_v1.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Audio Hot Reload Safety ===\n\n");

    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);
    check(registry.scan(staging.c_str()), "registry.scan()");
    check(registry.find("AudioReloadOp") != nullptr, "AudioReloadOp found");
    check(registry.find("audio_out") != nullptr, "audio_out builtin found");

    vivid::Graph graph;
    check(graph.add_node("audio", "AudioReloadOp", {{"level", 2.0f}}), "graph.add_node(audio)");
    check(graph.add_node("out", "audio_out"), "graph.add_node(out)");
    check(graph.add_connection("audio", "out", "out", "input"),
          "graph.add_connection(audio/out -> out/input)");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");

    vivid::AudioEngine audio_engine;
    check(audio_engine.build(graph, registry, scheduler), "audio_engine.build()");

    std::fprintf(stderr, "\n--- initial v1 processing ---\n");
    check_float(first_sample_after_process(audio_engine), 4.0f, 1e-4f,
                "v1 audio output = level * 2.0");

    std::fprintf(stderr, "\n--- reload compatible v2 ---\n");
    const std::string staged_v2 = staging + "/audio_reload_v2_reload_0.dylib";
    std::filesystem::copy_file(v2_path, staged_v2,
                               std::filesystem::copy_options::overwrite_existing);
    audio_engine.pre_reload_operator("AudioReloadOp");
    check(scheduler.reload_operator("AudioReloadOp", registry, staged_v2),
          "scheduler reload succeeds for compatible audio operator");
    check(audio_engine.post_reload_operator("AudioReloadOp", registry),
          "audio engine reload succeeds for compatible audio operator");
    check_float(first_sample_after_process(audio_engine), 7.0f, 1e-4f,
                "v2 audio output preserves level and applies new offset default");

    std::fprintf(stderr, "\n--- reject incompatible audio reload ---\n");
    const std::string staged_bad = staging + "/audio_reload_bad_reload_0.dylib";
    std::filesystem::copy_file(bad_path, staged_bad,
                               std::filesystem::copy_options::overwrite_existing);
    audio_engine.pre_reload_operator("AudioReloadOp");
    check(!scheduler.reload_operator("AudioReloadOp", registry, staged_bad),
          "scheduler rejects incompatible audio operator reload");
    // Recreate instances from old (still-loaded) dylib after rejected reload
    check(audio_engine.post_reload_operator("AudioReloadOp", registry),
          "audio engine recovers after rejected reload");
    check_float(first_sample_after_process(audio_engine), 7.0f, 1e-4f,
                "previous audio operator remains active after rejected reload");

    audio_engine.shutdown();
    scheduler.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
