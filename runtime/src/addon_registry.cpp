#include "addon_registry.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <regex>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace vivid {

std::string AddonRegistry::getCurrentPlatform() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

bool AddonInfo::isAvailableOnPlatform() const {
    std::string currentPlatform = AddonRegistry::getCurrentPlatform();
    for (const auto& platform : platforms) {
        if (platform == currentPlatform) {
            return true;
        }
    }
    return false;
}

bool AddonRegistry::parseAddonJson(const std::filesystem::path& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        std::cerr << "[AddonRegistry] Failed to open: " << jsonPath << "\n";
        return false;
    }

    try {
        json j = json::parse(file);

        AddonInfo info;
        info.name = j.value("name", "");
        info.version = j.value("version", "1.0.0");
        info.description = j.value("description", "");

        // Parse platforms
        if (j.contains("platforms") && j["platforms"].is_array()) {
            for (const auto& platform : j["platforms"]) {
                info.platforms.push_back(platform.get<std::string>());
            }
        }

        // Parse detect headers
        if (j.contains("detect_headers") && j["detect_headers"].is_array()) {
            for (const auto& header : j["detect_headers"]) {
                info.detectHeaders.push_back(header.get<std::string>());
            }
        }

        // Parse include dirs
        if (j.contains("include_dirs") && j["include_dirs"].is_array()) {
            for (const auto& dir : j["include_dirs"]) {
                info.includeDirs.push_back(dir.get<std::string>());
            }
        }

        // Parse platform-specific libraries
        std::string currentPlatform = getCurrentPlatform();
        if (j.contains("libraries") && j["libraries"].is_object()) {
            auto& libs = j["libraries"];
            if (libs.contains(currentPlatform) && libs[currentPlatform].is_object()) {
                auto& platformLibs = libs[currentPlatform];

                if (platformLibs.contains("static") && platformLibs["static"].is_array()) {
                    for (const auto& lib : platformLibs["static"]) {
                        info.staticLibs.push_back(lib.get<std::string>());
                    }
                }

                if (platformLibs.contains("system") && platformLibs["system"].is_array()) {
                    for (const auto& lib : platformLibs["system"]) {
                        info.systemLibs.push_back(lib.get<std::string>());
                    }
                }

                if (platformLibs.contains("runtime") && platformLibs["runtime"].is_array()) {
                    for (const auto& dll : platformLibs["runtime"]) {
                        info.runtimeDlls.push_back(dll.get<std::string>());
                    }
                }

                if (platformLibs.contains("frameworks") && platformLibs["frameworks"].is_array()) {
                    for (const auto& fw : platformLibs["frameworks"]) {
                        info.frameworks.push_back(fw.get<std::string>());
                    }
                }
            }
        }

        if (info.name.empty()) {
            std::cerr << "[AddonRegistry] Addon has no name: " << jsonPath << "\n";
            return false;
        }

        // Register header -> addon mapping
        for (const auto& header : info.detectHeaders) {
            headerToAddon_[header] = info.name;
        }

        addons_[info.name] = std::move(info);
        return true;

    } catch (const json::exception& e) {
        std::cerr << "[AddonRegistry] JSON parse error in " << jsonPath << ": " << e.what() << "\n";
        return false;
    }
}

int AddonRegistry::loadFromDirectory(const std::filesystem::path& metaDir) {
    // Store base path (parent of meta dir, i.e., build/addons)
    addonsBasePath_ = metaDir.parent_path();

    if (!fs::exists(metaDir)) {
        std::cerr << "[AddonRegistry] Metadata directory not found: " << metaDir << "\n";
        return 0;
    }

    int count = 0;
    for (const auto& entry : fs::directory_iterator(metaDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            if (parseAddonJson(entry.path())) {
                count++;
            }
        }
    }

    std::cout << "[AddonRegistry] Loaded " << count << " addon(s) from " << metaDir << "\n";
    return count;
}

std::vector<std::string> AddonRegistry::scanSourceForAddons(const std::filesystem::path& projectPath) const {
    std::vector<std::string> required;

    if (!fs::exists(projectPath)) {
        return required;
    }

    // Regex to match #include <vivid/...> directives
    std::regex includeRegex(R"(#include\s*<(vivid/[^>]+)>)");

    // Scan all source files in the project
    for (const auto& entry : fs::directory_iterator(projectPath)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".cc" && ext != ".cxx" && ext != ".h" && ext != ".hpp") continue;

        std::ifstream file(entry.path());
        if (!file.is_open()) continue;

        std::string line;
        while (std::getline(file, line)) {
            // Skip lines without #include
            if (line.find("#include") == std::string::npos) continue;

            std::smatch match;
            if (std::regex_search(line, match, includeRegex)) {
                std::string header = match[1].str();

                // Check if this header belongs to an addon
                auto it = headerToAddon_.find(header);
                if (it != headerToAddon_.end()) {
                    const std::string& addonName = it->second;

                    // Check platform availability
                    const AddonInfo* addon = getAddon(addonName);
                    if (addon && addon->isAvailableOnPlatform()) {
                        // Avoid duplicates
                        if (std::find(required.begin(), required.end(), addonName) == required.end()) {
                            required.push_back(addonName);
                            std::cout << "[AddonRegistry] Detected addon: " << addonName << "\n";
                        }
                    } else if (addon) {
                        std::cerr << "[AddonRegistry] Warning: addon '" << addonName
                                  << "' is not available on this platform ("
                                  << getCurrentPlatform() << ")\n";
                    }
                }
            }
        }
    }

    return required;
}

const AddonInfo* AddonRegistry::getAddon(const std::string& name) const {
    auto it = addons_.find(name);
    if (it != addons_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<const AddonInfo*> AddonRegistry::getAvailableAddons() const {
    std::vector<const AddonInfo*> available;
    for (const auto& [name, info] : addons_) {
        if (info.isAvailableOnPlatform()) {
            available.push_back(&info);
        }
    }
    return available;
}

bool AddonRegistry::isAvailable(const std::string& name) const {
    const AddonInfo* addon = getAddon(name);
    return addon && addon->isAvailableOnPlatform();
}

} // namespace vivid
