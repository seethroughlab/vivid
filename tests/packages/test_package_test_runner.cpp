// Test: PackageTestRunner — manifest test contract classification and execution
#include "runtime/packages/package_test_runner.h"
#include "runtime/packages/package_manager.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/operators/builtin_operators.h"
#include "runtime/core/build_console.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unordered_map>

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

static vivid::SingleTestResult* find_test(vivid::PackageTestResult& result,
                                          const std::string& name) {
    for (auto& test : result.tests) {
        if (test.name == name) return &test;
    }
    return nullptr;
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
    fs::path test_home = fs::path(build_dir) / ".test_package_test_runner_home";
    fs::remove_all(test_home);
    fs::create_directories(test_home);
    setenv("HOME", test_home.string().c_str(), 1);

    std::fprintf(stderr, "\n=== Test: PackageTestRunner ===\n\n");

    vivid::OperatorRegistry registry;
    registry.scan_deferred(build_dir.c_str());
    register_builtin_operators(registry);

    vivid::PackageCompiler compiler(source_dir, build_dir);
    vivid::BuildConsole build_console;
    compiler.set_build_console(&build_console);
    vivid::PackageManager pm(compiler, registry);

    // --- Basic package: graph pass + cpp pass + cpp runtime failure ---
    std::string basic_pkg_dir = build_dir + "/.test_runner_basic_package";
    fs::remove_all(basic_pkg_dir);
    fs::create_directories(basic_pkg_dir + "/tests");

    write_text(fs::path(basic_pkg_dir) / "tests/basic.json", R"json({
  "nodes": {
    "pass1": { "type": "ControlPassOp", "params": { "gain": 2.0 } }
  },
  "connections": []
})json");

    write_text(fs::path(basic_pkg_dir) / "tests/test_pass.cpp", R"cpp(
#include <cstdio>
int main() {
    std::fprintf(stderr, "test_pass: OK\n");
    return 0;
}
)cpp");

    write_text(fs::path(basic_pkg_dir) / "tests/test_fail.cpp", R"cpp(
#include <cstdio>
int main() {
    std::fprintf(stderr, "test_fail: deliberately failing\n");
    return 1;
}
)cpp");

    write_text(fs::path(basic_pkg_dir) / "vivid-package.json", R"json({
  "name": "test-runner-basic-package",
  "version": "0.1.0",
  "description": "Mock package for test runner happy-path tests",
  "operators": [],
  "gpu_operators": [],
  "tests": {
    "graphs": ["tests/basic.json"],
    "cpp": ["tests/test_pass.cpp", "tests/test_fail.cpp"]
  }
})json");

    auto basic_install = pm.link(basic_pkg_dir);
    check(basic_install.success, "basic package links");
    if (!basic_install.success) {
        std::fprintf(stderr, "Link error: %s\n", basic_install.error.c_str());
        return 1;
    }

    auto basic = vivid::run_package_tests("test-runner-basic-package", pm, compiler, registry);
    check(basic.package_name == "test-runner-basic-package", "basic package_name correct");
    check(basic.total == 3, "basic total == 3");
    check(basic.passed == 2, "basic passed == 2");
    check(basic.failed == 1, "basic failed == 1");
    check(basic.skipped == 0, "basic skipped == 0");
    check(!basic.success, "basic overall success == false");

    auto* basic_graph = find_test(basic, "tests/basic.json");
    check(basic_graph && basic_graph->code == "graph_passed", "graph pass code is stable");
    auto* basic_cpp_pass = find_test(basic, "tests/test_pass.cpp");
    check(basic_cpp_pass && basic_cpp_pass->code == "cpp_passed", "cpp pass code is stable");
    auto* basic_cpp_fail = find_test(basic, "tests/test_fail.cpp");
    check(basic_cpp_fail && basic_cpp_fail->code == "cpp_runtime_failed", "cpp runtime failure code is stable");
    auto console_snapshot = build_console.snapshot();
    bool saw_test_output = false;
    for (const auto& line : console_snapshot.lines) {
        if (line.text.find("test_pass: OK") != std::string::npos) {
            saw_test_output = true;
            break;
        }
    }
    check(saw_test_output, "build console captured cpp test stdout");

    // --- Validation package: deterministic graph/cpp classification ---
    std::string validation_pkg_dir = build_dir + "/.test_runner_validation_package";
    fs::remove_all(validation_pkg_dir);
    fs::create_directories(validation_pkg_dir + "/tests");

    write_text(fs::path(validation_pkg_dir) / "tests/needs_gpu.json", R"json({
  "nodes": {
    "noise1": { "type": "NoiseTexture" }
  },
  "connections": []
})json");

    write_text(fs::path(validation_pkg_dir) / "tests/needs_audio.json", R"json({
  "nodes": {
    "osc1": { "type": "Oscillator" }
  },
  "connections": []
})json");

    write_text(fs::path(validation_pkg_dir) / "tests/not_json.txt", "not a graph");

    write_text(fs::path(validation_pkg_dir) / "tests/test_good.cpp", R"cpp(
#include <cstdio>
int main() {
    std::fprintf(stderr, "good cpp test\n");
    return 0;
}
)cpp");

    write_text(fs::path(validation_pkg_dir) / "tests/compile_fail.cpp", R"cpp(
#include <cstdio>
int main() {
    return missing_symbol;
}
)cpp");

    write_text(fs::path(validation_pkg_dir) / "tests/not_cpp.cc", R"cpp(
int main() { return 0; }
)cpp");

    write_text(fs::path(validation_pkg_dir) / "tests/framework.cpp", R"cpp(
#include <gtest/gtest.h>
TEST(Smoke, Works) { EXPECT_EQ(1, 1); }
)cpp");

    write_text(fs::path(validation_pkg_dir) / "vivid-package.json", R"json({
  "name": "test-runner-validation-package",
  "version": "0.1.0",
  "operators": [],
  "gpu_operators": [],
  "tests": {
    "graphs": [
      "tests/needs_gpu.json",
      "tests/needs_audio.json",
      "tests/missing.json",
      "../escaped.json",
      "tests/not_json.txt",
      "tests/needs_gpu.json"
    ],
    "cpp": [
      "tests/test_good.cpp",
      "tests/compile_fail.cpp",
      "tests/framework.cpp",
      "tests/missing.cpp",
      "../escaped.cpp",
      "tests/not_cpp.cc",
      "tests/test_good.cpp"
    ]
  }
})json");

    auto validation_install = pm.link(validation_pkg_dir);
    check(validation_install.success, "validation package links");
    if (!validation_install.success) {
        std::fprintf(stderr, "Link error: %s\n", validation_install.error.c_str());
        return 1;
    }

    auto validation = vivid::run_package_tests("test-runner-validation-package", pm, compiler, registry);
    check(validation.total == 13, "validation total == 13");
    check(validation.passed == 1, "validation passed == 1");
    check(validation.failed == 10, "validation failed == 10");
    check(validation.skipped == 2, "validation skipped == 2");
    check(!validation.success, "validation overall success == false");
    check(!validation.notes.empty(), "validation emits package-level notes");

    auto* needs_gpu = find_test(validation, "tests/needs_gpu.json");
    check(needs_gpu && needs_gpu->code == "graph_needs_gpu", "graph gpu skip code is stable");
    auto* needs_audio = find_test(validation, "tests/needs_audio.json");
    check(needs_audio && needs_audio->code == "graph_needs_audio", "graph audio skip code is stable");
    auto* missing_graph = find_test(validation, "tests/missing.json");
    check(missing_graph && missing_graph->code == "missing_test_file", "missing graph file code is stable");
    auto* escaped_graph = find_test(validation, "../escaped.json");
    check(escaped_graph && escaped_graph->code == "path_outside_package", "escaped graph path code is stable");
    auto* bad_graph_ext = find_test(validation, "tests/not_json.txt");
    check(bad_graph_ext && bad_graph_ext->code == "unsupported_graph_test_shape", "graph extension classification is stable");

    auto* good_cpp = find_test(validation, "tests/test_good.cpp");
    check(good_cpp && good_cpp->code == "cpp_passed", "good cpp test passes");
    auto* compile_fail = find_test(validation, "tests/compile_fail.cpp");
    check(compile_fail && compile_fail->code == "cpp_compile_failed", "compile failure code is stable");
    auto* framework_cpp = find_test(validation, "tests/framework.cpp");
    check(framework_cpp && framework_cpp->code == "unsupported_cpp_test_shape", "framework cpp shape is rejected early");
    auto* missing_cpp = find_test(validation, "tests/missing.cpp");
    check(missing_cpp && missing_cpp->code == "missing_test_file", "missing cpp file code is stable");
    auto* escaped_cpp = find_test(validation, "../escaped.cpp");
    check(escaped_cpp && escaped_cpp->code == "path_outside_package", "escaped cpp path code is stable");
    auto* bad_cpp_ext = find_test(validation, "tests/not_cpp.cc");
    check(bad_cpp_ext && bad_cpp_ext->code == "unsupported_test_extension", "cpp extension classification is stable");

    int duplicate_count = 0;
    for (const auto& test : validation.tests) {
        if (test.code == "duplicate_test_entry") duplicate_count++;
    }
    check(duplicate_count == 2, "duplicate test entries are reported deterministically");

    bool saw_unsupported_note = false;
    for (const auto& note : validation.notes) {
        if (note.find("package-local CMake/CTest") != std::string::npos) {
            saw_unsupported_note = true;
        }
    }
    check(saw_unsupported_note, "unsupported cpp note points authors at package-local CMake/CTest");

    // --- Package with no manifest tests ---
    std::string empty_pkg_dir = build_dir + "/.test_empty_tests_package";
    fs::remove_all(empty_pkg_dir);
    fs::create_directories(empty_pkg_dir);
    write_text(fs::path(empty_pkg_dir) / "vivid-package.json", R"json({
  "name": "test-empty-tests-package",
  "version": "0.1.0",
  "operators": []
})json");

    auto empty_install = pm.link(empty_pkg_dir);
    check(empty_install.success, "empty-tests package links");
    if (!empty_install.success) {
        std::fprintf(stderr, "Link error: %s\n", empty_install.error.c_str());
        return 1;
    }

    auto empty = vivid::run_package_tests("test-empty-tests-package", pm, compiler, registry);
    check(empty.success, "empty package test run succeeds");
    check(empty.total == 0, "empty package total == 0");
    check(empty.notes.size() == 1, "empty package reports a coverage note");

    // --- Non-existent package ---
    auto bad_result = vivid::run_package_tests("nonexistent-package", pm, compiler, registry);
    check(!bad_result.success, "non-existent package fails");
    check(!bad_result.error.empty(), "non-existent package returns error");

    // Cleanup
    pm.unlink("test-empty-tests-package");
    pm.unlink("test-runner-validation-package");
    pm.unlink("test-runner-basic-package");
    fs::remove_all(empty_pkg_dir);
    fs::remove_all(validation_pkg_dir);
    fs::remove_all(basic_pkg_dir);
    fs::remove_all(test_home);

    if (old_home) setenv("HOME", old_home_val.c_str(), 1);
    else unsetenv("HOME");

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures;
}
