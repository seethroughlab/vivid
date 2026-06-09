#include "runtime/core/runtime_bootstrap.h"
#include "runtime/packages/package_manager.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/graph.h"
#include "runtime/platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include "test_helpers.h"

namespace fs = std::filesystem;

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

static fs::path canonical_or_normal(const fs::path& path) {
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(path, ec);
    if (ec) return path.lexically_normal();
    return normalized;
}

static fs::path write_test_package_source(const fs::path& package_root) {
    fs::create_directories(package_root / "operators" / "control" / "bootstrap_lane_source");
    fs::create_directories(package_root / "operators" / "control" / "bootstrap_lane_sink");

    {
        std::ofstream ofs(package_root / "vivid-package.json");
        ofs << R"json({
  "name": "test-runtime-bootstrap-package",
  "version": "0.0.1",
  "description": "Headless bootstrap package visibility test",
  "operators": ["control/bootstrap_lane_source", "control/bootstrap_lane_sink"],
  "gpu_operators": []
})json";
    }

    {
        std::ofstream ofs(package_root / "operators" / "control" / "bootstrap_lane_source" / "bootstrap_lane_source.cpp");
        ofs << R"cpp(
#include "operator_api/operator.h"

struct BootstrapLaneSourceOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "BootstrapLaneSourceOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    vivid::Param<float> base{"base", 1.0f, 0.0f, 100.0f};
    vivid::Param<int>   count{"count", 4, 1, 64};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&base);
        out.push_back(&count);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float b = ctx->param_values[0];
        int n = static_cast<int>(ctx->param_values[1]);
        ctx->output_values[0] = b;
        if (ctx->value_outputs) {
            uint32_t len = static_cast<uint32_t>(n);
            float* buf = vivid_value_output_floats(&ctx->value_outputs[0], len);
            if (buf) {
                for (uint32_t i = 0; i < len; ++i) {
                    buf[i] = b * static_cast<float>(i + 1);
                }
                vivid_value_output_commit(&ctx->value_outputs[0], len);
            }
        }
    }
};

)cpp";
    }

    {
        std::ofstream ofs(package_root / "operators" / "control" / "bootstrap_lane_sink" / "bootstrap_lane_sink.cpp");
        ofs << R"cpp(
#include "operator_api/operator.h"

struct BootstrapLaneSinkOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "BootstrapLaneSinkOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0];
        if (ctx->values && ctx->value_outputs) {
            const float* src = vivid_value_floats(&ctx->values[0]);
            uint32_t len = vivid_value_count(&ctx->values[0]);
            float* buf = vivid_value_output_floats(&ctx->value_outputs[0], len);
            if (buf) {
                for (uint32_t i = 0; i < len; ++i) {
                    buf[i] = src ? src[i] : 0.0f;
                }
                vivid_value_output_commit(&ctx->value_outputs[0], len);
            }
        }
    }
};

)cpp";
    }

    return package_root;
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
    graph.add_node("src", "BootstrapLaneSourceOp", {{"base", 2.0f}, {"count", 4.0f}});
    graph.add_node("sink", "BootstrapLaneSinkOp");
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
    ScopedTempDir temp_home("runtime_bootstrap_home");
    ScopedEnvVar scoped_home("HOME", temp_home.path.string());
    ScopedEnvVar scoped_extra_paths("VIVID_PACKAGE_PATHS", nullptr);
    ScopedTempDir package_source("runtime_bootstrap_pkg_src");
    fs::path package_root = write_test_package_source(package_source.path);

    {
        vivid::OperatorRegistry install_registry;
        vivid::PackageCompiler compiler(source_dir, build_dir);
        vivid::PackageManager pm(compiler, install_registry);
        auto install_result = pm.link(package_root.string());
        check(install_result.success, "local bootstrap test package links");
        if (!install_result.success) {
            std::fprintf(stderr, "Link error: %s\n", install_result.error.c_str());
            return 1;
        }
    }

    std::fprintf(stderr, "\n=== Test: Runtime bootstrap package visibility ===\n\n");

    {
        vivid::OperatorRegistry registry;
        vivid::PackageCompiler compiler(source_dir, build_dir);
        vivid::PackageManager pm(compiler, registry);
        auto paths = vivid::resolve_runtime_bootstrap_paths(argv[0], source_dir);
        vivid::RegistryBootstrapOptions opts;
        opts.scan_shader_operators = false;
        opts.scan_factory_presets = false;
        auto result = vivid::bootstrap_operator_registry(registry, &pm, paths, opts);

        check(result.package_scan_attempted, "package scan attempted");
        check(discovery_loaded_package(result.package_discovery, package_name),
              "bootstrap loaded the test package");
        check(registry.find("BootstrapLaneSourceOp") != nullptr, "BootstrapLaneSourceOp visible after bootstrap");
        check(registry.find("BootstrapLaneSinkOp") != nullptr, "BootstrapLaneSinkOp visible after bootstrap");

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
        fs::remove_all(package_root / "build", ec);
        vivid::OperatorRegistry registry;
        vivid::PackageCompiler compiler(source_dir, build_dir);
        vivid::PackageManager pm(compiler, registry);
        auto paths = vivid::resolve_runtime_bootstrap_paths(argv[0], source_dir);
        vivid::RegistryBootstrapOptions opts;
        opts.scan_shader_operators = false;
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

    {
        ScopedTempDir fake_bundle("runtime_bootstrap_bundle");
        const fs::path exe_path = fake_bundle.path / "Vivid.app" / "Contents" / "MacOS" / "vivid";
        const fs::path bundle_resources =
            fake_bundle.path / "Vivid.app" / "Contents" / "Resources";
        const fs::path bundled_source = bundle_resources / "source";
        fs::create_directories(exe_path.parent_path());
        std::ofstream(exe_path) << "stub";
        fs::create_directories(bundled_source / "src" / "runtime");
        auto paths = vivid::resolve_runtime_bootstrap_paths(exe_path, "");
        check(canonical_or_normal(paths.resources_dir) == canonical_or_normal(bundle_resources),
              "runtime bootstrap resolves bundle resources directory");
        const fs::path resolved_source = canonical_or_normal(fs::path(paths.source_dir));
        check(resolved_source == canonical_or_normal(fs::path(source_dir)) ||
                  resolved_source == canonical_or_normal(bundled_source),
              "runtime bootstrap prefers checkout source when present, otherwise bundled source tree");
    }

    if (failures != 0) {
        std::fprintf(stderr, "\n%d test(s) failed\n", failures);
        return 1;
    }
    std::fprintf(stderr, "\nAll runtime bootstrap package tests passed\n");
    return 0;
}
