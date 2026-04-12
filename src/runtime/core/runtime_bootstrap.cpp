#include "runtime/core/runtime_bootstrap.h"

#include "runtime/operators/builtin_operators.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/packages/package_manager.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace vivid {
namespace fs = std::filesystem;

RuntimeBootstrapPaths resolve_runtime_bootstrap_paths(const fs::path& argv0,
                                                      const std::string& user_src_dir) {
    RuntimeBootstrapPaths out;

    std::error_code ec;
    out.exe_path = fs::canonical(argv0, ec);
    if (ec)
        out.exe_path = fs::absolute(argv0);
    out.exe_dir = out.exe_path.parent_path();

#ifdef __APPLE__
    bool bundled_executable =
        out.exe_dir.filename() == "MacOS" &&
        out.exe_dir.parent_path().filename() == "Contents";
    if (bundled_executable) {
        out.resources_dir = out.exe_dir.parent_path() / "Resources";
        out.plugins_dir = out.exe_dir.parent_path() / "PlugIns";
    } else {
        out.resources_dir = out.exe_dir;
        out.plugins_dir = out.exe_dir;
    }
#else
    out.resources_dir = out.exe_dir;
    out.plugins_dir = out.exe_dir;
#endif

#ifdef VIVID_BUILD_DIR
    out.build_dir = VIVID_BUILD_DIR;
    if (!fs::is_directory(out.build_dir))
#endif
    {
#ifdef __APPLE__
        out.build_dir = out.exe_dir.parent_path().parent_path().parent_path().string();
#else
        out.build_dir = out.exe_dir.string();
#endif
    }

    auto c = fs::path(out.build_dir);
    for (int i = 0; i < 3; ++i) {
        if (fs::exists(c / "CMakeLists.txt") && fs::exists(c / "src" / "runtime")) {
            out.source_dir = c.string();
            break;
        }
        c = c.parent_path();
    }

#ifdef __APPLE__
    if (out.source_dir.empty()) {
        auto bundled_source_dir = out.resources_dir / "source";
        if (fs::is_directory(bundled_source_dir / "src" / "runtime"))
            out.source_dir = bundled_source_dir.string();
    }
    if (out.source_dir.empty()) {
        auto sdk_dir = out.resources_dir / "sdk";
        if (fs::is_directory(sdk_dir / "src" / "operator_api"))
            out.source_dir = sdk_dir.string();
    }
#endif

    if (out.source_dir.empty())
        out.source_dir = user_src_dir;

    return out;
}

RegistryBootstrapResult bootstrap_operator_registry(OperatorRegistry& registry,
                                                    PackageManager* package_manager,
                                                    const RuntimeBootstrapPaths& paths,
                                                    const RegistryBootstrapOptions& options) {
    RegistryBootstrapResult result;

#ifdef __APPLE__
    registry.scan_deferred(paths.plugins_dir.string().c_str());
#else
    registry.scan_deferred(paths.exe_dir.string().c_str());
#endif
    register_builtin_operators(registry);

    if (options.scan_shader_operators)
        registry.scan_shader_operators((paths.resources_dir / "filters").string());

    if (options.scan_factory_presets)
        registry.scan_factory_presets((paths.resources_dir / "factory_presets").string());

    if (!package_manager || !options.scan_packages)
        return result;

    result.package_scan_attempted = true;
    if (options.subgraph_modules)
        package_manager->set_subgraph_module_registry(options.subgraph_modules);

    if (options.respect_skip_package_scan_env && std::getenv("VIVID_SKIP_PACKAGE_SCAN")) {
        std::fprintf(stderr, "[vivid] Skipping installed package scan (VIVID_SKIP_PACKAGE_SCAN)\n");
        result.package_scan_skipped = true;
        return result;
    }

    package_manager->scan_installed();
    result.package_discovery = package_manager->last_discovery_report();
    return result;
}

} // namespace vivid
