// Test: representative ecosystem package-test contract coverage
#include "runtime/packages/package_test_runner.h"
#include "runtime/packages/package_manager.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/operators/builtin_operators.h"
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

static void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream ofs(path);
    ofs << text;
}

static std::string find_source_dir(const std::string& build_dir) {
    namespace fs = std::filesystem;
    auto candidate = fs::path(build_dir).parent_path();
    for (int i = 0; i < 3; ++i) {
        if (fs::exists(candidate / "CMakeLists.txt") &&
            fs::exists(candidate / "src" / "runtime")) {
            return candidate.string();
        }
        candidate = candidate.parent_path();
    }
    return {};
}

int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;

    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    std::string source_dir = find_source_dir(build_dir);
    if (source_dir.empty()) {
        std::fprintf(stderr, "Cannot determine source directory, skipping test\n");
        return 0;
    }

    const char* old_home = std::getenv("HOME");
    std::string old_home_val = old_home ? old_home : "";
    fs::path test_home = fs::path(build_dir) / ".test_package_contract_home";
    fs::remove_all(test_home);
    fs::create_directories(test_home);
    setenv("HOME", test_home.string().c_str(), 1);

    vivid::OperatorRegistry registry;
    registry.scan_deferred(build_dir.c_str());
    register_builtin_operators(registry);

    vivid::PackageCompiler compiler(source_dir, build_dir);
    vivid::PackageManager pm(compiler, registry);

    std::fprintf(stderr, "\n=== Test: Package Contract Ecosystem ===\n\n");

    // Graph-only package
    std::string graph_pkg_dir = build_dir + "/.test_pkg_contract_graph";
    fs::remove_all(graph_pkg_dir);
    write_text(fs::path(graph_pkg_dir) / "tests/basic.json", R"json({
  "nodes": {
    "pass1": { "type": "ControlPassOp" }
  },
  "connections": []
})json");
    write_text(fs::path(graph_pkg_dir) / "vivid-package.json", R"json({
  "name": "pkg-contract-graph",
  "version": "0.1.0",
  "operators": [],
  "tests": { "graphs": ["tests/basic.json"] }
})json");
    check(pm.link(graph_pkg_dir).success, "graph-only package links");
    auto graph_result = vivid::run_package_tests("pkg-contract-graph", pm, compiler, registry);
    check(graph_result.success, "graph-only package passes manifest tests");
    check(graph_result.total == 1 && graph_result.passed == 1, "graph-only package summary is correct");

    // Lightweight manifest cpp package
    std::string cpp_pkg_dir = build_dir + "/.test_pkg_contract_cpp";
    fs::remove_all(cpp_pkg_dir);
    write_text(fs::path(cpp_pkg_dir) / "tests/light.cpp", R"cpp(
#include <cstdio>
int main() {
    std::fprintf(stderr, "light manifest cpp test\n");
    return 0;
}
)cpp");
    write_text(fs::path(cpp_pkg_dir) / "vivid-package.json", R"json({
  "name": "pkg-contract-cpp",
  "version": "0.1.0",
  "operators": [],
  "tests": { "cpp": ["tests/light.cpp"] }
})json");
    check(pm.link(cpp_pkg_dir).success, "lightweight cpp package links");
    auto cpp_result = vivid::run_package_tests("pkg-contract-cpp", pm, compiler, registry);
    check(cpp_result.success, "lightweight cpp package passes manifest tests");
    check(cpp_result.total == 1 && cpp_result.passed == 1, "lightweight cpp package summary is correct");
    check(cpp_result.tests.size() == 1 && cpp_result.tests[0].code == "cpp_passed",
          "lightweight cpp package uses the generic runner");

    // Heavier package-local CMake case, intentionally unsupported by generic runner
    std::string heavy_pkg_dir = build_dir + "/.test_pkg_contract_heavy";
    fs::remove_all(heavy_pkg_dir);
    write_text(fs::path(heavy_pkg_dir) / "tests/framework.cpp", R"cpp(
#include <doctest/doctest.h>
TEST_CASE("heavy package-local test") {
    CHECK(true);
}
)cpp");
    write_text(fs::path(heavy_pkg_dir) / "vivid-package.json", R"json({
  "name": "pkg-contract-heavy",
  "version": "0.1.0",
  "operators": [],
  "tests": { "cpp": ["tests/framework.cpp"] }
})json");
    check(pm.link(heavy_pkg_dir).success, "heavy cpp package links");
    auto heavy_result = vivid::run_package_tests("pkg-contract-heavy", pm, compiler, registry);
    check(!heavy_result.success, "heavy package is not silently treated as supported");
    check(heavy_result.tests.size() == 1 &&
          heavy_result.tests[0].code == "unsupported_cpp_test_shape",
          "heavy package is classified as unsupported_cpp_test_shape");
    bool saw_note = false;
    for (const auto& note : heavy_result.notes) {
        if (note.find("package-local CMake/CTest") != std::string::npos)
            saw_note = true;
    }
    check(saw_note, "heavy package result points authors at package-local CMake/CTest");

    pm.unlink("pkg-contract-heavy");
    pm.unlink("pkg-contract-cpp");
    pm.unlink("pkg-contract-graph");
    fs::remove_all(heavy_pkg_dir);
    fs::remove_all(cpp_pkg_dir);
    fs::remove_all(graph_pkg_dir);
    fs::remove_all(test_home);

    if (old_home) setenv("HOME", old_home_val.c_str(), 1);
    else unsetenv("HOME");

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures;
}
