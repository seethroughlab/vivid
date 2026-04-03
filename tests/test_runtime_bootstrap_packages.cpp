#include "runtime/runtime_bootstrap.h"
#include "runtime/package_manager.h"
#include "runtime/package_compiler.h"
#include "runtime/operator_registry.h"
#include "runtime/runtime_core.h"
#include "runtime/graph.h"
#include "runtime/platform.h"

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

namespace fs = std::filesystem;

struct ScopedPackageDir {
    fs::path path;
    explicit ScopedPackageDir(fs::path p) : path(std::move(p)) {}
    ~ScopedPackageDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

static std::string find_source_dir(const std::string& build_dir) {
    fs::path candidate = fs::path(build_dir).parent_path();
    for (int i = 0; i < 4; ++i) {
        if (fs::exists(candidate / "CMakeLists.txt") &&
            fs::exists(candidate / "src" / "runtime")) {
            return candidate.string();
        }
        if (!candidate.has_parent_path()) break;
        candidate = candidate.parent_path();
    }
    return {};
}

static fs::path make_plugin_path(const std::string& build_dir, const std::string& stem) {
    return fs::path(build_dir) / (stem + vivid::kPluginSuffix);
}

static fs::path write_test_package(const std::string& build_dir, const std::string& package_name) {
    fs::path pkg_dir = fs::path(vivid::PackageManager::packages_dir()) / package_name;
    std::error_code ec;
    fs::remove_all(pkg_dir, ec);
    fs::create_directories(pkg_dir / "build");

    {
        std::ofstream ofs(pkg_dir / "vivid-package.json");
        ofs << R"json({
  "name": "test-runtime-bootstrap-package",
  "version": "0.0.1",
  "description": "Headless bootstrap package visibility test",
  "operators": ["control/lane_source_op", "control/lane_sink_op"],
  "gpu_operators": []
})json";
    }

    fs::copy_file(make_plugin_path(build_dir, "lane_source_op"),
                  pkg_dir / "build" / (std::string("lane_source_op") + vivid::kPluginSuffix),
                  fs::copy_options::overwrite_existing);
    fs::copy_file(make_plugin_path(build_dir, "lane_sink_op"),
                  pkg_dir / "build" / (std::string("lane_sink_op") + vivid::kPluginSuffix),
                  fs::copy_options::overwrite_existing);
    return pkg_dir;
}

static bool discovery_loaded_package(const vivid::DiscoveryReport& report, const std::string& name) {
    for (const auto& pkg : report.loaded_packages) {
        if (pkg.name == name) return true;
    }
    return false;
}

static bool discovery_skipped_package(const vivid::DiscoveryReport& report,
                                      const std::string& name,
                                      const std::string& reason) {
    for (const auto& pkg : report.skipped_packages) {
        if (pkg.name == name && pkg.reason == reason) return true;
    }
    return false;
}

static vivid::Graph make_graph() {
    vivid::Graph graph;
    graph.add_node("src", "LaneSourceOp", {{"base", 2.0f}, {"count", 4.0f}});
    graph.add_node("sink", "LaneSinkOp");
    graph.add_connection("src", "out", "sink", "in");
    return graph;
}

static int count_missing_nodes(vivid::RuntimeCore& runtime) {
    int missing = 0;
    if (const auto* cg = runtime.compiled_graph()) {
        for (const auto& node : cg->nodes) {
            if (node.missing_operator) missing++;
        }
    }
    return missing;
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    const std::string source_dir = find_source_dir(build_dir);
    if (source_dir.empty()) {
        std::fprintf(stderr, "Cannot determine source directory, skipping test\n");
        return 0;
    }

    const std::string package_name = "test-runtime-bootstrap-package";
    ScopedPackageDir scoped_pkg(write_test_package(build_dir, package_name));

    std::fprintf(stderr, "\n=== Test: Runtime bootstrap package visibility ===\n\n");

    {
        vivid::OperatorRegistry registry;
        vivid::PackageCompiler compiler(source_dir, build_dir);
        vivid::PackageManager pm(compiler, registry);
        auto paths = vivid::resolve_runtime_bootstrap_paths(argv[0], source_dir);
        vivid::RegistryBootstrapOptions opts;
        opts.scan_wgsl_presets = false;
        opts.scan_factory_presets = false;
        auto result = vivid::bootstrap_operator_registry(registry, &pm, paths, opts);

        check(result.package_scan_attempted, "package scan attempted");
        check(discovery_loaded_package(result.package_discovery, package_name),
              "bootstrap loaded the test package");
        check(registry.find("LaneSourceOp") != nullptr, "LaneSourceOp visible after bootstrap");
        check(registry.find("LaneSinkOp") != nullptr, "LaneSinkOp visible after bootstrap");

        vivid::Graph graph = make_graph();
        registry.load_for_graph(graph);
        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build succeeds with installed package operators");
        if (runtime.compiled_graph()) {
            check(count_missing_nodes(runtime) == 0,
                  "compiled graph has no missing-operator placeholders when package is built");
        }
        runtime.shutdown();
    }

    {
        std::error_code ec;
        fs::remove_all(scoped_pkg.path / "build", ec);
        vivid::OperatorRegistry registry;
        vivid::PackageCompiler compiler(source_dir, build_dir);
        vivid::PackageManager pm(compiler, registry);
        auto paths = vivid::resolve_runtime_bootstrap_paths(argv[0], source_dir);
        vivid::RegistryBootstrapOptions opts;
        opts.scan_wgsl_presets = false;
        opts.scan_factory_presets = false;
        auto result = vivid::bootstrap_operator_registry(registry, &pm, paths, opts);

        check(discovery_skipped_package(result.package_discovery, package_name, "not_built"),
              "bootstrap reports the package as not built when build dir is missing");

        vivid::Graph graph = make_graph();
        registry.load_for_graph(graph);
        vivid::RuntimeCore runtime;
        check(runtime.build(graph, registry), "runtime.build still succeeds with placeholders when package is unbuilt");
        if (runtime.compiled_graph()) {
            check(count_missing_nodes(runtime) == 2,
                  "compiled graph uses placeholders when package operators are unavailable");
        }
        runtime.shutdown();
    }

    if (failures != 0) {
        std::fprintf(stderr, "\n%d test(s) failed\n", failures);
        return 1;
    }
    std::fprintf(stderr, "\nAll runtime bootstrap package tests passed\n");
    return 0;
}
