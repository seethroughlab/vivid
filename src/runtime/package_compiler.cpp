#include "runtime/package_compiler.h"
#include "runtime/tool_discovery.h"
#include "runtime/platform.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "yyjson.h"

namespace vivid {
namespace fs = std::filesystem;

// Shell-quote a string to handle spaces and special characters (including single quotes)
static std::string quote(const std::string& s) {
    std::string escaped;
    for (char c : s) {
        if (c == '\'') escaped += "'\\''";
        else escaped += c;
    }
    return "'" + escaped + "'";
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

    // Resolve compiler path
    std::string compiler_exe = find_tool("clang++");
    if (compiler_exe.empty()) {
        result.success = false;
        result.error_output = missing_tool_error("clang++");
        return result;
    }

    // Build compiler command
    // -I <vivid_src>/src  — for operator_api/ headers
    // -I <package>/operators/<domain>  — for package-local shared headers
    std::string domain_include = package_dir + "/operators";
    if (!domain.empty())
        domain_include = package_dir + "/operators/" + domain;

    std::string cmd = quote(compiler_exe) + " -std=c++17 -shared -fPIC -O2"
        " -I " + quote(vivid_src_dir_ + "/src") +
        " -I " + quote(domain_include);

    // Vendor / extra include directories (e.g. bundled third-party headers)
    for (const auto& dir : extra_include_dirs)
        cmd += " -I " + quote(dir);

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
            cmd += " -I " + quote(wgpu_include);
        }
        if (!wgpu_lib_dir.empty()) {
            cmd += " -L " + quote(wgpu_lib_dir) + " -lwgpu_native";
        }
    }

    cmd += " -o " + quote(temp_output) + " " + quote(source_path) + " 2>&1";

    std::fprintf(stderr, "[vivid] PackageCompiler: %s\n", cmd.c_str());

    // Execute compilation
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.success = false;
        result.error_output = "Failed to execute compiler";
        return result;
    }

    std::array<char, 256> buf;
    bool output_truncated = false;
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        if (output.size() < 1024 * 1024)
            output += buf.data();
        else if (!output_truncated) {
            output += "\n... (compiler output truncated at 1MB) ...\n";
            output_truncated = true;
        }
    }
    int status = pclose(pipe);

    if (status != 0) {
        result.success = false;
        result.error_output = output;
        std::error_code ec;
        std::filesystem::remove(temp_output, ec);
        std::fprintf(stderr, "[vivid] PackageCompiler: FAILED %s:\n%s",
                     name.c_str(), output.c_str());
    } else {
        std::error_code ec;
        std::filesystem::rename(temp_output, output_path, ec);
        if (ec) {
            result.success = false;
            result.error_output = "Failed to finalize output: " + ec.message();
            std::filesystem::remove(temp_output, ec);
        } else {
            result.success = true;
            std::fprintf(stderr, "[vivid] PackageCompiler: compiled %s\n", name.c_str());
        }
    }

    return result;
}

TestCompileResult PackageCompiler::compile_test(const std::string& package_dir,
                                                const std::string& test_rel_path,
                                                const std::vector<std::string>& extra_include_dirs) {
    TestCompileResult result = inspect_cpp_test_source(package_dir, test_rel_path);
    auto stem = result.test_name;
    if (!result.success) {
        result.error_output = result.message;
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
        return result;
    }

    // Build compiler command — executable, not shared library
    std::string cmd = quote(compiler_exe) + " -std=c++17 -O0 -g"
        " -I " + quote(vivid_src_dir_ + "/src") +
        " -I " + quote(package_dir + "/operators");

    for (const auto& dir : extra_include_dirs)
        cmd += " -I " + quote(dir);

    cmd += " -o " + quote(output_path) + " " + quote(source_path) + " 2>&1";

    std::fprintf(stderr, "[vivid] PackageCompiler::compile_test: %s\n", cmd.c_str());

    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.success = false;
        result.error_output = "Failed to execute compiler";
        return result;
    }

    std::array<char, 256> buf;
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        output += buf.data();
    }
    int status = pclose(pipe);

    if (status != 0) {
        result.success = false;
        result.code = "cpp_compile_failed";
        result.message = "Compilation failed";
        result.error_output = truncate_output(output);
        std::error_code ec;
        fs::remove(output_path, ec);
        std::fprintf(stderr, "[vivid] PackageCompiler::compile_test: FAILED %s:\n%s",
                     stem.c_str(), output.c_str());
    } else {
        result.success = true;
        result.code = "cpp_compiled";
        result.message = "compiled";
        std::fprintf(stderr, "[vivid] PackageCompiler::compile_test: compiled %s\n", stem.c_str());
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

    yyjson_doc* doc = yyjson_read(json_str.c_str(), json_str.size(), 0);
    if (!doc) {
        CompileResult err;
        err.success = false;
        err.error_output = "Invalid JSON in manifest: " + manifest_path;
        return {std::move(err)};
    }

    yyjson_val* root = yyjson_doc_get_root(doc);

    std::vector<std::string> operators;
    yyjson_val* ops = yyjson_obj_get(root, "operators");
    if (ops && yyjson_is_arr(ops)) {
        size_t idx, max;
        yyjson_val* val;
        yyjson_arr_foreach(ops, idx, max, val) {
            if (yyjson_is_str(val))
                operators.push_back(yyjson_get_str(val));
        }
    }

    std::vector<std::string> gpu_ops_list;
    yyjson_val* gpu_ops = yyjson_obj_get(root, "gpu_operators");
    if (gpu_ops && yyjson_is_arr(gpu_ops)) {
        size_t idx, max;
        yyjson_val* val;
        yyjson_arr_foreach(gpu_ops, idx, max, val) {
            if (yyjson_is_str(val))
                gpu_ops_list.push_back(yyjson_get_str(val));
        }
    }

    yyjson_doc_free(doc);
    return compile_all(package_dir, operators, gpu_ops_list);
}

} // namespace vivid
