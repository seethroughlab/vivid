// Vivid Module Registry Implementation

#include <vivid/module_registry.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <regex>
#include <iostream>
#include <set>

using json = nlohmann::json;

namespace vivid {

void ModuleRegistry::setRootDir(const fs::path& rootDir) {
    m_rootDir = rootDir;

    // Build search paths
    m_searchPaths.clear();

    // libs/ directory contains all optional libraries
    fs::path libsDir = m_rootDir / "modules";
    if (fs::exists(libsDir)) {
        m_searchPaths.push_back(libsDir);
    }

    // User-installed libraries (~/.vivid/libs/)
    const char* home = getenv("HOME");
    if (home) {
        fs::path userLibs = fs::path(home) / ".vivid" / "modules";
        if (fs::exists(userLibs)) {
            m_searchPaths.push_back(userLibs);
        }
    }
}

std::vector<fs::path> ModuleRegistry::getSearchPaths() const {
    return m_searchPaths;
}

std::vector<std::string> ModuleRegistry::scanIncludes(const fs::path& sourcePath) {
    std::set<std::string> namespaces;

    std::ifstream file(sourcePath);
    if (!file.is_open()) {
        return {};
    }

    // Match #include <vivid/xxx/...> where xxx is the library namespace
    // Examples:
    //   #include <vivid/effects/noise.h>     -> "effects"
    //   #include <vivid/video/player.h>      -> "video"
    //   #include <vivid/render3d/scene.h>    -> "render3d"
    std::regex includeRegex(R"(#\s*include\s*<vivid/(\w+)/)");

    std::string line;
    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, includeRegex)) {
            std::string ns = match[1].str();
            // Skip core namespaces (not libraries)
            // Note: "network" and "gui" are libraries, not core - don't skip them
            if (ns != "vivid" && ns != "context" && ns != "display" &&
                ns != "hot_reload" && ns != "operator" && ns != "chain" &&
                ns != "effects" && ns != "io") {
                namespaces.insert(ns);
            }
        }
    }

    return std::vector<std::string>(namespaces.begin(), namespaces.end());
}

std::string ModuleRegistry::namespaceToModule(const std::string& ns) {
    // Map include namespace to library directory name
    // Most libraries follow the pattern: vivid-<namespace>
    // Special cases can be handled here

    if (ns == "effects") {
        return "vivid-effects-2d";
    }
    if (ns == "render3d") {
        return "vivid-render3d";
    }
    if (ns == "gui") {
        return "vivid-imgui";
    }

    // Default: vivid-<namespace>
    return "vivid-" + ns;
}

std::optional<fs::path> ModuleRegistry::findModule(const std::string& name) {
    for (const auto& searchPath : m_searchPaths) {
        fs::path modulePath = searchPath / name;
        if (fs::exists(modulePath)) {
            return modulePath;
        }
    }
    return std::nullopt;
}

std::optional<ModuleInfo> ModuleRegistry::loadModuleJson(const fs::path& modulePath) {
    fs::path jsonPath = modulePath / "module.json";

    // module.json is optional - we can still use the library without it
    ModuleInfo info;
    info.path = modulePath;
    info.name = modulePath.filename().string();
    info.libraryName = info.name;

    // Default include path
    fs::path incPath = modulePath / "include";
    if (fs::exists(incPath)) {
        info.includePath = incPath;
    } else {
        info.includePath = modulePath;
    }

    if (!fs::exists(jsonPath)) {
        // No module.json - return with defaults
        return info;
    }

    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        return info;
    }

    try {
        json j = json::parse(file);

        std::string name = j.value("name", "");
        if (!name.empty()) {
            info.name = name;
            info.libraryName = name;
        }

        info.version = j.value("version", "");
        info.description = j.value("description", "");

        // Parse dependencies array
        if (j.contains("dependencies") && j["dependencies"].is_array()) {
            for (const auto& dep : j["dependencies"]) {
                if (dep.is_string()) {
                    info.dependencies.push_back(dep.get<std::string>());
                }
            }
        }
    } catch (const json::exception&) {
        // Invalid JSON - return with defaults
    }

    return info;
}

std::optional<ModuleInfo> ModuleRegistry::getModule(const std::string& name) const {
    for (const auto& lib : m_modules) {
        if (lib.name == name) {
            return lib;
        }
    }
    return std::nullopt;
}

std::vector<ModuleInfo> ModuleRegistry::discoverFromChain(const fs::path& chainPath) {
    m_modules.clear();

    // Scan chain.cpp for include directives
    auto namespaces = scanIncludes(chainPath);

    std::cout << "Scanning " << chainPath.filename() << " for library dependencies..." << std::endl;

    // Track which modules we've already added to avoid duplicates
    std::set<std::string> addedModules;

    // Helper to add a module and its dependencies recursively
    std::function<void(const std::string&, int)> addModuleWithDeps = [&](const std::string& moduleName, int depth) {
        // Avoid infinite recursion and duplicates
        if (addedModules.count(moduleName) > 0) {
            return;
        }

        auto libPath = findModule(moduleName);
        if (!libPath) {
            if (depth == 0) {
                // Only warn for direct dependencies, not transitive ones
                std::cerr << "  Warning: Could not find module: " << moduleName << std::endl;
            }
            return;
        }

        auto info = loadModuleJson(*libPath);
        if (!info) {
            return;
        }

        addedModules.insert(moduleName);
        m_modules.push_back(*info);

        std::string indent(depth * 2 + 2, ' ');
        std::cout << indent << "Found module: " << info->name;
        if (!info->version.empty()) {
            std::cout << " v" << info->version;
        }
        if (!info->dependencies.empty()) {
            std::cout << " (deps: ";
            for (size_t i = 0; i < info->dependencies.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << info->dependencies[i];
            }
            std::cout << ")";
        }
        std::cout << std::endl;

        // Recursively add dependencies
        for (const auto& dep : info->dependencies) {
            addModuleWithDeps(dep, depth + 1);
        }
    };

    // Add each discovered namespace's module
    for (const auto& ns : namespaces) {
        std::string libName = namespaceToModule(ns);
        addModuleWithDeps(libName, 0);
    }

    return m_modules;
}

void ModuleRegistry::registerBundledLibrary(const std::string& libraryName, const fs::path& libDir) {
    // Check if already registered
    for (const auto& mod : m_modules) {
        if (mod.libraryName == libraryName) {
            return;
        }
    }

    ModuleInfo info;
    info.name = libraryName;
    info.libraryName = libraryName;
    info.path = libDir;
    info.includePath = libDir.parent_path() / "include";

    m_modules.push_back(info);
}

} // namespace vivid
