// Test: PackageTestRunner — run graph and C++ tests for a mock package
#include "runtime/package_test_runner.h"
#include "runtime/package_manager.h"
#include "runtime/package_compiler.h"
#include "runtime/operator_registry.h"
#include "runtime/builtin_operators.h"
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

    std::fprintf(stderr, "\n=== Test: PackageTestRunner ===\n\n");

    // --- Setup ---
    vivid::OperatorRegistry registry;
    registry.scan_deferred(build_dir.c_str());
    register_builtin_operators(registry);

    vivid::PackageCompiler compiler(source_dir, build_dir);
    vivid::PackageManager pm(compiler, registry);

    // Create a mock package with tests
    std::string mock_pkg_dir = build_dir + "/.test_runner_package";
    fs::remove_all(mock_pkg_dir);
    fs::create_directories(mock_pkg_dir + "/tests");

    // Write a test graph using ControlPassOp (available from scanned build dir)
    {
        std::ofstream ofs(mock_pkg_dir + "/tests/basic.json");
        ofs << R"json({
  "nodes": {
    "pass1": { "type": "ControlPassOp", "params": { "gain": 2.0 } }
  },
  "connections": []
})json";
    }

    // Write a passing C++ test
    {
        std::ofstream ofs(mock_pkg_dir + "/tests/test_pass.cpp");
        ofs << R"cpp(
#include <cstdio>
int main() {
    std::fprintf(stderr, "test_pass: OK\n");
    return 0;
}
)cpp";
    }

    // Write a failing C++ test
    {
        std::ofstream ofs(mock_pkg_dir + "/tests/test_fail.cpp");
        ofs << R"cpp(
#include <cstdio>
int main() {
    std::fprintf(stderr, "test_fail: deliberately failing\n");
    return 1;
}
)cpp";
    }

    // Write manifest with tests
    {
        std::ofstream ofs(mock_pkg_dir + "/vivid-package.json");
        ofs << R"json({
  "name": "test-runner-package",
  "version": "0.1.0",
  "description": "Mock package for test runner tests",
  "operators": [],
  "gpu_operators": [],
  "tests": {
    "graphs": ["tests/basic.json"],
    "cpp": ["tests/test_pass.cpp", "tests/test_fail.cpp"]
  }
})json";
    }

    // Install the mock package
    std::string pkg_install_dir = vivid::PackageManager::packages_dir() + "/test-runner-package";
    fs::remove_all(pkg_install_dir);

    auto install_result = pm.install(mock_pkg_dir);
    check(install_result.success, "mock package installs");
    if (!install_result.success) {
        std::fprintf(stderr, "  Install error: %s\n", install_result.error.c_str());
        fs::remove_all(mock_pkg_dir);
        std::fprintf(stderr, "\n=== ABORTED (install failed) ===\n");
        return 1;
    }

    // --- Test 1: Run tests on mock package ---
    std::fprintf(stderr, "\n--- Run tests on mock package ---\n");
    auto result = vivid::run_package_tests("test-runner-package", pm, compiler, registry);

    check(result.package_name == "test-runner-package", "package_name correct");
    check(result.total == 3, "total == 3");
    check(result.passed == 2, "passed == 2 (graph + passing cpp)");
    check(result.failed == 1, "failed == 1 (failing cpp)");
    check(result.skipped == 0, "skipped == 0");
    check(!result.success, "overall success == false (has failures)");
    check(result.error.empty(), "no error string");

    // Verify individual test results
    if (result.tests.size() == 3) {
        // Graph test
        check(result.tests[0].name == "tests/basic.json", "test[0] name");
        check(result.tests[0].type == "graph", "test[0] type == graph");
        check(result.tests[0].status == "passed", "test[0] passed");

        // Passing C++ test
        check(result.tests[1].name == "tests/test_pass.cpp", "test[1] name");
        check(result.tests[1].type == "cpp", "test[1] type == cpp");
        check(result.tests[1].status == "passed", "test[1] passed");

        // Failing C++ test
        check(result.tests[2].name == "tests/test_fail.cpp", "test[2] name");
        check(result.tests[2].type == "cpp", "test[2] type == cpp");
        check(result.tests[2].status == "failed", "test[2] failed");
        check(!result.tests[2].reason.empty(), "test[2] has failure reason");
    } else {
        std::fprintf(stderr, "  FAIL: expected 3 test results, got %zu\n", result.tests.size());
        failures++;
    }

    // --- Test 2: Non-existent package ---
    std::fprintf(stderr, "\n--- Non-existent package ---\n");
    auto bad_result = vivid::run_package_tests("nonexistent-package", pm, compiler, registry);
    check(!bad_result.success, "non-existent package fails");
    check(!bad_result.error.empty(), "error message set");

    // --- Test 3: Package with no tests ---
    std::fprintf(stderr, "\n--- Package with no tests ---\n");
    {
        std::string empty_pkg_dir = build_dir + "/.test_empty_tests_package";
        fs::remove_all(empty_pkg_dir);
        fs::create_directories(empty_pkg_dir);
        {
            std::ofstream ofs(empty_pkg_dir + "/vivid-package.json");
            ofs << R"json({
  "name": "test-empty-tests-package",
  "version": "0.1.0",
  "operators": []
})json";
        }

        std::string empty_install = vivid::PackageManager::packages_dir() + "/test-empty-tests-package";
        fs::remove_all(empty_install);

        auto empty_install_res = pm.install(empty_pkg_dir);
        check(empty_install_res.success, "empty-tests package installs");

        auto empty_result = vivid::run_package_tests("test-empty-tests-package", pm, compiler, registry);
        check(empty_result.success, "empty tests returns success");
        check(empty_result.total == 0, "total == 0");
        check(empty_result.passed == 0, "passed == 0");
        check(empty_result.failed == 0, "failed == 0");
        check(empty_result.error.empty(), "no error");

        pm.uninstall("test-empty-tests-package");
        fs::remove_all(empty_pkg_dir);
    }

    // --- Cleanup ---
    pm.uninstall("test-runner-package");
    fs::remove_all(mock_pkg_dir);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures;
}
