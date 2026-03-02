#pragma once

#include "runtime/package_compiler.h"
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace vivid {

class OperatorRegistry;

struct VendorDependency {
    std::string name;     // e.g. "stb_image"
    std::string include;  // relative path, e.g. "deps/stb"
};

struct PackageDependencies {
    std::vector<std::string> packages;       // vivid package names
    std::vector<VendorDependency> vendor;    // bundled vendor libs
};

struct PackageTests {
    std::vector<std::string> graphs;  // relative paths to test graph files
    std::vector<std::string> cpp;     // relative paths to C++ test sources
};

struct PackageInfo {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::vector<std::string> operators;      // "audio/drum_kick", etc.
    std::vector<std::string> gpu_operators;  // operators needing Dawn
    std::string path;                        // absolute path on disk
    std::string build_type;                  // "" = clang++ (default), "cmake" = cmake build
    bool linked = false;                     // true if symlinked (vivid link)
    PackageDependencies dependencies;
    PackageTests tests;
};

struct InstallResult {
    bool success = false;
    std::string error;
    PackageInfo info;
    std::vector<CompileResult> compile_results;
    std::vector<std::string> installed_deps;  // deps installed during this call
};

class PackageManager {
public:
    using PackageResolver = std::function<std::string(const std::string& package_name)>;

    PackageManager(PackageCompiler& compiler, OperatorRegistry& registry);

    // Install from git URL or local path → clone/copy + compile + scan into registry
    InstallResult install(const std::string& url);

    // Remove package directory and unregister operators
    bool uninstall(const std::string& name);

    // Symlink a local package for development (npm link-style)
    InstallResult link(const std::string& path);

    // Remove a linked package symlink (never touches source)
    bool unlink(const std::string& name);

    // Recompile operators for an installed or linked package
    InstallResult rebuild(const std::string& name);

    // List installed packages
    std::vector<PackageInfo> list();

    // Scan already-installed packages into registry (called at startup)
    void scan_installed();

    // Returns ~/.vivid/packages/ (or platform equivalent)
    static std::string packages_dir();

    // Set a callback that resolves package names to URLs (for dependency resolution)
    void set_resolver(PackageResolver resolver);

    // Check if a package is installed (by name)
    bool is_installed(const std::string& name) const;

private:
    // Parse vivid-package.json into PackageInfo
    static bool parse_manifest(const std::string& package_dir, PackageInfo& info);

    // Internal install with dependency chain tracking for circular detection
    InstallResult install_with_chain(const std::string& url,
                                     std::set<std::string>& installing_chain,
                                     std::vector<std::string>& installed_deps);

    // Compile operators in a package directory (shared by install, link, rebuild)
    bool compile_package(const std::string& pkg_dir, InstallResult& result);

    PackageCompiler& compiler_;
    OperatorRegistry& registry_;
    PackageResolver resolver_;
};

} // namespace vivid
