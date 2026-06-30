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
//     "operators": [ { "name": "Gradient", "source": "gradient.cpp", "gpu": true } ]
//   }
namespace vivid {

struct PackageOperator {
    std::string name;        // output dylib stem (the registry name comes from the op's descriptor)
    std::string source;      // operator .cpp, relative to the package dir
    bool        gpu = true;  // link wgpu (GPU operators); false = frame/audio-only
};

struct PackageManifest {
    bool        ok = false;
    std::string error;       // parse error message when !ok
    std::string dir;         // absolute package root
    std::string name;
    std::string version;
    std::vector<PackageOperator> operators;
};

// Parse <package_dir>/vivid-package.json. Returns ok=false + error on any problem.
PackageManifest parse_package_manifest(const std::string& package_dir);

}  // namespace vivid
