#include "runtime/builtin_operators.h"
#include "runtime/graph.h"
#include "runtime/operator_registry.h"
#include "runtime/runtime_api.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
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

static bool write_text(const std::string& path, const std::string& text) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs << text;
    return ofs.good();
}

static std::string make_graph_json(float scale_a, float scale_b) {
    return std::string("{\n") +
           "  \"nodes\": {\n"
           "    \"a\": {\n"
           "      \"type\": \"TestOp\",\n"
           "      \"params\": {\"scale\": " + std::to_string(scale_a) + "}\n"
           "    },\n"
           "    \"b\": {\n"
           "      \"type\": \"TestOp\",\n"
           "      \"params\": {\"scale\": " + std::to_string(scale_b) + "}\n"
           "    }\n"
           "  },\n"
           "  \"connections\": [\n"
           "    {\"from\": \"a/out\", \"to\": \"b/scale\"}\n"
           "  ]\n"
           "}\n";
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    const std::string staging = build_dir + "/.test_runtime_stress_staging";
    const std::string state_carry_path = build_dir + "/test_state_carry_op.dylib";
    const std::string test_op_path = build_dir + "/test_op_v1.dylib";
    const std::string save_path = build_dir + "/test_runtime_stress_graph.json";
    const std::string test_home = build_dir + "/.test_runtime_stress_home";

    std::filesystem::remove_all(staging);
    std::filesystem::remove_all(test_home);
    std::filesystem::create_directories(staging);
    std::filesystem::create_directories(test_home);
    setenv("HOME", test_home.c_str(), 1);
    std::filesystem::copy_file(test_op_path, staging + "/test_op_v1.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(state_carry_path, staging + "/test_state_carry_op.dylib",
                               std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: Runtime Stress ===\n\n");
    std::fprintf(stderr, "[runtime_stress] setup files done\n");

    vivid::OperatorRegistry registry;
    std::fprintf(stderr, "[runtime_stress] registry constructed\n");
    register_builtin_operators(registry);
    std::fprintf(stderr, "[runtime_stress] builtins registered\n");
    check(registry.scan(staging.c_str()), "registry.scan()");
    std::fprintf(stderr, "[runtime_stress] scan returned\n");

    vivid::Graph graph;
    check(graph.load((build_dir + "/test_runtime_api.json").c_str()), "graph.load(test_runtime_api.json)");
    std::fprintf(stderr, "[runtime_stress] graph loaded\n");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build()");
    std::fprintf(stderr, "[runtime_stress] scheduler built\n");

    vivid::AudioEngine audio_engine;
    bool has_gpu_ops = false;
    bool has_audio = false;

    vivid::RuntimeAPI api(graph, scheduler, audio_engine, registry);
    std::fprintf(stderr, "[runtime_stress] runtime api constructed\n");

    auto save_result = api.save_as(save_path);
    check(save_result.ok, "initial save_as() succeeds");
    check(graph.source_path() == save_path, "initial save_as retargets source path");

    uint64_t serial = api.reload_serial();

    for (int i = 0; i < 4; ++i) {
        std::fprintf(stderr, "\n--- cycle %d ---\n", i + 1);

        const float new_scale = 10.0f + static_cast<float>(i);
        check(api.set_param("a", "scale", new_scale).ok, "set_param(a/scale)");
        scheduler.tick(0.0, 0.016, static_cast<uint64_t>(i));

        std::string live_snapshot;
        check(graph.save_to_string(live_snapshot), "save_to_string(live snapshot)");

        const uint64_t before_snapshot_ok = api.reload_serial();
        auto snapshot_ok = api.apply_snapshot_json(live_snapshot, has_gpu_ops, has_audio);
        check(snapshot_ok.ok, "apply_snapshot_json(valid) succeeds");
        check(api.reload_serial() == before_snapshot_ok + 1,
              "apply_snapshot_json(valid) increments reload_serial");
        check(graph.source_path() == save_path, "apply_snapshot_json preserves source_path");
        serial = api.reload_serial();

        const uint64_t before_snapshot_fail = api.reload_serial();
        auto snapshot_fail = api.apply_snapshot_json("{ not valid json", has_gpu_ops, has_audio);
        check(!snapshot_fail.ok, "apply_snapshot_json(invalid) fails");
        check(api.reload_serial() == before_snapshot_fail,
              "apply_snapshot_json(invalid) leaves reload_serial unchanged");
        check(graph.source_path() == save_path, "failed snapshot preserves source_path");
        const auto* a_after_snapshot = graph.find_node("a");
        check(a_after_snapshot != nullptr, "node a restored after failed snapshot");
        if (a_after_snapshot) {
            auto it = a_after_snapshot->params.find("scale");
            check(it != a_after_snapshot->params.end(), "node a scale param restored after failed snapshot");
            if (it != a_after_snapshot->params.end())
                check_float(it->second, new_scale, 1e-4f, "failed snapshot restores live param state");
        }

        const std::string reload_json = make_graph_json(2.0f + static_cast<float>(i), 3.0f + static_cast<float>(i));
        check(write_text(save_path, reload_json), "write valid reload graph");
        const uint64_t before_reload = api.reload_serial();
        auto reload_ok = api.reload(has_gpu_ops, has_audio);
        check(reload_ok.ok, "reload(valid) succeeds");
        check(api.reload_serial() == before_reload + 1, "reload(valid) increments reload_serial");
        check(graph.source_path() == save_path, "reload(valid) preserves source_path");
        serial = api.reload_serial();

        check(write_text(save_path, "{ bad json"), "write invalid reload graph");
        const uint64_t before_reload_fail = api.reload_serial();
        auto reload_fail = api.reload(has_gpu_ops, has_audio);
        check(!reload_fail.ok, "reload(invalid) fails");
        check(api.reload_serial() == before_reload_fail, "reload(invalid) leaves reload_serial unchanged");
        check(graph.source_path() == save_path, "failed reload preserves source_path");
        const auto* a_after_reload = graph.find_node("a");
        check(a_after_reload != nullptr, "node a restored after failed reload");

        check(api.reload_serial() >= serial, "reload_serial remains monotonic");
        serial = api.reload_serial();
    }

    scheduler.shutdown();
    std::filesystem::remove_all(staging);
    std::filesystem::remove_all(test_home);
    std::filesystem::remove(save_path);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
