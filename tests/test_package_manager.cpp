// Test: PackageManager — install/list/uninstall lifecycle with local paths
#include "runtime/package_manager.h"
#include "runtime/package_compiler.h"
#include "runtime/operator_registry.h"
#include <cstdlib>
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

    // Create a vendored dependency header
    fs::create_directories(mock_pkg_dir + "/deps/mock_vendor");
    {
        std::ofstream ofs(mock_pkg_dir + "/deps/mock_vendor/mock_vendor.h");
        ofs << "#pragma once\n"
               "static constexpr float MOCK_VENDOR_SCALE = 3.0f;\n";
    }

    // Write operator source (uses vendored header)
    {
        std::ofstream ofs(mock_pkg_dir + "/operators/control/test_mgr_op/test_mgr_op.cpp");
        ofs << R"cpp(
#include "operator_api/operator.h"
#include "mock_vendor.h"

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
        ctx->output_values[0] = ctx->param_values[0] * MOCK_VENDOR_SCALE;
    }
};

VIVID_REGISTER(TestMgrOp)
)cpp";
    }

    // Write manifest (with vendor dependency)
    {
        std::ofstream ofs(mock_pkg_dir + "/vivid-package.json");
        ofs << R"json({
  "name": "test-mgr-package",
  "version": "1.2.3",
  "vivid_core": ">=0.1.0 <2.0.0",
  "description": "Test package for manager tests",
  "author": "tester",
  "operators": ["control/test_mgr_op"],
  "gpu_operators": [],
  "dependencies": {
    "vendor": [{"name": "mock_vendor", "include": "deps/mock_vendor"}]
  }
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
    check(install_result.info.vivid_core == ">=0.1.0 <2.0.0", "vivid_core parsed");
    check(install_result.info.author == "tester", "author correct");
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

    // --- Test 7: Missing compiler tool has clear remediation ---
    std::fprintf(stderr, "\n--- Missing compiler tool preflight ---\n");
    setenv("VIVID_MOCK_MISSING_TOOL", "clang++", 1);
    auto missing_tool_result = pm.install(mock_pkg_dir);
    unsetenv("VIVID_MOCK_MISSING_TOOL");
    check(!missing_tool_result.success, "install fails when compiler is unavailable");
    check(missing_tool_result.error.find("Missing required build tool: clang++") != std::string::npos,
          "missing compiler error is clear");
    check(!fs::exists(pkg_install_dir), "missing compiler failure rolls back package directory");

    // --- Test 8: Compile failure cleans up partial install ---
    std::fprintf(stderr, "\n--- Compile failure cleanup ---\n");
    std::string fail_pkg_dir = build_dir + "/.test_compile_fail_package";
    std::string fail_install_dir = vivid::PackageManager::packages_dir() + "/test-compile-fail-package";
    fs::remove_all(fail_pkg_dir);
    fs::remove_all(fail_install_dir);
    fs::create_directories(fail_pkg_dir + "/operators/control/bad_compile");
    {
        std::ofstream ofs(fail_pkg_dir + "/vivid-package.json");
        ofs << R"json({
  "name": "test-compile-fail-package",
  "version": "0.0.1",
  "description": "Intentional compile failure",
  "operators": ["control/bad_compile"],
  "gpu_operators": []
})json";
    }
    {
        std::ofstream ofs(fail_pkg_dir + "/operators/control/bad_compile/bad_compile.cpp");
        ofs << R"cpp(
#include "operator_api/operator.h"
using namespace vivid;
struct BadCompile : OperatorBase {
    void process(const VividProcessContext* ctx) override {
        int x = ; // intentional syntax error
        (void)x;
    }
};
VIVID_REGISTER(BadCompile, "BadCompile", "Bad compile fixture", "control")
)cpp";
    }
    auto fail_result = pm.install(fail_pkg_dir);
    check(!fail_result.success, "install with compile failure fails");
    check(!fail_result.error.empty(), "compile failure has error message");
    check(!fs::exists(fail_install_dir), "compile failure rolls back package directory");
    fs::remove_all(fail_pkg_dir);

    // --- Test 9: Expanded manifest fields ---
    std::fprintf(stderr, "\n--- Expanded manifest fields ---\n");
    {
        std::string exp_pkg_dir = build_dir + "/.test_expanded_package";
        fs::remove_all(exp_pkg_dir);
        fs::create_directories(exp_pkg_dir);

        std::ofstream ofs(exp_pkg_dir + "/vivid-package.json");
        ofs << R"json({
  "name": "test-expanded-package",
  "version": "2.0.0",
  "vivid_core": ">=0.1.0 <2.0.0",
  "description": "Package with all manifest fields",
  "author": "Jane Doe",
  "operators": [],
  "gpu_operators": [],
  "dependencies": {
    "packages": ["vivid-core", "vivid-audio"],
    "vendor": [
      {"name": "stb_image", "include": "deps/stb"},
      {"name": "dr_wav", "include": "deps/dr_libs"}
    ]
  },
  "tests": {
    "graphs": ["tests/basic.json", "tests/stress.json"],
    "cpp": ["tests/test_ops.cpp"]
  }
})json";
        ofs.close();

        // Parse via list() — create in packages dir so list() finds it
        std::string exp_install_dir = vivid::PackageManager::packages_dir() + "/test-expanded-package";
        fs::create_directories(exp_install_dir);
        fs::copy_file(exp_pkg_dir + "/vivid-package.json",
                       exp_install_dir + "/vivid-package.json",
                       fs::copy_options::overwrite_existing);

        auto pkgs = pm.list();
        const vivid::PackageInfo* found_pkg = nullptr;
        for (const auto& p : pkgs) {
            if (p.name == "test-expanded-package") { found_pkg = &p; break; }
        }
        check(found_pkg != nullptr, "expanded package in list");
        if (found_pkg) {
            check(found_pkg->author == "Jane Doe", "author parsed");
            check(found_pkg->vivid_core == ">=0.1.0 <2.0.0", "vivid_core parsed");
            check(found_pkg->dependencies.packages.size() == 2, "2 dependency packages");
            check(found_pkg->dependencies.packages[0] == "vivid-core", "dep package 0");
            check(found_pkg->dependencies.packages[1] == "vivid-audio", "dep package 1");
            check(found_pkg->dependencies.vendor.size() == 2, "2 vendor deps");
            check(found_pkg->dependencies.vendor[0].name == "stb_image", "vendor 0 name");
            check(found_pkg->dependencies.vendor[0].include == "deps/stb", "vendor 0 include");
            check(found_pkg->dependencies.vendor[1].name == "dr_wav", "vendor 1 name");
            check(found_pkg->dependencies.vendor[1].include == "deps/dr_libs", "vendor 1 include");
            check(found_pkg->tests.graphs.size() == 2, "2 test graphs");
            check(found_pkg->tests.graphs[0] == "tests/basic.json", "test graph 0");
            check(found_pkg->tests.graphs[1] == "tests/stress.json", "test graph 1");
            check(found_pkg->tests.cpp.size() == 1, "1 test cpp");
            check(found_pkg->tests.cpp[0] == "tests/test_ops.cpp", "test cpp 0");
        }

        // Cleanup
        fs::remove_all(exp_install_dir);
        fs::remove_all(exp_pkg_dir);
    }

    // --- Test 10: Install cmake-built package ---
    std::fprintf(stderr, "\n--- Install cmake-built package ---\n");
    std::string cmake_pkg_dir = build_dir + "/.test_cmake_package";
    fs::remove_all(cmake_pkg_dir);
    fs::create_directories(cmake_pkg_dir + "/src");

    // Write operator source
    {
        std::ofstream ofs(cmake_pkg_dir + "/src/test_cmake_op.cpp");
        ofs << R"cpp(
#include "operator_api/operator.h"

struct TestCmakeOp : vivid::OperatorBase {
    static constexpr const char* kName   = "TestCmakeOp";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> value{"value", 0.5f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&value);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0] * 2.0f;
    }
};

VIVID_REGISTER(TestCmakeOp)
)cpp";
    }

    // Write CMakeLists.txt that builds a MODULE library
    {
        std::ofstream ofs(cmake_pkg_dir + "/CMakeLists.txt");
        ofs << R"cmake(
cmake_minimum_required(VERSION 3.16)
project(test-cmake-package)

add_library(test_cmake_op MODULE src/test_cmake_op.cpp)
target_include_directories(test_cmake_op PRIVATE ${VIVID_SRC_DIR}/src)
set_target_properties(test_cmake_op PROPERTIES
    PREFIX ""
    SUFFIX "${VIVID_PLUGIN_SUFFIX}"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
)cmake";
    }

    // Write manifest with build: cmake
    {
        std::ofstream ofs(cmake_pkg_dir + "/vivid-package.json");
        ofs << R"json({
  "name": "test-cmake-package",
  "version": "0.1.0",
  "description": "Test cmake-built package",
  "build": "cmake",
  "gpu_operators": ["test_cmake_op"]
})json";
    }

    std::string cmake_install_dir = vivid::PackageManager::packages_dir() + "/test-cmake-package";
    if (fs::exists(cmake_install_dir))
        fs::remove_all(cmake_install_dir);

    auto cmake_result = pm.install(cmake_pkg_dir);
    check(cmake_result.success, "cmake install succeeds");
    if (!cmake_result.success) {
        std::fprintf(stderr, "    error: %s\n", cmake_result.error.c_str());
    }
    check(cmake_result.info.name == "test-cmake-package", "cmake package name correct");
    check(cmake_result.info.build_type == "cmake", "build_type is cmake");
    check(!cmake_result.compile_results.empty(), "cmake produced dylib(s)");

    // Verify operator in registry
    names = registry.type_names();
    found = false;
    for (const auto& n : names) {
        if (n == "TestCmakeOp") { found = true; break; }
    }
    check(found, "TestCmakeOp in registry after cmake install");

    // Uninstall cmake package
    check(pm.uninstall("test-cmake-package"), "cmake package uninstall succeeds");
    check(!fs::exists(cmake_install_dir), "cmake package directory removed");

    // --- Test 9: Transitive dependency installation ---
    std::fprintf(stderr, "\n--- Transitive dependency installation ---\n");
    {
        // Create three mock packages: base -> mid -> top
        std::string dep_base_dir = build_dir + "/.test_dep_base";
        std::string dep_mid_dir  = build_dir + "/.test_dep_mid";
        std::string dep_top_dir  = build_dir + "/.test_dep_top";
        fs::remove_all(dep_base_dir);
        fs::remove_all(dep_mid_dir);
        fs::remove_all(dep_top_dir);
        fs::create_directories(dep_base_dir);
        fs::create_directories(dep_mid_dir);
        fs::create_directories(dep_top_dir);

        {
            std::ofstream ofs(dep_base_dir + "/vivid-package.json");
            ofs << R"json({"name": "test-dep-base", "version": "1.0.0", "operators": []})json";
        }
        {
            std::ofstream ofs(dep_mid_dir + "/vivid-package.json");
            ofs << R"json({"name": "test-dep-mid", "version": "1.0.0", "operators": [],
                           "dependencies": {"packages": ["test-dep-base"]}})json";
        }
        {
            std::ofstream ofs(dep_top_dir + "/vivid-package.json");
            ofs << R"json({"name": "test-dep-top", "version": "1.0.0", "operators": [],
                           "dependencies": {"packages": ["test-dep-mid"]}})json";
        }

        // Clean any leftovers
        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-dep-base");
        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-dep-mid");
        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-dep-top");

        // Set up resolver mapping names → local dirs
        pm.set_resolver([&](const std::string& name) -> std::string {
            if (name == "test-dep-base") return dep_base_dir;
            if (name == "test-dep-mid")  return dep_mid_dir;
            return "";
        });

        auto dep_result = pm.install(dep_top_dir);
        check(dep_result.success, "transitive install succeeds");
        if (!dep_result.success)
            std::fprintf(stderr, "    error: %s\n", dep_result.error.c_str());
        check(pm.is_installed("test-dep-base"), "base dep is installed");
        check(pm.is_installed("test-dep-mid"), "mid dep is installed");
        check(pm.is_installed("test-dep-top"), "top package is installed");
        check(dep_result.installed_deps.size() == 2, "2 deps installed");

        // Cleanup
        pm.uninstall("test-dep-top");
        pm.uninstall("test-dep-mid");
        pm.uninstall("test-dep-base");
        fs::remove_all(dep_base_dir);
        fs::remove_all(dep_mid_dir);
        fs::remove_all(dep_top_dir);
        pm.set_resolver(nullptr);
    }

    // --- Test 10: Circular dependency detection ---
    std::fprintf(stderr, "\n--- Circular dependency detection ---\n");
    {
        std::string circ_a_dir = build_dir + "/.test_circ_a";
        std::string circ_b_dir = build_dir + "/.test_circ_b";
        fs::remove_all(circ_a_dir);
        fs::remove_all(circ_b_dir);
        fs::create_directories(circ_a_dir);
        fs::create_directories(circ_b_dir);

        {
            std::ofstream ofs(circ_a_dir + "/vivid-package.json");
            ofs << R"json({"name": "test-circ-a", "version": "1.0.0", "operators": [],
                           "dependencies": {"packages": ["test-circ-b"]}})json";
        }
        {
            std::ofstream ofs(circ_b_dir + "/vivid-package.json");
            ofs << R"json({"name": "test-circ-b", "version": "1.0.0", "operators": [],
                           "dependencies": {"packages": ["test-circ-a"]}})json";
        }

        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-circ-a");
        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-circ-b");

        pm.set_resolver([&](const std::string& name) -> std::string {
            if (name == "test-circ-a") return circ_a_dir;
            if (name == "test-circ-b") return circ_b_dir;
            return "";
        });

        auto circ_result = pm.install(circ_a_dir);
        check(!circ_result.success, "circular dep install fails");
        check(circ_result.error.find("Circular") != std::string::npos, "error mentions Circular");

        // Cleanup
        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-circ-a");
        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-circ-b");
        fs::remove_all(circ_a_dir);
        fs::remove_all(circ_b_dir);
        pm.set_resolver(nullptr);
    }

    // --- Test 11: Already-installed dependency is skipped ---
    std::fprintf(stderr, "\n--- Already-installed dependency skipped ---\n");
    {
        std::string skip_base_dir = build_dir + "/.test_skip_base";
        std::string skip_mid_dir  = build_dir + "/.test_skip_mid";
        fs::remove_all(skip_base_dir);
        fs::remove_all(skip_mid_dir);
        fs::create_directories(skip_base_dir);
        fs::create_directories(skip_mid_dir);

        {
            std::ofstream ofs(skip_base_dir + "/vivid-package.json");
            ofs << R"json({"name": "test-skip-base", "version": "1.0.0", "operators": []})json";
        }
        {
            std::ofstream ofs(skip_mid_dir + "/vivid-package.json");
            ofs << R"json({"name": "test-skip-mid", "version": "1.0.0", "operators": [],
                           "dependencies": {"packages": ["test-skip-base"]}})json";
        }

        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-skip-base");
        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-skip-mid");

        // Pre-install base manually
        pm.set_resolver(nullptr);
        auto base_res = pm.install(skip_base_dir);
        check(base_res.success, "pre-install base succeeds");

        // Now install mid — base should be skipped
        pm.set_resolver([&](const std::string& name) -> std::string {
            if (name == "test-skip-base") return skip_base_dir;
            return "";
        });

        auto mid_res = pm.install(skip_mid_dir);
        check(mid_res.success, "install with pre-installed dep succeeds");
        if (!mid_res.success)
            std::fprintf(stderr, "    error: %s\n", mid_res.error.c_str());
        check(mid_res.installed_deps.empty(), "no deps installed (already present)");

        // Cleanup
        pm.uninstall("test-skip-mid");
        pm.uninstall("test-skip-base");
        fs::remove_all(skip_base_dir);
        fs::remove_all(skip_mid_dir);
        pm.set_resolver(nullptr);
    }

    // --- Test 12: Missing resolver error ---
    std::fprintf(stderr, "\n--- Missing resolver error ---\n");
    {
        std::string nores_dir = build_dir + "/.test_nores";
        fs::remove_all(nores_dir);
        fs::create_directories(nores_dir);

        {
            std::ofstream ofs(nores_dir + "/vivid-package.json");
            ofs << R"json({"name": "test-nores", "version": "1.0.0", "operators": [],
                           "dependencies": {"packages": ["some-other-pkg"]}})json";
        }

        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-nores");

        // Don't set a resolver
        pm.set_resolver(nullptr);
        auto nores_result = pm.install(nores_dir);
        check(!nores_result.success, "install without resolver fails");
        check(nores_result.error.find("resolver") != std::string::npos, "error mentions resolver");

        // Cleanup
        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-nores");
        fs::remove_all(nores_dir);
    }

    // --- Test 13: Unresolvable dependency error ---
    std::fprintf(stderr, "\n--- Unresolvable dependency error ---\n");
    {
        std::string unres_dir = build_dir + "/.test_unres";
        fs::remove_all(unres_dir);
        fs::create_directories(unres_dir);

        {
            std::ofstream ofs(unres_dir + "/vivid-package.json");
            ofs << R"json({"name": "test-unres", "version": "1.0.0", "operators": [],
                           "dependencies": {"packages": ["nonexistent-pkg"]}})json";
        }

        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-unres");

        // Set resolver that returns "" for unknown names
        pm.set_resolver([](const std::string&) -> std::string { return ""; });

        auto unres_result = pm.install(unres_dir);
        check(!unres_result.success, "install with unresolvable dep fails");
        check(unres_result.error.find("nonexistent-pkg") != std::string::npos, "error mentions dep name");
        check(unres_result.error.find("not found") != std::string::npos, "error mentions not found");

        // Cleanup
        fs::remove_all(vivid::PackageManager::packages_dir() + "/test-unres");
        fs::remove_all(unres_dir);
        pm.set_resolver(nullptr);
    }

    // Cleanup
    fs::remove_all(mock_pkg_dir);
    fs::remove_all(bad_pkg_dir);
    fs::remove_all(cmake_pkg_dir);
    // Clean up any leftover installed package
    if (fs::exists(pkg_install_dir))
        fs::remove_all(pkg_install_dir);
    if (fs::exists(cmake_install_dir))
        fs::remove_all(cmake_install_dir);
    // The bad package would have been cleaned up by install failure

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures;
}
