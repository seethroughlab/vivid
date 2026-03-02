#pragma once

#include "runtime/package_compiler.h"
#include <string>
#include <vector>

namespace vivid {

class OperatorRegistry;

struct PackageInfo {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> operators;      // "audio/drum_kick", etc.
    std::vector<std::string> gpu_operators;  // operators needing Dawn
    std::string path;                        // absolute path on disk
};

struct InstallResult {
    bool success = false;
    std::string error;
    PackageInfo info;
    std::vector<CompileResult> compile_results;
};

class PackageManager {
public:
    PackageManager(PackageCompiler& compiler, OperatorRegistry& registry);

    // Install from git URL or local path → clone/copy + compile + scan into registry
    InstallResult install(const std::string& url);

    // Remove package directory and unregister operators
    bool uninstall(const std::string& name);

    // List installed packages
    std::vector<PackageInfo> list();

    // Scan already-installed packages into registry (called at startup)
    void scan_installed();

    // Returns ~/.vivid/packages/ (or platform equivalent)
    static std::string packages_dir();

private:
    // Parse vivid-package.json into PackageInfo
    static bool parse_manifest(const std::string& package_dir, PackageInfo& info);

    PackageCompiler& compiler_;
    OperatorRegistry& registry_;
};

} // namespace vivid
