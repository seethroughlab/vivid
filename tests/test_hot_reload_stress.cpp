#include "runtime/audio_engine.h"
#include "runtime/builtin_operators.h"
#include "runtime/compiled_graph.h"
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
    const std::string v3_path = build_dir + "/audio_reload_v3.dylib";
    const std::string bad_path = build_dir + "/audio_reload_incompatible.dylib";
    const std::string staging = build_dir + "/.test_hot_reload_stress_staging";
    const std::string active_path = staging + "/audio_reload_active.dylib";
    const std::string rejected_path = staging + "/audio_reload_rejected.dylib";

    std::filesystem::remove_all(staging);
    std::filesystem::create_directories(staging);
    std::filesystem::copy_file(v1_path, active_path, std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Hot Reload Stress ===\n\n");
    std::fprintf(stderr, "[hot_reload_stress] staging files ready\n");

    vivid::OperatorRegistry registry;
    std::fprintf(stderr, "[hot_reload_stress] registry constructed\n");
    register_builtin_operators(registry);
    std::fprintf(stderr, "[hot_reload_stress] builtins registered\n");
    check(registry.scan(staging.c_str()), "registry.scan()");
    std::fprintf(stderr, "[hot_reload_stress] registry scanned\n");
    check(registry.find("AudioReloadOp") != nullptr, "AudioReloadOp found");

    vivid::Graph graph;
    check(graph.add_node("audio", "AudioReloadOp", {{"level", 2.0f}}), "graph.add_node(audio)");
    check(graph.add_node("out", "audio_out"), "graph.add_node(out)");
    check(graph.add_connection("audio", "out", "out", "input"), "graph.add_connection(audio->out)");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");

    vivid::AudioEngine audio_engine;
    check(audio_engine.build(graph, registry, scheduler), "audio_engine.build()");

    bool active_is_v2_family = false;
    float expected_sample = 4.0f;
    check_float(first_sample_after_process(audio_engine), expected_sample, 1e-4f,
                "initial v1 output");

    for (int i = 0; i < 8; ++i) {
        std::fprintf(stderr, "\n--- hot reload cycle %d ---\n", i + 1);
        if ((i % 2) == 1) {
            const std::string staged_bad = staging + "/audio_reload_bad_" + std::to_string(i) + ".dylib";
            std::filesystem::copy_file(bad_path, staged_bad,
                                       std::filesystem::copy_options::overwrite_existing);
            audio_engine.pre_reload_operator("AudioReloadOp");
            check(!scheduler.reload_operator("AudioReloadOp", registry, staged_bad),
                  "incompatible reload rejected");
            audio_engine.post_reload_operator("AudioReloadOp", registry);
            check_float(first_sample_after_process(audio_engine), expected_sample, 1e-4f,
                        "rejected reload preserves active audio operator");
            auto failures_for_dir = registry.loader_failure_diagnostics_for_dir(staging);
            bool saw_bad = false;
            for (const auto& diag : failures_for_dir) {
                if (diag.plugin_name == std::filesystem::path(staged_bad).filename().string() &&
                    diag.code == "hot_reload_incompatible_descriptor") {
                    saw_bad = true;
                    break;
                }
            }
            check(saw_bad, "registry records stable incompatible reload diagnostic");
            continue;
        }

        const bool target_is_v2 = !active_is_v2_family;
        const std::string& src = target_is_v2 ? v2_path : v3_path;
        const std::string staged_ok = staging + "/audio_reload_ok_" + std::to_string(i) + ".dylib";
        std::filesystem::copy_file(src, staged_ok, std::filesystem::copy_options::overwrite_existing);
        audio_engine.pre_reload_operator("AudioReloadOp");
        check(scheduler.reload_operator("AudioReloadOp", registry, staged_ok),
              "scheduler compatible reload succeeds");
        check(audio_engine.post_reload_operator("AudioReloadOp", registry),
              "audio engine compatible reload succeeds");

        active_is_v2_family = target_is_v2;
        expected_sample = active_is_v2_family ? 7.0f : 9.0f;
        check_float(first_sample_after_process(audio_engine), expected_sample, 1e-4f,
                    "compatible reload updates active audio behavior");

        const auto* audio_node = scheduler.compiled_graph()->find_node("audio");
        check(audio_node != nullptr, "scheduler retains audio node after reload");
        if (audio_node) {
            auto pi = audio_node->param_indices.find("level");
            check(pi != audio_node->param_indices.end(), "level param still present after reload");
            if (pi != audio_node->param_indices.end())
                check_float(audio_node->param_values[pi->second], 2.0f, 1e-4f,
                            "level param preserved across reload churn");
            auto oi = audio_node->param_indices.find("offset");
            check(oi != audio_node->param_indices.end(),
                  "offset param present after shape-compatible reload churn");
            if (oi != audio_node->param_indices.end())
                check_float(audio_node->param_values[oi->second], 1.0f, 1e-4f,
                            "offset param default stays stable across reload churn");
        }
    }

    audio_engine.shutdown();
    scheduler.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
