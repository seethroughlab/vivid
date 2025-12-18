// Vivid Addon Registry Implementation

#include <vivid/addon_registry.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <regex>
#include <iostream>
#include <set>

using json = nlohmann::json;

namespace vivid {

void AddonRegistry::setRootDir(const fs::path& rootDir) {
    m_rootDir = rootDir;

    // Build search paths
    m_searchPaths.clear();

    // Development mode: addons/ in source tree
    fs::path addonsDir = m_rootDir / "addons";
    if (fs::exists(addonsDir)) {
        m_searchPaths.push_back(addonsDir);
    }

    // User-installed addons (future)
    // fs::path userAddons = fs::path(getenv("HOME")) / ".vivid" / "addons";
    // if (fs::exists(userAddons)) {
    //     m_searchPaths.push_back(userAddons);
    // }
}

std::vector<fs::path> AddonRegistry::getSearchPaths() const {
    return m_searchPaths;
}

std::vector<std::string> AddonRegistry::scanIncludes(const fs::path& sourcePath) {
    std::set<std::string> namespaces;

    std::ifstream file(sourcePath);
    if (!file.is_open()) {
        return {};
    }

    // Match #include <vivid/xxx/...> where xxx is the addon namespace
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
            // Skip core namespaces (not addons)
            if (ns != "vivid" && ns != "context" && ns != "display" &&
                ns != "hot_reload" && ns != "operator" && ns != "chain") {
                namespaces.insert(ns);
            }
        }
    }

    return std::vector<std::string>(namespaces.begin(), namespaces.end());
}

std::string AddonRegistry::namespaceToAddon(const std::string& ns) {
    // Map include namespace to addon directory name
    // Most addons follow the pattern: vivid-<namespace>
    // Special cases can be handled here

    if (ns == "effects") {
        return "vivid-effects-2d";
    }
    if (ns == "render3d") {
        return "vivid-render3d";
    }

    // Default: vivid-<namespace>
    return "vivid-" + ns;
}

std::optional<fs::path> AddonRegistry::findAddon(const std::string& name) {
    for (const auto& searchPath : m_searchPaths) {
        fs::path addonPath = searchPath / name;
        if (fs::exists(addonPath)) {
            return addonPath;
        }
    }
    return std::nullopt;
}

std::optional<AddonInfo> AddonRegistry::loadAddonJson(const fs::path& addonPath) {
    fs::path jsonPath = addonPath / "addon.json";

    // addon.json is optional - we can still use the addon without it
    AddonInfo info;
    info.path = addonPath;
    info.name = addonPath.filename().string();
    info.libraryName = info.name;

    // Default include path
    fs::path incPath = addonPath / "include";
    if (fs::exists(incPath)) {
        info.includePath = incPath;
    } else {
        info.includePath = addonPath;
    }

    if (!fs::exists(jsonPath)) {
        // No addon.json - return with defaults
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
            info.libraryName = name;  // Library name matches addon name
        }

        info.version = j.value("version", "");
        info.description = j.value("description", "");

        if (j.contains("operators") && j["operators"].is_array()) {
            for (const auto& op : j["operators"]) {
                info.operators.push_back(op.get<std::string>());
            }
        }
    } catch (const json::exception&) {
        // Invalid JSON - return with defaults
    }

    return info;
}

std::optional<AddonInfo> AddonRegistry::getAddon(const std::string& name) const {
    for (const auto& addon : m_addons) {
        if (addon.name == name) {
            return addon;
        }
    }
    return std::nullopt;
}

std::vector<AddonInfo> AddonRegistry::discoverFromChain(const fs::path& chainPath) {
    m_addons.clear();

    // Scan chain.cpp for include directives
    auto namespaces = scanIncludes(chainPath);

    std::cout << "Scanning " << chainPath.filename() << " for addon dependencies..." << std::endl;

    for (const auto& ns : namespaces) {
        std::string addonName = namespaceToAddon(ns);
        auto addonPath = findAddon(addonName);

        if (addonPath) {
            auto info = loadAddonJson(*addonPath);
            if (info) {
                m_addons.push_back(*info);
                std::cout << "  Found addon: " << info->name;
                if (!info->version.empty()) {
                    std::cout << " v" << info->version;
                }
                std::cout << std::endl;
            }
        } else {
            std::cerr << "  Warning: Could not find addon for namespace '" << ns
                      << "' (looked for " << addonName << ")" << std::endl;
        }
    }

    return m_addons;
}

} // namespace vivid
