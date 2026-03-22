#include "runtime/builtin_operators.h"
#include "runtime/graph.h"
#include "runtime/operator_registry.h"
#include "runtime/package_compiler.h"
#include "runtime/package_manager.h"
#include "runtime/runtime_api.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

static void print_install_result(const char* label, const vivid::InstallResult& result) {
    if (result.success) return;
    std::fprintf(stderr, "    %s error: %s\n", label, result.error.c_str());
    for (const auto& compile : result.compile_results) {
        if (!compile.success) {
            std::fprintf(stderr, "    compile failure for %s:\n%s\n",
                         compile.operator_name.c_str(), compile.error_output.c_str());
        }
    }
}

static void write_live_pkg_source(const std::string& root,
                                  const std::string& type_name,
                                  float output_value) {
    std::filesystem::create_directories(root + "/operators/control/pkg_live_op");
    std::ofstream ofs(root + "/operators/control/pkg_live_op/pkg_live_op.cpp", std::ios::trunc);
    ofs << "#include \"operator_api/operator.h\"\n\n"
           "struct " << type_name << " : vivid::ControlOperatorBase {\n"
           "    static constexpr const char* kName = \"" << type_name << "\";\n"
           "    static constexpr bool kTimeDependent = false;\n"
           "    void collect_params(std::vector<vivid::ParamBase*>&) override {}\n"
           "    void collect_ports(std::vector<VividPortDescriptor>& out) override {\n"
           "        out.push_back({\"out\", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});\n"
           "    }\n"
           "    void process(const VividProcessContext* ctx) override {\n"
        << "        ctx->output_values[0] = " << std::to_string(output_value) << "f;\n"
           "    }\n"
           "};\n\n"
           "VIVID_REGISTER(" << type_name << ")\n";
}

static void write_live_pkg_manifest(const std::string& root, const std::string& package_name) {
    std::ofstream ofs(root + "/vivid-package.json", std::ios::trunc);
    ofs << "{\n"
           "  \"name\": \"" << package_name << "\",\n"
           "  \"version\": \"0.0.1\",\n"
           "  \"operators\": [\"control/pkg_live_op\"]\n"
           "}\n";
}

static float node_scalar_output(const vivid::Scheduler& scheduler, const std::string& node_id) {
    const auto* node = scheduler.find_node(node_id);
    if (!node || node->output_values.empty()) return std::nanf("");
    return node->output_values[0];
}

static void check_finite_output(const vivid::Scheduler& scheduler,
                                const std::string& node_id,
                                const char* msg) {
    const float value = node_scalar_output(scheduler, node_id);
    check(std::isfinite(value), msg);
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    build_dir = std::filesystem::absolute(build_dir).string();

    const auto original_cwd = std::filesystem::current_path();
    const auto isolated_cwd = std::filesystem::path(build_dir) / ".test_pkg_stress_cwd";
    const auto test_home = std::filesystem::path(build_dir) / ".test_pkg_stress_home";
    const auto pkg_src = std::filesystem::path(build_dir) / ".test_pkg_stress_live_pkg_src";
    const std::string package_name = "vivid-live-pkg-stress";
    const std::string type_name = "PkgLiveStressOp";

    std::filesystem::remove_all(isolated_cwd);
    std::filesystem::remove_all(test_home);
    std::filesystem::remove_all(pkg_src);
    std::filesystem::create_directories(isolated_cwd);
    std::filesystem::create_directories(test_home);
    std::filesystem::create_directories(pkg_src);
    std::filesystem::current_path(isolated_cwd);
    setenv("HOME", test_home.string().c_str(), 1);
    setenv("VIVID_SKIP_PACKAGE_CATALOG_NETWORK", "1", 1);

    write_live_pkg_manifest(pkg_src.string(), package_name);
    write_live_pkg_source(pkg_src.string(), type_name, 3.0f);

    std::fprintf(stderr, "\n=== Test: Package Stress ===\n\n");

    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);

    std::string vivid_src_dir = std::filesystem::absolute(build_dir).parent_path().string();
    vivid::PackageCompiler compiler(vivid_src_dir, build_dir);
    vivid::PackageManager package_manager(compiler, registry);

    auto link_result = package_manager.link(pkg_src.string());
    print_install_result("link", link_result);
    check(link_result.success, "package_manager.link() succeeds");
    check(registry.find(type_name) != nullptr, "package operator registered after link");

    vivid::Graph graph;
    check(graph.add_node("pkg_live", type_name), "graph.add_node(pkg_live)");

    vivid::Scheduler scheduler;
    check(scheduler.build(graph, registry), "scheduler.build() with linked package");
    vivid::AudioEngine audio_engine;
    bool has_gpu_ops = false;
    bool has_audio = false;
    vivid::RuntimeAPI api(graph, scheduler, audio_engine, registry);

    check_finite_output(scheduler, "pkg_live", "initial linked package output is finite");
    for (int cycle = 0; cycle < 4; ++cycle) {
        std::fprintf(stderr, "\n--- package cycle %d ---\n", cycle + 1);

        scheduler.tick(0.0, 0.016, static_cast<uint64_t>(cycle));
        check_finite_output(scheduler, "pkg_live", "linked package output stays finite");

        std::string snapshot_json;
        check(graph.save_to_string(snapshot_json), "save_to_string(package snapshot)");
        audio_engine.shutdown();
        scheduler.shutdown();

        const float rebuilt_value = 7.0f + cycle * 8.0f;
        write_live_pkg_source(pkg_src.string(), type_name, rebuilt_value);
        auto rebuild_result = package_manager.rebuild(package_name);
        print_install_result("rebuild", rebuild_result);
        check(rebuild_result.success, "package_manager.rebuild() succeeds");
        std::fprintf(stderr, "  INFO: applying snapshot after rebuild\n");
        auto apply_rebuild = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
        check(apply_rebuild.ok, "apply_snapshot_json after rebuild succeeds");
        std::fprintf(stderr, "  INFO: ticking after rebuild\n");
        scheduler.tick(0.0, 0.016, static_cast<uint64_t>(cycle + 10));
        {
            const auto* rebuilt_node = scheduler.find_node("pkg_live");
            check(rebuilt_node != nullptr, "pkg_live remains after rebuild");
            if (rebuilt_node)
                check(!rebuilt_node->missing_operator, "pkg_live stays resolved after rebuild");
        }
        check_finite_output(scheduler, "pkg_live", "rebuilt package output stays finite");

        check(graph.save_to_string(snapshot_json), "save_to_string before unlink");
        audio_engine.shutdown();
        scheduler.shutdown();
        check(package_manager.unlink(package_name), "package_manager.unlink() succeeds");
        std::fprintf(stderr, "  INFO: applying snapshot after unlink\n");
        auto apply_unlink = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
        check(apply_unlink.ok, "apply_snapshot_json after unlink succeeds");
        const auto* missing_node = scheduler.find_node("pkg_live");
        check(missing_node != nullptr, "pkg_live remains in graph after unlink");
        if (missing_node)
            check(missing_node->missing_operator, "pkg_live becomes missing operator after unlink");

        const float relinked_value = rebuilt_value + 4.0f;
        write_live_pkg_source(pkg_src.string(), type_name, relinked_value);
        audio_engine.shutdown();
        scheduler.shutdown();
        auto relink_result = package_manager.link(pkg_src.string());
        print_install_result("relink", relink_result);
        check(relink_result.success, "package_manager.link() relink succeeds");
        std::fprintf(stderr, "  INFO: applying snapshot after relink\n");
        auto apply_relink = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
        check(apply_relink.ok, "apply_snapshot_json after relink succeeds");
        std::fprintf(stderr, "  INFO: ticking after relink\n");
        scheduler.tick(0.0, 0.016, static_cast<uint64_t>(cycle + 20));
        const auto* recovered_node = scheduler.find_node("pkg_live");
        check(recovered_node != nullptr, "pkg_live restored after relink");
        if (recovered_node) {
            check(!recovered_node->missing_operator, "pkg_live resolves again after relink");
        }
        check_finite_output(scheduler, "pkg_live", "relinked package output stays finite");
    }

    scheduler.shutdown();
    std::filesystem::current_path(original_cwd);
    std::filesystem::remove_all(isolated_cwd);
    std::filesystem::remove_all(test_home);
    std::filesystem::remove_all(pkg_src);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
