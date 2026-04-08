#include "runtime/packages/package_compiler.h"
#include "runtime/core/tool_discovery.h"
#include "runtime/platform/platform.h"
#include "runtime/platform/process_runner.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace vivid {
namespace fs = std::filesystem;

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

static std::string truncate_output(std::string output, size_t limit = 4096) {
    if (output.size() <= limit) return output;
    return output.substr(0, limit) + "\n... (truncated)";
}

static bool contains_any(const std::string& haystack, const std::initializer_list<const char*>& needles) {
    for (const char* needle : needles) {
        if (haystack.find(needle) != std::string::npos) return true;
    }
    return false;
}

static TestCompileResult inspect_cpp_test_source(const std::string& package_dir,
                                                 const std::string& test_rel_path) {
    TestCompileResult result;

    fs::path package_root = fs::absolute(package_dir).lexically_normal();
    fs::path rel_path(test_rel_path);
    result.test_name = rel_path.stem().string();
    result.normalized_rel_path = rel_path.lexically_normal().generic_string();

    if (rel_path.is_absolute()) {
        result.code = "path_outside_package";
        result.message = "Test path must be package-relative";
        return result;
    }

    fs::path source_path = (package_root / rel_path).lexically_normal();
    if (!path_within_root(package_root, source_path)) {
        result.code = "path_outside_package";
        result.message = "Test path escapes the package root";
        return result;
    }

    if (!fs::exists(source_path)) {
        result.code = "missing_test_file";
        result.message = "Test source not found: " + source_path.string();
        return result;
    }

    if (!fs::is_regular_file(source_path)) {
        result.code = "missing_test_file";
        result.message = "Test source is not a regular file: " + source_path.string();
        return result;
    }

    if (source_path.extension() != ".cpp") {
        result.code = "unsupported_test_extension";
        result.message = "Manifest cpp tests must point to .cpp files";
        return result;
    }

    std::ifstream ifs(source_path);
    if (!ifs) {
        result.code = "missing_test_file";
        result.message = "Cannot read test source: " + source_path.string();
        return result;
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string source = ss.str();

    // The generic runner only supports self-contained, single-source entrypoints
    // with an explicit main() and no external test framework linkage.
    if (contains_any(source, {
            "#include <gtest/",
            "#include <catch2/",
            "#include <doctest/",
            "TEST_CASE(",
            "SCENARIO(",
            "TEST(",
            "BENCHMARK(",
            "CATCH_CONFIG_",
            "DOCTEST_CONFIG_",
            "#include \"../",
            "#include \"..\\\\"
        })) {
        result.code = "unsupported_cpp_test_shape";
        result.message =
            "Manifest cpp tests must be self-contained single-source entrypoints. "
            "Framework-based or path-traversing tests should stay in package-local CMake/CTest.";
        return result;
    }

    if (source.find("main(") == std::string::npos) {
        result.code = "unsupported_cpp_test_shape";
        result.message =
            "Manifest cpp tests must define a standalone main(). "
            "Framework-driven tests should stay in package-local CMake/CTest.";
        return result;
    }

    result.success = true;
    result.code = "cpp_ready";
    result.message = "ready";
    return result;
}

PackageCompiler::PackageCompiler(const std::string& vivid_src_dir,
                                 const std::string& vivid_build_dir)
    : vivid_src_dir_(vivid_src_dir)
    , vivid_build_dir_(vivid_build_dir) {}

CompileResult PackageCompiler::compile_operator(const std::string& package_dir,
                                                 const std::string& operator_rel_path,
                                                 bool needs_gpu,
                                                 const std::vector<std::string>& extra_include_dirs) {
    CompileResult result;

    // operator_rel_path is e.g. "audio/drum_kick"
    // Source file: <package_dir>/operators/<domain>/<name>/<name>.cpp
    auto slash = operator_rel_path.rfind('/');
    std::string domain = (slash != std::string::npos)
        ? operator_rel_path.substr(0, slash)
        : "";
    std::string name = (slash != std::string::npos)
        ? operator_rel_path.substr(slash + 1)
        : operator_rel_path;

    result.operator_name = name;

    std::string source_path = package_dir + "/operators/" +
        operator_rel_path + "/" + name + ".cpp";

    if (!std::filesystem::exists(source_path)) {
        result.success = false;
        result.error_output = "Source file not found: " + source_path;
        return result;
    }

    // Ensure build output directory exists
    std::string build_dir = package_dir + "/build";
    std::filesystem::create_directories(build_dir);

    std::string output_path = build_dir + "/" + name + kPluginSuffix;
    std::string temp_output = output_path + ".tmp";
    result.dylib_path = output_path;
    BuildTaskId task_id = build_console_
        ? build_console_->begin_task(BuildTaskKind::PackageBuild, "operator " + name)
        : 0;

    // Resolve compiler path
    std::string compiler_exe = find_tool("clang++");
    if (compiler_exe.empty()) {
        result.success = false;
        result.error_output = missing_tool_error("clang++");
        if (build_console_) {
            build_console_->append_system_line(task_id, result.error_output);
            build_console_->finish_task(task_id, BuildTaskState::Failed, "missing clang++");
        }
        return result;
    }

    // Build compiler argv
    // -I <vivid_src>/src  — for operator_api/ headers
    // -I <package>/operators/<domain>  — for package-local shared headers
    std::string domain_include = package_dir + "/operators";
    if (!domain.empty())
        domain_include = package_dir + "/operators/" + domain;

    std::vector<std::string> argv = {
        compiler_exe, "-std=c++17", "-shared", "-fPIC", "-O2",
        "-I", vivid_src_dir_ + "/src",
        "-I", domain_include,
    };

    // Vendor / extra include directories (e.g. bundled third-party headers)
    for (const auto& dir : extra_include_dirs) {
        argv.push_back("-I");
        argv.push_back(dir);
    }

    // GPU operators need Dawn/WebGPU includes and library
    if (needs_gpu) {
        // Prefer host runtime locations first to avoid linking packages against
        // a second, stale wgpu-native binary from _deps.
        std::string wgpu_include;
        std::string wgpu_lib_dir;
        std::string sdk_candidate = vivid_src_dir_ + "/include";
        if (std::filesystem::exists(sdk_candidate + "/webgpu/webgpu.h"))
            wgpu_include = sdk_candidate;

        std::string host_bundle_lib = vivid_build_dir_ + "/vivid.app/Contents/MacOS/libwgpu_native.dylib";
        if (std::filesystem::exists(host_bundle_lib)) {
            wgpu_lib_dir = vivid_build_dir_ + "/vivid.app/Contents/MacOS";
        } else if (std::filesystem::exists(vivid_build_dir_ + "/libwgpu_native.dylib")) {
            wgpu_lib_dir = vivid_build_dir_;
        }

        // Fallback: find include/lib under _deps if host locations are unavailable.
        if (wgpu_include.empty() || wgpu_lib_dir.empty()) {
            std::string deps_dir = vivid_build_dir_ + "/_deps";
            if (std::filesystem::is_directory(deps_dir)) {
                for (auto& entry : std::filesystem::directory_iterator(deps_dir)) {
                    std::string entry_name = entry.path().filename().string();
                    if (entry_name.find("wgpu") != std::string::npos &&
                        entry_name.find("-src") != std::string::npos) {
                        if (wgpu_include.empty()) {
                            std::string include_candidate = entry.path().string() + "/include";
                            if (std::filesystem::exists(include_candidate + "/webgpu/webgpu.h"))
                                wgpu_include = include_candidate;
                        }
                        if (wgpu_lib_dir.empty()) {
                            std::string lib_candidate = entry.path().string() + "/lib";
                            if (std::filesystem::exists(lib_candidate))
                                wgpu_lib_dir = lib_candidate;
                        }
                        if (!wgpu_include.empty() && !wgpu_lib_dir.empty())
                            break;
                    }
                }
            }
        }

        if (!wgpu_include.empty()) {
            argv.push_back("-I");
            argv.push_back(wgpu_include);
        }
        if (!wgpu_lib_dir.empty()) {
            argv.push_back("-L");
            argv.push_back(wgpu_lib_dir);
            argv.push_back("-lwgpu_native");
        }
    }

    argv.push_back("-o");
    argv.push_back(temp_output);
    argv.push_back(source_path);

    std::fprintf(stderr, "[vivid] PackageCompiler: %s %s\n", compiler_exe.c_str(), name.c_str());

    // Execute compilation
    ProcessRunOptions compile_opts;
    compile_opts.argv = std::move(argv);
    compile_opts.output_limit_bytes = 1024 * 1024;  // 1MB cap on accumulated output

    ProcessRunResult compile_result;
    if (build_console_) {
        compile_result = run_build_process(compile_opts, *build_console_, task_id,
                                           BuildConsoleStreamKind::Stdout);
    } else {
        compile_result = run_process(compile_opts);
    }

    if (!compile_result.launched) {
        result.success = false;
        result.error_output = "Failed to execute compiler: " + compile_result.error;
        if (build_console_) {
            build_console_->append_system_line(task_id, result.error_output);
            build_console_->finish_task(task_id, BuildTaskState::Failed, "launch failed");
        }
        return result;
    }

    if (compile_result.exit_code != 0) {
        result.success = false;
        result.error_output = compile_result.output;
        std::error_code ec;
        std::filesystem::remove(temp_output, ec);
        std::fprintf(stderr, "[vivid] PackageCompiler: FAILED %s:\n%s",
                     name.c_str(), compile_result.output.c_str());
        if (build_console_)
            build_console_->finish_task(task_id, BuildTaskState::Failed,
                                        "failed (exit " + std::to_string(compile_result.exit_code) + ")");
    } else {
        std::error_code ec;
        std::filesystem::rename(temp_output, output_path, ec);
        if (ec) {
            result.success = false;
            result.error_output = "Failed to finalize output: " + ec.message();
            std::filesystem::remove(temp_output, ec);
            if (build_console_) {
                build_console_->append_system_line(task_id, result.error_output);
                build_console_->finish_task(task_id, BuildTaskState::Failed, "finalize failed");
            }
        } else {
            result.success = true;
            std::fprintf(stderr, "[vivid] PackageCompiler: compiled %s\n", name.c_str());
            if (build_console_)
                build_console_->finish_task(task_id, BuildTaskState::Succeeded, "succeeded");
        }
    }

    return result;
}

TestCompileResult PackageCompiler::compile_test(const std::string& package_dir,
                                                const std::string& test_rel_path,
                                                const std::vector<std::string>& extra_include_dirs) {
    TestCompileResult result = inspect_cpp_test_source(package_dir, test_rel_path);
    auto stem = result.test_name;
    BuildTaskId task_id = build_console_
        ? build_console_->begin_task(BuildTaskKind::PackageTestCompile, "test " + stem)
        : 0;
    if (!result.success) {
        result.error_output = result.message;
        if (build_console_) {
            build_console_->append_system_line(task_id, result.message);
            build_console_->finish_task(task_id, BuildTaskState::Failed,
                                        result.code.empty() ? "validation failed" : result.code);
        }
        return result;
    }

    std::string source_path =
        (fs::absolute(package_dir) / fs::path(result.normalized_rel_path)).lexically_normal().string();

    // Ensure build output directory exists
    std::string build_dir = package_dir + "/build";
    fs::create_directories(build_dir);

    std::string output_path = build_dir + "/" + stem;
    result.executable_path = output_path;

    // Resolve compiler path
    std::string compiler_exe = find_tool("clang++");
    if (compiler_exe.empty()) {
        result.success = false;
        result.error_output = missing_tool_error("clang++");
        if (build_console_) {
            build_console_->append_system_line(task_id, result.error_output);
            build_console_->finish_task(task_id, BuildTaskState::Failed, "missing clang++");
        }
        return result;
    }

    // Build compiler argv — executable, not shared library
    std::vector<std::string> test_argv = {
        compiler_exe, "-std=c++17", "-O0", "-g",
        "-I", vivid_src_dir_ + "/src",
        "-I", package_dir + "/operators",
    };

    for (const auto& dir : extra_include_dirs) {
        test_argv.push_back("-I");
        test_argv.push_back(dir);
    }

    test_argv.push_back("-o");
    test_argv.push_back(output_path);
    test_argv.push_back(source_path);

    std::fprintf(stderr, "[vivid] PackageCompiler::compile_test: %s %s\n", compiler_exe.c_str(), stem.c_str());

    ProcessRunOptions test_compile_opts;
    test_compile_opts.argv = std::move(test_argv);

    ProcessRunResult test_compile_result;
    if (build_console_) {
        test_compile_result = run_build_process(test_compile_opts, *build_console_, task_id,
                                                BuildConsoleStreamKind::Stdout);
    } else {
        test_compile_result = run_process(test_compile_opts);
    }

    if (!test_compile_result.launched) {
        result.success = false;
        result.error_output = "Failed to execute compiler: " + test_compile_result.error;
        if (build_console_) {
            build_console_->append_system_line(task_id, result.error_output);
            build_console_->finish_task(task_id, BuildTaskState::Failed, "launch failed");
        }
        return result;
    }

    if (test_compile_result.exit_code != 0) {
        result.success = false;
        result.code = "cpp_compile_failed";
        result.message = "Compilation failed";
        result.error_output = truncate_output(test_compile_result.output);
        std::error_code ec;
        fs::remove(output_path, ec);
        std::fprintf(stderr, "[vivid] PackageCompiler::compile_test: FAILED %s:\n%s",
                     stem.c_str(), test_compile_result.output.c_str());
        if (build_console_)
            build_console_->finish_task(task_id, BuildTaskState::Failed,
                                        "failed (exit " + std::to_string(test_compile_result.exit_code) + ")");
    } else {
        result.success = true;
        result.code = "cpp_compiled";
        result.message = "compiled";
        std::fprintf(stderr, "[vivid] PackageCompiler::compile_test: compiled %s\n", stem.c_str());
        if (build_console_)
            build_console_->finish_task(task_id, BuildTaskState::Succeeded, "compiled");
    }

    return result;
}

std::vector<CompileResult> PackageCompiler::compile_all(
        const std::string& package_dir,
        const std::vector<std::string>& operators,
        const std::vector<std::string>& gpu_operators,
        const std::vector<std::string>& vendor_include_dirs) {
    std::vector<CompileResult> results;

    for (const auto& op : operators)
        results.push_back(compile_operator(package_dir, op, false, vendor_include_dirs));
    for (const auto& op : gpu_operators)
        results.push_back(compile_operator(package_dir, op, true, vendor_include_dirs));

    return results;
}

std::vector<CompileResult> PackageCompiler::compile_all(const std::string& package_dir) {
    // Read vivid-package.json to extract operator lists, then delegate.
    std::string manifest_path = package_dir + "/vivid-package.json";
    std::ifstream ifs(manifest_path);
    if (!ifs) {
        CompileResult err;
        err.success = false;
        err.error_output = "Cannot read manifest: " + manifest_path;
        return {std::move(err)};
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string json_str = ss.str();

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_str);
    } catch (const nlohmann::json::parse_error&) {
        CompileResult err;
        err.success = false;
        err.error_output = "Invalid JSON in manifest: " + manifest_path;
        return {std::move(err)};
    }

    std::vector<std::string> operators;
    auto ops_it = root.find("operators");
    if (ops_it != root.end() && ops_it->is_array()) {
        for (auto& val : *ops_it) {
            if (val.is_string())
                operators.push_back(val.get<std::string>());
        }
    }

    std::vector<std::string> gpu_ops_list;
    auto gpu_it = root.find("gpu_operators");
    if (gpu_it != root.end() && gpu_it->is_array()) {
        for (auto& val : *gpu_it) {
            if (val.is_string())
                gpu_ops_list.push_back(val.get<std::string>());
        }
    }

    return compile_all(package_dir, operators, gpu_ops_list);
}

} // namespace vivid
