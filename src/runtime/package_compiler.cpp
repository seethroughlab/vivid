#include "runtime/package_compiler.h"
#include "runtime/platform.h"
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "yyjson.h"

namespace vivid {

// Shell-quote a path to handle spaces and special characters
static std::string quote(const std::string& s) {
    return "'" + s + "'";
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
    result.dylib_path = output_path;

    // Build compiler command
    // -I <vivid_src>/src  — for operator_api/ headers
    // -I <package>/operators/<domain>  — for package-local shared headers
    std::string domain_include = package_dir + "/operators";
    if (!domain.empty())
        domain_include = package_dir + "/operators/" + domain;

    std::string cmd = "clang++ -std=c++17 -shared -fPIC -O2"
        " -I " + quote(vivid_src_dir_ + "/src") +
        " -I " + quote(domain_include);

    // Vendor / extra include directories (e.g. bundled third-party headers)
    for (const auto& dir : extra_include_dirs)
        cmd += " -I " + quote(dir);

    // GPU operators need Dawn/WebGPU includes and library
    if (needs_gpu) {
        // Find the wgpu directory in the build tree
        std::string wgpu_include;
        std::string wgpu_lib_dir;
        for (auto& entry : std::filesystem::directory_iterator(
                vivid_build_dir_ + "/_deps")) {
            std::string entry_name = entry.path().filename().string();
            if (entry_name.find("wgpu") != std::string::npos &&
                entry_name.find("-src") != std::string::npos) {
                std::string candidate = entry.path().string() + "/include";
                if (std::filesystem::exists(candidate + "/webgpu/webgpu.h")) {
                    wgpu_include = candidate;
                    // Library is in the sibling lib/ directory
                    std::string lib_candidate = entry.path().string() + "/lib";
                    if (std::filesystem::exists(lib_candidate))
                        wgpu_lib_dir = lib_candidate;
                    break;
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

    cmd += " -o " + quote(output_path) + " " + quote(source_path) + " 2>&1";

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
    while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
        output += buf.data();
    }
    int status = pclose(pipe);

    if (status != 0) {
        result.success = false;
        result.error_output = output;
        std::fprintf(stderr, "[vivid] PackageCompiler: FAILED %s:\n%s",
                     name.c_str(), output.c_str());
    } else {
        result.success = true;
        std::fprintf(stderr, "[vivid] PackageCompiler: compiled %s\n", name.c_str());
    }

    return result;
}

TestCompileResult PackageCompiler::compile_test(const std::string& package_dir,
                                                const std::string& test_rel_path,
                                                const std::vector<std::string>& extra_include_dirs) {
    TestCompileResult result;

    std::string source_path = package_dir + "/" + test_rel_path;

    // Derive test name from filename stem
    auto stem = std::filesystem::path(test_rel_path).stem().string();
    result.test_name = stem;

    if (!std::filesystem::exists(source_path)) {
        result.success = false;
        result.error_output = "Test source not found: " + source_path;
        return result;
    }

    // Ensure build output directory exists
    std::string build_dir = package_dir + "/build";
    std::filesystem::create_directories(build_dir);

    std::string output_path = build_dir + "/" + stem;
    result.executable_path = output_path;

    // Build compiler command — executable, not shared library
    std::string cmd = "clang++ -std=c++17 -O0 -g"
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
        result.error_output = output;
        std::fprintf(stderr, "[vivid] PackageCompiler::compile_test: FAILED %s:\n%s",
                     stem.c_str(), output.c_str());
    } else {
        result.success = true;
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
