// Addon Registry for Vivid
// Manages discovery and loading of addon metadata

#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>

namespace vivid {

namespace fs = std::filesystem;

/// Information about an addon loaded from addon.json
struct AddonInfo {
    std::string name;
    std::string version;
    std::string description;
    std::vector<std::string> platforms;
    std::vector<std::string> detectHeaders;  // Headers that trigger detection
    std::vector<std::string> includeDirs;

    // Per-platform libraries: platform -> list of library names
    std::map<std::string, std::vector<std::string>> libraries;

    /// Check if this addon supports the given platform
    bool supportsPlatform(const std::string& platform) const;

    /// Get libraries for a specific platform
    std::vector<std::string> getLibraries(const std::string& platform) const;
};

/// Registry for managing available addons
class AddonRegistry {
public:
    AddonRegistry() = default;
    ~AddonRegistry() = default;

    /// Load addon metadata from a directory containing addon.json files
    /// Returns number of addons loaded
    int loadFromDirectory(const fs::path& metaDir);

    /// Scan a source file for addon includes
    /// Returns list of addon names that are used
    std::vector<std::string> scanSourceForAddons(const fs::path& sourcePath) const;

    /// Get addon by name (nullptr if not found)
    const AddonInfo* getAddon(const std::string& name) const;

    /// Get all available addons
    std::vector<const AddonInfo*> getAvailableAddons() const;

    /// Check if an addon is available
    bool isAvailable(const std::string& name) const;

    /// Get current platform string ("macos", "windows", "linux")
    static std::string currentPlatform();

private:
    std::map<std::string, AddonInfo> addons_;

    /// Parse a single addon.json file
    bool parseAddonJson(const fs::path& jsonPath);
};

} // namespace vivid
