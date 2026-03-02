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

class PackageCompiler {
public:
    PackageCompiler(const std::string& vivid_src_dir, const std::string& vivid_build_dir);

    // Compile a single operator from a package.
    // operator_rel_path: relative within package, e.g. "audio/drum_kick"
    // needs_gpu: if true, adds Dawn include paths and framework linkage
    CompileResult compile_operator(const std::string& package_dir,
                                   const std::string& operator_rel_path,
                                   bool needs_gpu);

    // Compile all operators listed in the package manifest.
    // Reads vivid-package.json from package_dir.
    std::vector<CompileResult> compile_all(const std::string& package_dir);

private:
    std::string vivid_src_dir_;
    std::string vivid_build_dir_;
};

} // namespace vivid
