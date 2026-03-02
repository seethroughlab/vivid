#include "runtime/package_test_runner.h"
#include "runtime/package_manager.h"
#include "runtime/package_compiler.h"
#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <sys/wait.h>

namespace vivid {

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
        r.reason = "Failed to load graph file: " + graph_path;
        return r;
    }

    // Load operators the graph needs
    registry.load_for_graph(graph);

    // Build scheduler
    Scheduler sched;
    if (!sched.build(graph, registry)) {
        r.status = "failed";
        r.reason = "Failed to build scheduler for graph";
        sched.shutdown();
        return r;
    }

    // Skip GPU/audio tests — no device available in this context
    if (sched.has_gpu_operators()) {
        r.status = "skipped";
        r.reason = "needs GPU (no device available in test context)";
        sched.shutdown();
        return r;
    }
    if (sched.has_audio_operators()) {
        r.status = "skipped";
        r.reason = "needs audio (no audio engine in test context)";
        sched.shutdown();
        return r;
    }

    // Tick 10 frames
    for (int frame = 0; frame < 10; frame++) {
        sched.tick(frame * 0.016, 0.016, frame);
    }

    // Check for errored nodes
    for (const auto& node : sched.nodes()) {
        if (node.errored) {
            r.status = "failed";
            r.reason = "Node '" + node.node_id + "' errored: " + node.error_message;
            sched.shutdown();
            return r;
        }
    }

    sched.shutdown();
    r.status = "passed";
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
        r.reason = "Compile error";
        r.output = cr.error_output;
        // Truncate to 4KB
        if (r.output.size() > 4096)
            r.output = r.output.substr(0, 4096) + "\n... (truncated)";
        return r;
    }

    // Run executable
    std::string cmd = "'" + cr.executable_path + "' 2>&1";
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        r.status = "failed";
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
        } else {
            r.status = "failed";
            r.reason = "Exit code " + std::to_string(exit_code);
        }
    } else {
        r.status = "failed";
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

    // Resolve vendor include dirs
    std::vector<std::string> vendor_includes;
    for (const auto& vd : info.dependencies.vendor)
        vendor_includes.push_back(pkg_dir + "/" + vd.include);

    // Run graph tests
    for (const auto& graph_rel : info.tests.graphs) {
        std::string graph_path = pkg_dir + "/" + graph_rel;
        auto tr = run_graph_test(graph_path, graph_rel, registry);
        result.total++;
        if (tr.status == "passed") result.passed++;
        else if (tr.status == "failed") result.failed++;
        else if (tr.status == "skipped") result.skipped++;
        result.tests.push_back(std::move(tr));
    }

    // Run C++ tests
    for (const auto& cpp_rel : info.tests.cpp) {
        auto tr = run_cpp_test(compiler, pkg_dir, cpp_rel, vendor_includes);
        result.total++;
        if (tr.status == "passed") result.passed++;
        else if (tr.status == "failed") result.failed++;
        else if (tr.status == "skipped") result.skipped++;
        result.tests.push_back(std::move(tr));
    }

    result.success = (result.failed == 0);
    return result;
}

} // namespace vivid
