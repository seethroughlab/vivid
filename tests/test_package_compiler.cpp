// Test: PackageCompiler — compile a test operator from a mock package directory
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

    // Find source directory (parent of build_dir that has CMakeLists.txt)
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
        return 0;  // Skip rather than fail in CI environments
    }

    std::fprintf(stderr, "\n=== Test: PackageCompiler ===\n\n");

    // Create a mock package directory with a simple operator
    std::string pkg_dir = build_dir + "/.test_package";
    fs::create_directories(pkg_dir + "/operators/control/test_pkg_op");
    fs::create_directories(pkg_dir + "/build");

    // Write a simple operator source
    {
        std::ofstream ofs(pkg_dir + "/operators/control/test_pkg_op/test_pkg_op.cpp");
        ofs << R"cpp(
#include "operator_api/operator.h"

struct TestPkgOp : vivid::OperatorBase {
    static constexpr const char* kName   = "TestPkgOp";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> value{"value", 42.0f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&value);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0];
    }
};

VIVID_REGISTER(TestPkgOp)
)cpp";
    }

    // Write a manifest
    {
        std::ofstream ofs(pkg_dir + "/vivid-package.json");
        ofs << R"json({
  "name": "test-package",
  "version": "0.1.0",
  "description": "Test package for unit tests",
  "operators": ["control/test_pkg_op"],
  "gpu_operators": []
})json";
    }

    // --- Test 1: Compile single operator ---
    std::fprintf(stderr, "--- Compile single operator ---\n");
    vivid::PackageCompiler compiler(source_dir, build_dir);
    auto result = compiler.compile_operator(pkg_dir, "control/test_pkg_op", false);
    check(result.success, "compile_operator succeeds");
    check(!result.dylib_path.empty(), "dylib_path is set");
    check(result.operator_name == "test_pkg_op", "operator_name is correct");

    if (result.success) {
        check(fs::exists(result.dylib_path), "compiled dylib exists on disk");

        // --- Test 2: Load compiled operator into registry ---
        std::fprintf(stderr, "\n--- Load into registry ---\n");
        vivid::OperatorRegistry registry;
        check(registry.scan_deferred((pkg_dir + "/build").c_str()),
              "scan_deferred succeeds on package build dir");

        // The operator should be discoverable
        auto names = registry.type_names();
        bool found = false;
        for (const auto& n : names) {
            if (n == "TestPkgOp") { found = true; break; }
        }
        check(found, "TestPkgOp found in registry type_names");

        // Probe descriptor
        auto* desc = registry.probe_descriptor("TestPkgOp");
        check(desc != nullptr, "probe_descriptor returns non-null");
        if (desc) {
            check(desc->domain == VIVID_DOMAIN_CONTROL, "domain is control");
            check(desc->param_count == 1, "has 1 param");
        }
    } else {
        std::fprintf(stderr, "  Compile error: %s\n", result.error_output.c_str());
    }

    // --- Test 3: Compile all from manifest ---
    std::fprintf(stderr, "\n--- Compile all from manifest ---\n");
    // Remove previous build to force recompile
    fs::remove_all(pkg_dir + "/build");
    auto results = compiler.compile_all(pkg_dir);
    check(results.size() == 1, "compile_all returns 1 result");
    if (!results.empty()) {
        check(results[0].success, "compile_all: operator compiled successfully");
    }

    // --- Test 4: Missing source file ---
    std::fprintf(stderr, "\n--- Missing source file ---\n");
    auto bad_result = compiler.compile_operator(pkg_dir, "audio/nonexistent", false);
    check(!bad_result.success, "compile_operator fails for missing source");
    check(!bad_result.error_output.empty(), "error_output is set");

    // --- Test 5: Compile with vendor include dirs ---
    std::fprintf(stderr, "\n--- Compile with vendor include dirs ---\n");
    {
        // Create a vendored header
        fs::create_directories(pkg_dir + "/deps/testlib");
        {
            std::ofstream ofs(pkg_dir + "/deps/testlib/testlib.h");
            ofs << "#pragma once\n"
                   "static constexpr float TESTLIB_MAGIC = 123.456f;\n";
        }

        // Write an operator that uses the vendored header
        fs::create_directories(pkg_dir + "/operators/control/test_vendor_op");
        {
            std::ofstream ofs(pkg_dir + "/operators/control/test_vendor_op/test_vendor_op.cpp");
            ofs << R"cpp(
#include "operator_api/operator.h"
#include "testlib.h"

struct TestVendorOp : vivid::OperatorBase {
    static constexpr const char* kName   = "TestVendorOp";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> value{"value", TESTLIB_MAGIC, 0.0f, 200.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&value);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0];
    }
};

VIVID_REGISTER(TestVendorOp)
)cpp";
        }

        // Compile with vendor include dir — should succeed
        auto vendor_result = compiler.compile_operator(
            pkg_dir, "control/test_vendor_op", false,
            {pkg_dir + "/deps/testlib"});
        check(vendor_result.success, "compile with vendor include succeeds");
        if (vendor_result.success) {
            check(fs::exists(vendor_result.dylib_path), "vendor op dylib exists");
        } else {
            std::fprintf(stderr, "  Error: %s\n", vendor_result.error_output.c_str());
        }

        // Compile WITHOUT vendor include dir — should fail
        fs::remove_all(pkg_dir + "/build");
        auto no_vendor_result = compiler.compile_operator(
            pkg_dir, "control/test_vendor_op", false);
        check(!no_vendor_result.success, "compile without vendor include fails");
    }

    // Cleanup
    fs::remove_all(pkg_dir);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures;
}
