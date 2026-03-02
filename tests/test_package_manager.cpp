// Test: PackageManager — install/list/uninstall lifecycle with local paths
#include "runtime/package_manager.h"
#include "runtime/package_compiler.h"
#include "runtime/operator_registry.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

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
    namespace fs = std::filesystem;

    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    // Find source directory
    std::string source_dir;
    auto candidate = fs::path(build_dir).parent_path();
    for (int i = 0; i < 3; ++i) {
        if (fs::exists(candidate / "CMakeLists.txt") &&
            fs::exists(candidate / "src" / "runtime")) {
            source_dir = candidate.string();
            break;
        }
        candidate = candidate.parent_path();
    }

    if (source_dir.empty()) {
        std::fprintf(stderr, "Cannot determine source directory, skipping test\n");
        return 0;
    }

    std::fprintf(stderr, "\n=== Test: PackageManager ===\n\n");

    // Create a mock package as a local directory
    std::string mock_pkg_dir = build_dir + "/.test_mock_package";
    fs::create_directories(mock_pkg_dir + "/operators/control/test_mgr_op");

    // Write operator source
    {
        std::ofstream ofs(mock_pkg_dir + "/operators/control/test_mgr_op/test_mgr_op.cpp");
        ofs << R"cpp(
#include "operator_api/operator.h"

struct TestMgrOp : vivid::OperatorBase {
    static constexpr const char* kName   = "TestMgrOp";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 10.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0] * 3.0f;
    }
};

VIVID_REGISTER(TestMgrOp)
)cpp";
    }

    // Write manifest
    {
        std::ofstream ofs(mock_pkg_dir + "/vivid-package.json");
        ofs << R"json({
  "name": "test-mgr-package",
  "version": "1.2.3",
  "description": "Test package for manager tests",
  "operators": ["control/test_mgr_op"],
  "gpu_operators": []
})json";
    }

    // --- Setup ---
    vivid::OperatorRegistry registry;
    registry.scan_deferred(build_dir.c_str());

    vivid::PackageCompiler compiler(source_dir, build_dir);
    vivid::PackageManager pm(compiler, registry);

    // Clean up any previous test package
    std::string pkg_install_dir = vivid::PackageManager::packages_dir() + "/test-mgr-package";
    if (fs::exists(pkg_install_dir))
        fs::remove_all(pkg_install_dir);

    // --- Test 1: Install from local path ---
    std::fprintf(stderr, "--- Install from local path ---\n");
    auto install_result = pm.install(mock_pkg_dir);
    check(install_result.success, "install succeeds");
    check(install_result.info.name == "test-mgr-package", "package name correct");
    check(install_result.info.version == "1.2.3", "version correct");
    check(install_result.info.operators.size() == 1, "1 operator listed");
    check(install_result.compile_results.size() == 1, "1 compile result");
    if (!install_result.compile_results.empty())
        check(install_result.compile_results[0].success, "operator compiled successfully");

    // Verify operator is in registry
    auto names = registry.type_names();
    bool found = false;
    for (const auto& n : names) {
        if (n == "TestMgrOp") { found = true; break; }
    }
    check(found, "TestMgrOp in registry after install");

    // --- Test 2: List packages ---
    std::fprintf(stderr, "\n--- List packages ---\n");
    auto packages = pm.list();
    bool listed = false;
    for (const auto& p : packages) {
        if (p.name == "test-mgr-package") { listed = true; break; }
    }
    check(listed, "test-mgr-package in list");

    // --- Test 3: Double install fails ---
    std::fprintf(stderr, "\n--- Double install ---\n");
    auto double_install = pm.install(mock_pkg_dir);
    check(!double_install.success, "double install fails");
    check(!double_install.error.empty(), "error message set");

    // --- Test 4: Uninstall ---
    std::fprintf(stderr, "\n--- Uninstall ---\n");
    check(pm.uninstall("test-mgr-package"), "uninstall succeeds");
    check(!fs::exists(pkg_install_dir), "package directory removed");

    // Verify operator is gone from registry
    names = registry.type_names();
    found = false;
    for (const auto& n : names) {
        if (n == "TestMgrOp") { found = true; break; }
    }
    check(!found, "TestMgrOp removed from registry after uninstall");

    // --- Test 5: Uninstall non-existent ---
    std::fprintf(stderr, "\n--- Uninstall non-existent ---\n");
    check(!pm.uninstall("nonexistent-package"), "uninstall of non-existent fails");

    // --- Test 6: Install with bad manifest ---
    std::fprintf(stderr, "\n--- Install with bad manifest ---\n");
    std::string bad_pkg_dir = build_dir + "/.test_bad_package";
    fs::create_directories(bad_pkg_dir);
    // No vivid-package.json
    auto bad_result = pm.install(bad_pkg_dir);
    check(!bad_result.success, "install without manifest fails");
    check(!bad_result.error.empty(), "error message set");

    // Cleanup
    fs::remove_all(mock_pkg_dir);
    fs::remove_all(bad_pkg_dir);
    // Clean up any leftover installed package
    if (fs::exists(pkg_install_dir))
        fs::remove_all(pkg_install_dir);
    // The bad package would have been cleaned up by install failure

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures;
}
