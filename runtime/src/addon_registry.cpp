// Addon Registry Implementation

#include "addon_registry.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>

namespace vivid {

// Simple JSON value extraction (no external dependency)
namespace {

std::string extractString(const std::string& json, const std::string& key) {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

std::vector<std::string> extractStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;

    // Find the array
    std::regex arrayRe("\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]");
    std::smatch arrayMatch;
    if (std::regex_search(json, arrayMatch, arrayRe) && arrayMatch.size() > 1) {
        std::string arrayContent = arrayMatch[1].str();

        // Extract strings from array
        std::regex stringRe("\"([^\"]*)\"");
        auto begin = std::sregex_iterator(arrayContent.begin(), arrayContent.end(), stringRe);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            result.push_back((*it)[1].str());
        }
    }

    return result;
}

std::map<std::string, std::vector<std::string>> extractLibraries(const std::string& json) {
    std::map<std::string, std::vector<std::string>> result;

    // Find libraries object
    std::regex libsRe("\"libraries\"\\s*:\\s*\\{([^}]*)\\}");
    std::smatch libsMatch;

    std::string searchStr = json;
    while (std::regex_search(searchStr, libsMatch, libsRe)) {
        std::string libsContent = libsMatch[1].str();

        // Find each platform
        std::regex platformRe("\"(\\w+)\"\\s*:\\s*\\{([^}]*)\\}");
        auto begin = std::sregex_iterator(libsContent.begin(), libsContent.end(), platformRe);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::string platform = (*it)[1].str();
            std::string platformContent = (*it)[2].str();

            // Extract static libraries
            std::vector<std::string> libs = extractStringArray("{\"static\":" + platformContent + "}", "static");
            if (!libs.empty()) {
                result[platform] = libs;
            }
        }

        searchStr = libsMatch.suffix();
    }

    return result;
}

} // anonymous namespace

bool AddonInfo::supportsPlatform(const std::string& platform) const {
    for (const auto& p : platforms) {
        if (p == platform) return true;
    }
    return false;
}

std::vector<std::string> AddonInfo::getLibraries(const std::string& platform) const {
    auto it = libraries.find(platform);
    if (it != libraries.end()) {
        return it->second;
    }
    return {};
}

std::string AddonRegistry::currentPlatform() {
#if defined(__APPLE__)
    return "macos";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

int AddonRegistry::loadFromDirectory(const fs::path& metaDir) {
    int count = 0;

    if (!fs::exists(metaDir)) {
        return 0;
    }

    for (const auto& entry : fs::directory_iterator(metaDir)) {
        if (entry.path().extension() == ".json") {
            if (parseAddonJson(entry.path())) {
                count++;
            }
        }
    }

    return count;
}

bool AddonRegistry::parseAddonJson(const fs::path& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        std::cerr << "[AddonRegistry] Failed to open: " << jsonPath << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    AddonInfo info;
    info.name = extractString(json, "name");
    if (info.name.empty()) {
        std::cerr << "[AddonRegistry] Missing name in: " << jsonPath << std::endl;
        return false;
    }

    info.version = extractString(json, "version");
    info.description = extractString(json, "description");
    info.platforms = extractStringArray(json, "platforms");
    info.detectHeaders = extractStringArray(json, "detect_headers");
    info.includeDirs = extractStringArray(json, "include_dirs");
    info.libraries = extractLibraries(json);

    std::cout << "[AddonRegistry] Loaded addon: " << info.name
              << " v" << info.version << std::endl;

    addons_[info.name] = std::move(info);
    return true;
}

std::vector<std::string> AddonRegistry::scanSourceForAddons(const fs::path& sourcePath) const {
    std::vector<std::string> result;

    std::ifstream file(sourcePath);
    if (!file.is_open()) {
        return result;
    }

    std::string line;
    std::regex includeRe("#include\\s*[<\"]([^>\"]+)[>\"]");

    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, includeRe) && match.size() > 1) {
            std::string header = match[1].str();

            // Check if this header matches any addon
            for (const auto& [name, info] : addons_) {
                for (const auto& detectHeader : info.detectHeaders) {
                    if (header == detectHeader) {
                        // Check if not already added
                        bool found = false;
                        for (const auto& r : result) {
                            if (r == name) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            result.push_back(name);
                        }
                    }
                }
            }
        }
    }

    return result;
}

const AddonInfo* AddonRegistry::getAddon(const std::string& name) const {
    auto it = addons_.find(name);
    if (it != addons_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<const AddonInfo*> AddonRegistry::getAvailableAddons() const {
    std::vector<const AddonInfo*> result;
    for (const auto& [name, info] : addons_) {
        result.push_back(&info);
    }
    return result;
}

bool AddonRegistry::isAvailable(const std::string& name) const {
    return addons_.find(name) != addons_.end();
}

} // namespace vivid
