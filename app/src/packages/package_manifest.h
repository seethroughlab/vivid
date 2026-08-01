#pragma once

#include <string>
#include <vector>

// A Vivid operator package: a directory with a `vivid-package.json` manifest and
// one or more operator source files (authored with VIVID_REGISTER). The package
// compiler builds each listed operator into a loadable .dylib on the user's
// machine — no app rebuild, no CMake reconfigure. (Right-sized vs vivid-classic:
// flat operator list, no git/lockfile/vendor-deps — added later if needed.)
//
// vivid-package.json shape:
//   {
//     "name": "example-visuals",
//     "version": "0.1.0",
//     "operators": [ { "name": "Gradient", "kind": "gpu_visual", "source": "gradient.cpp" } ]
//   }
namespace vivid {

struct PackageOperator {
    std::string name;        // output dylib stem (the registry name comes from the op's descriptor)
    std::string source;      // operator .cpp, relative to the package dir
    std::string kind;        // authoring intent: "gpu_visual" | "audio_effect" | "instrument" |
                             // "frame" (or "" = unspecified). Describes the operator's domain for
                             // discovery/tooling and DEFAULTS the wgpu link (gpu_visual links wgpu,
                             // the others don't). The op's descriptor capability flags remain the
                             // runtime authority; `kind` is manifest metadata, not enforced against them.
    bool        gpu = true;  // link wgpu at build time. Derived from `kind` unless set explicitly.
};

struct PackageManifest {
    bool        ok = false;
    bool        manifest_present = false;   // a vivid-package.json exists here (so !ok is a REAL error,
                                            // not just "this dir isn't a package") — Ph5 P2-02
    std::string error;       // parse error message when !ok
    std::string dir;         // absolute package root
    std::string name;
    std::string version;
    int         abi = 0;     // declared target operator ABI (0 = unspecified). Informational —
                             // the real compatibility check is the dlopen-time ABI guard.
    std::vector<PackageOperator> operators;
};

// Parse <package_dir>/vivid-package.json. Returns ok=false + error on any problem.
PackageManifest parse_package_manifest(const std::string& package_dir);

}  // namespace vivid
