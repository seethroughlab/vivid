#include "runtime/package_test_runner.h"
#include "runtime/package_manager.h"
#include "runtime/package_compiler.h"
#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <set>
#include <sys/wait.h>

namespace vivid {
namespace fs = std::filesystem;

// Find the PackageInfo for a given package name from the installed list
static bool find_package_info(PackageManager& pm, const std::string& name, PackageInfo& out) {
    auto packages = pm.list();
    for (auto& p : packages) {
        if (p.name == name) {
            out = std::move(p);
            return true;
        }
    }
    return false;
}

static bool path_within_root(const fs::path& root, const fs::path& candidate) {
    auto abs_root = fs::absolute(root).lexically_normal();
    auto abs_candidate = fs::absolute(candidate).lexically_normal();

    auto root_it = abs_root.begin();
    auto cand_it = abs_candidate.begin();
    for (; root_it != abs_root.end() && cand_it != abs_candidate.end(); ++root_it, ++cand_it) {
        if (*root_it != *cand_it) return false;
    }
    return root_it == abs_root.end();
}

static void accumulate(PackageTestResult& result, SingleTestResult tr) {
    result.total++;
    if (tr.status == "passed") result.passed++;
    else if (tr.status == "failed") result.failed++;
    else if (tr.status == "skipped") result.skipped++;
    result.tests.push_back(std::move(tr));
}

static SingleTestResult invalid_test_result(const std::string& rel_path,
                                            const std::string& type,
                                            const std::string& code,
                                            const std::string& reason) {
    SingleTestResult r;
    r.name = rel_path;
    r.type = type;
    r.status = "failed";
    r.code = code;
    r.reason = reason;
    return r;
}

static SingleTestResult validate_graph_test_path(const fs::path& package_root,
                                                 const std::string& rel_path) {
    fs::path rel(rel_path);
    if (rel.is_absolute()) {
        return invalid_test_result(rel_path, "graph", "path_outside_package",
                                   "Graph test path must be package-relative");
    }

    fs::path graph_path = (package_root / rel).lexically_normal();
    if (!path_within_root(package_root, graph_path)) {
        return invalid_test_result(rel_path, "graph", "path_outside_package",
                                   "Graph test path escapes the package root");
    }
    if (!fs::exists(graph_path) || !fs::is_regular_file(graph_path)) {
        return invalid_test_result(rel_path, "graph", "missing_test_file",
                                   "Graph test file not found: " + graph_path.string());
    }
    if (graph_path.extension() != ".json") {
        return invalid_test_result(rel_path, "graph", "unsupported_graph_test_shape",
                                   "Manifest graph tests must point to .json files");
    }

    return {};
}

static SingleTestResult run_graph_test(const std::string& graph_path,
                                        const std::string& rel_path,
                                        OperatorRegistry& registry) {
    SingleTestResult r;
    r.name = rel_path;
    r.type = "graph";

    // Load graph
    Graph graph;
    if (!graph.load(graph_path.c_str())) {
        r.status = "failed";
        r.code = "graph_load_failed";
        r.reason = "Failed to load graph file: " + graph_path;
        return r;
    }

    // Load operators the graph needs
    registry.load_for_graph(graph);

    // Build runtime
    RuntimeCore runtime;
    if (!runtime.build(graph, registry)) {
        r.status = "failed";
        r.code = "graph_build_failed";
        r.reason = "Failed to build runtime for graph";
        runtime.shutdown();
        return r;
    }

    // Skip GPU/audio tests — no device available in this context
    if (runtime.has_gpu_operators()) {
        r.status = "skipped";
        r.code = "graph_needs_gpu";
        r.reason = "needs GPU (no device available in test context)";
        runtime.shutdown();
        return r;
    }
    if (runtime.has_audio_operators()) {
        r.status = "skipped";
        r.code = "graph_needs_audio";
        r.reason = "needs audio (no audio engine in test context)";
        runtime.shutdown();
        return r;
    }

    // Tick 10 frames
    for (int frame = 0; frame < 10; frame++) {
        runtime.tick(frame * 0.016, 0.016, frame);
    }

    // Check for errored nodes
    for (const auto& node : runtime.compiled_graph()->nodes) {
        if (node.errored) {
            r.status = "failed";
            r.code = "graph_node_error";
            r.reason = "Node '" + node.node_id + "' errored: " + node.error_message;
            runtime.shutdown();
            return r;
        }
    }

    runtime.shutdown();
    r.status = "passed";
    r.code = "graph_passed";
    return r;
}

static SingleTestResult run_cpp_test(PackageCompiler& compiler,
                                      const std::string& package_dir,
                                      const std::string& rel_path,
                                      const std::vector<std::string>& vendor_includes) {
    SingleTestResult r;
    r.name = rel_path;
    r.type = "cpp";

    // Compile
    auto cr = compiler.compile_test(package_dir, rel_path, vendor_includes);
    if (!cr.success) {
        r.status = "failed";
        r.code = cr.code.empty() ? "cpp_compile_failed" : cr.code;
        r.reason = cr.message.empty() ? "Compile error" : cr.message;
        r.output = cr.error_output;
        return r;
    }

    // Run executable
    std::string cmd = "'" + cr.executable_path + "' 2>&1";
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        r.status = "failed";
        r.code = "cpp_runtime_launch_failed";
        r.reason = "Failed to execute test binary";
        return r;
    }

    std::array<char, 256> buf;
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        output += buf.data();
        // Cap captured output at 4KB
        if (output.size() > 4096) {
            output = output.substr(0, 4096) + "\n... (truncated)";
            break;
        }
    }
    int status = pclose(pipe);

    r.output = output;

    // pclose returns the wait status; extract exit code
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) {
            r.status = "passed";
            r.code = "cpp_passed";
        } else {
            r.status = "failed";
            r.code = "cpp_runtime_failed";
            r.reason = "Exit code " + std::to_string(exit_code);
        }
    } else {
        r.status = "failed";
        r.code = "cpp_runtime_abnormal";
        r.reason = "Test process terminated abnormally";
    }

    return r;
}

PackageTestResult run_package_tests(const std::string& name,
                                     PackageManager& pm,
                                     PackageCompiler& compiler,
                                     OperatorRegistry& registry) {
    PackageTestResult result;
    result.package_name = name;

    // Verify package exists
    if (!pm.is_installed(name)) {
        result.error = "Package not installed: " + name;
        return result;
    }

    // Get package info
    PackageInfo info;
    if (!find_package_info(pm, name, info)) {
        result.error = "Failed to read package info for: " + name;
        return result;
    }

    std::string pkg_dir = PackageManager::packages_dir() + "/" + name;
    fs::path package_root = fs::absolute(pkg_dir).lexically_normal();

    // Resolve vendor include dirs
    std::vector<std::string> vendor_includes;
    for (const auto& vd : info.dependencies.vendor)
        vendor_includes.push_back(pkg_dir + "/" + vd.include);

    if (info.tests.graphs.empty() && info.tests.cpp.empty()) {
        result.notes.push_back(
            "Package declares no manifest tests. Package-local CMake/CTest may still provide coverage.");
    }

    std::set<std::string> seen_graphs;
    // Run graph tests
    for (const auto& graph_rel : info.tests.graphs) {
        if (!seen_graphs.insert(graph_rel).second) {
            accumulate(result, invalid_test_result(
                graph_rel, "graph", "duplicate_test_entry",
                "Duplicate graph test entry in manifest"));
            continue;
        }
        auto validation = validate_graph_test_path(package_root, graph_rel);
        if (!validation.code.empty()) {
            accumulate(result, std::move(validation));
            continue;
        }

        std::string graph_path = (package_root / fs::path(graph_rel)).lexically_normal().string();
        accumulate(result, run_graph_test(graph_path, graph_rel, registry));
    }

    bool saw_unsupported_cpp = false;
    std::set<std::string> seen_cpp;
    // Run C++ tests
    for (const auto& cpp_rel : info.tests.cpp) {
        if (!seen_cpp.insert(cpp_rel).second) {
            accumulate(result, invalid_test_result(
                cpp_rel, "cpp", "duplicate_test_entry",
                "Duplicate cpp test entry in manifest"));
            continue;
        }

        auto tr = run_cpp_test(compiler, pkg_dir, cpp_rel, vendor_includes);
        saw_unsupported_cpp = saw_unsupported_cpp || tr.code == "unsupported_cpp_test_shape";
        accumulate(result, std::move(tr));
    }

    if (saw_unsupported_cpp) {
        result.notes.push_back(
            "Some manifest cpp tests are outside the generic runner contract and should remain in package-local CMake/CTest.");
    }

    result.success = (result.failed == 0);
    return result;
}

} // namespace vivid
