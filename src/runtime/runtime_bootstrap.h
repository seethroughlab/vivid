#pragma once

#include <filesystem>
#include <string>
#include "runtime/package_manager.h"

namespace vivid {

class OperatorRegistry;
class PackageManager;
class SubgraphModuleRegistry;

struct RuntimeBootstrapPaths {
    std::filesystem::path exe_path;
    std::filesystem::path exe_dir;
    std::filesystem::path resources_dir;
    std::filesystem::path plugins_dir;
    std::string source_dir;
    std::string build_dir;
};

struct RegistryBootstrapOptions {
    bool scan_wgsl_presets = true;
    bool scan_factory_presets = false;
    bool scan_packages = true;
    bool respect_skip_package_scan_env = true;
    SubgraphModuleRegistry* subgraph_modules = nullptr;
};

struct RegistryBootstrapResult {
    DiscoveryReport package_discovery;
    bool package_scan_attempted = false;
    bool package_scan_skipped = false;
};

RuntimeBootstrapPaths resolve_runtime_bootstrap_paths(const std::filesystem::path& argv0,
                                                      const std::string& user_src_dir = "");

RegistryBootstrapResult bootstrap_operator_registry(OperatorRegistry& registry,
                                                    PackageManager* package_manager,
                                                    const RuntimeBootstrapPaths& paths,
                                                    const RegistryBootstrapOptions& options = {});

} // namespace vivid
