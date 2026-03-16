#pragma once

#include "runtime/package_compiler.h"
#include "runtime/tool_discovery.h"
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
    std::string vivid_core;                 // optional SemVer range for core compatibility
    std::string description;
    std::string author;
    std::string category;
    std::vector<std::string> tags;
    std::vector<std::string> operators;      // "audio/drum_kick", etc.
    std::vector<std::string> gpu_operators;  // operators needing Dawn
    std::string path;                        // absolute path on disk
    std::string source_scope;                // local|workspace|user|extra
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

enum class PackageUpdateClass {
    UpToDate,
    CompatibleUpdate,
    IncompatibleUpdate,
    RemoteOlderOrEqual,
    InvalidVersionData
};

struct PackageUpdateAssessment {
    std::string package_name;
    std::string installed_version;
    std::string remote_version;
    std::string remote_vivid_core;
    bool update_available = false;
    bool compatible = true;
    bool constraint_valid = true;
    PackageUpdateClass classification = PackageUpdateClass::UpToDate;
    std::string message;
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

    // Returns <config_dir>/packages (platform-specific config dir)
    static std::string packages_dir();

    // Set a callback that resolves package names to URLs (for dependency resolution)
    void set_resolver(PackageResolver resolver);

    // Check if a package is installed (by name)
    bool is_installed(const std::string& name) const;

    // Return the resolved active package path for a given package name.
    // Empty string if no package resolves for that name.
    std::string resolve_package_path(const std::string& name) const;

    // Compare installed vs remote metadata and classify update compatibility.
    static PackageUpdateAssessment assess_update(const PackageInfo& installed,
                                                 const std::string& remote_version,
                                                 const std::string& remote_vivid_core,
                                                 const std::string& core_version);

    // Normalize a GitHub URL for git clone.
    // Expands shorthand (user/repo), adds protocol, strips browser paths, ensures .git suffix.
    static std::string normalize_github_url(const std::string& url);

    // Classify the delta between a saved package version and the installed version.
    // Returns UpToDate, CompatibleUpdate (same major), IncompatibleUpdate (major changed),
    // RemoteOlderOrEqual (installed older than saved), or InvalidVersionData on parse failure.
    static PackageUpdateClass classify_version_delta(const std::string& saved_version,
                                                     const std::string& installed_version);

private:
    // Discover package candidates across all scopes and resolve winners by precedence.
    static std::vector<PackageInfo> resolve_packages(bool emit_warnings);

    // Parse vivid-package.json into PackageInfo.
    // Returns empty string on success, or a human-readable error message.
    static std::string parse_manifest(const std::string& package_dir, PackageInfo& info);

    // Internal install with dependency chain tracking for circular detection
    InstallResult install_with_chain(const std::string& url,
                                     std::set<std::string>& installing_chain,
                                     std::vector<std::string>& installed_deps);

    // Compile operators in a package directory (shared by install, link, rebuild)
    bool compile_package(const std::string& pkg_dir, InstallResult& result,
                         bool register_outputs = true);

    PackageCompiler& compiler_;
    OperatorRegistry& registry_;
    PackageResolver resolver_;
};

} // namespace vivid
