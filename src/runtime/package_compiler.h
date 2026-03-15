#pragma once

#include <string>
#include <vector>

namespace vivid {

struct CompileResult {
    bool success = false;
    std::string dylib_path;    // on success: path to compiled .dylib/.so/.dll
    std::string error_output;  // on failure: compiler stderr
    std::string operator_name; // e.g. "drum_kick"
};

struct TestCompileResult {
    bool success = false;
    std::string executable_path;
    std::string error_output;
    std::string test_name;
    std::string normalized_rel_path;
    std::string code;
    std::string message;
};

class PackageCompiler {
public:
    PackageCompiler(const std::string& vivid_src_dir, const std::string& vivid_build_dir);

    // Compile a single operator from a package.
    // operator_rel_path: relative within package, e.g. "audio/drum_kick"
    // needs_gpu: if true, adds Dawn include paths and framework linkage
    // extra_include_dirs: additional -I paths (e.g. resolved vendor deps)
    CompileResult compile_operator(const std::string& package_dir,
                                   const std::string& operator_rel_path,
                                   bool needs_gpu,
                                   const std::vector<std::string>& extra_include_dirs = {});

    // Compile all operators listed in the package manifest.
    // Reads vivid-package.json from package_dir.
    std::vector<CompileResult> compile_all(const std::string& package_dir);

    // Compile from already-parsed operator lists (avoids re-reading the manifest).
    // vendor_include_dirs: resolved absolute paths for vendored dependency headers.
    std::vector<CompileResult> compile_all(const std::string& package_dir,
                                           const std::vector<std::string>& operators,
                                           const std::vector<std::string>& gpu_operators,
                                           const std::vector<std::string>& vendor_include_dirs = {});

    // Compile a C++ test source into an executable.
    // test_rel_path: relative within package, e.g. "tests/test_ops.cpp"
    TestCompileResult compile_test(const std::string& package_dir,
                                   const std::string& test_rel_path,
                                   const std::vector<std::string>& extra_include_dirs = {});

    // Accessors for cmake-based packages
    const std::string& src_dir() const { return vivid_src_dir_; }
    const std::string& build_dir() const { return vivid_build_dir_; }

private:
    std::string vivid_src_dir_;
    std::string vivid_build_dir_;
};

} // namespace vivid
