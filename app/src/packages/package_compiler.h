#pragma once

#include "packages/package_manifest.h"
#include <string>

// Compiles a package operator source into a loadable .dylib by invoking the host
// C++ compiler (clang++) directly — no CMake reconfigure, no app rebuild. The
// source registers itself via VIVID_REGISTER (no codegen step; see P2.2 audit).
// Toolchain + header/lib paths come from compile-time defines set by CMake
// (VIVID_PKG_CXX / VIVID_PKG_SRC_DIR / VIVID_PKG_WEBGPU_INCLUDE_DIR / _LIB_DIR).
namespace vivid {

struct PackageCompileResult {
    bool        success = false;
    std::string dylib_path;     // on success
    std::string op_name;
    std::string error_output;   // compiler stderr on failure (or a reason string)
};

class PackageCompiler {
public:
    // Compile <package_dir>/<op.source> → <out_dir>/<op.name>.dylib.
    PackageCompileResult compile_operator(const std::string& package_dir,
                                          const PackageOperator& op,
                                          const std::string& out_dir);
};

}  // namespace vivid
