#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace vivid {

/**
 * @brief Information about an available addon.
 *
 * Loaded from addon.json files in the addons directory.
 */
struct AddonInfo {
    std::string name;                          // e.g., "spout"
    std::string version;                       // e.g., "1.0.0"
    std::string description;                   // Human-readable description
    std::vector<std::string> platforms;        // e.g., ["windows"]
    std::vector<std::string> detectHeaders;    // Headers that trigger auto-detection
    std::vector<std::string> includeDirs;      // Relative include directories
    std::vector<std::string> staticLibs;       // Static libraries to link
    std::vector<std::string> systemLibs;       // System libraries to link
    std::vector<std::string> runtimeDlls;      // DLLs needed at runtime
    std::vector<std::string> frameworks;       // macOS frameworks

    // Check if addon is available on current platform
    bool isAvailableOnPlatform() const;
};

/**
 * @brief Registry of available Vivid addons.
 *
 * The AddonRegistry loads addon metadata from addon.json files and provides:
 * - Auto-detection of required addons by scanning source files
 * - Information about available libraries and include paths
 * - Platform-specific addon filtering
 *
 * Usage:
 * @code
 * AddonRegistry registry;
 * registry.loadFromDirectory("build/addons/meta");
 *
 * // Detect which addons a project needs
 * auto required = registry.scanSourceForAddons("examples/spout-out");
 *
 * // Get addon info for CMake generation
 * for (const auto& name : required) {
 *     const AddonInfo* addon = registry.getAddon(name);
 *     // Use addon->staticLibs, addon->includeDirs, etc.
 * }
 * @endcode
 */
class AddonRegistry {
public:
    AddonRegistry() = default;

    /**
     * @brief Load addon metadata from a directory containing addon.json files.
     * @param metaDir Directory containing addon metadata files (e.g., build/addons/meta)
     * @return Number of addons loaded
     */
    int loadFromDirectory(const std::filesystem::path& metaDir);

    /**
     * @brief Scan source files for addon includes and return required addon names.
     * @param projectPath Path to the project directory
     * @return List of addon names that the project requires
     */
    std::vector<std::string> scanSourceForAddons(const std::filesystem::path& projectPath) const;

    /**
     * @brief Get addon info by name.
     * @param name Addon name (e.g., "spout")
     * @return Pointer to addon info, or nullptr if not found
     */
    const AddonInfo* getAddon(const std::string& name) const;

    /**
     * @brief Get all addons available on current platform.
     */
    std::vector<const AddonInfo*> getAvailableAddons() const;

    /**
     * @brief Check if an addon is available (exists and supports current platform).
     */
    bool isAvailable(const std::string& name) const;

    /**
     * @brief Get the base path for addon files (build/addons).
     */
    const std::filesystem::path& getAddonsBasePath() const { return addonsBasePath_; }

    /**
     * @brief Set the base path for addon files.
     */
    void setAddonsBasePath(const std::filesystem::path& path) { addonsBasePath_ = path; }

    // Get current platform string
    static std::string getCurrentPlatform();

private:
    std::unordered_map<std::string, AddonInfo> addons_;
    std::unordered_map<std::string, std::string> headerToAddon_;  // Maps header path to addon name
    std::filesystem::path addonsBasePath_;

    bool parseAddonJson(const std::filesystem::path& jsonPath);
};

} // namespace vivid
