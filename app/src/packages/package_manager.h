#pragma once

#include "packages/package_manifest.h"
#include "packages/package_compiler.h"
#include <string>
#include <vector>

// Install + discover operator packages. "Install" = compile every operator in a
// package's manifest into the managed operators directory (which the app scans at
// startup, so installed ops persist across launches). Right-sized vs vivid-classic:
// no git fetch / lockfile / scopes beyond the managed user dir (add later if needed).
namespace vivid {

// The managed directory compiled package operators are installed into and the app
// scans at startup. ~/Library/Application Support/Vivid/operators on macOS. Created
// on first call; overridable with $VIVID_OPERATORS_DIR.
std::string user_operators_dir();

struct PackageInstallResult {
    bool        ok = false;        // the manifest parsed + the install ran (per-op results below)
    std::string error;             // manifest/parse error when !ok
    std::string name;              // package name
    std::vector<PackageCompileResult> compiles;   // one per operator (check .success each)
};

// Parse <package_dir>/vivid-package.json and compile each operator into `out_dir`
// (default: the managed operators dir). Does NOT load them — the caller registers the
// produced dylibs (via gpu/operator_scan load_and_register_operator) or relies on the
// next startup scan. Pass the project folder as out_dir for a project-local package.
PackageInstallResult install_package(const std::string& package_dir,
                                     const std::string& out_dir = std::string());

// List the package manifests under a directory of package subdirectories.
std::vector<PackageManifest> discover_packages(const std::string& scope_dir);

}  // namespace vivid
