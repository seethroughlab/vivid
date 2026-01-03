#pragma once

/**
 * @file addon_manager.h
 * @brief User addon management (install, remove, update, load)
 *
 * Manages third-party addons installed via `vivid addons install <git-url>`.
 * Addons are installed to ~/.vivid/addons/ and loaded at runtime startup.
 */

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace vivid {

/**
 * @brief Metadata parsed from addon.json
 */
struct AddonJson {
    std::string name;
    std::string version;
    std::string description;
    std::string repository;
    std::string license;
    std::vector<std::string> dependencies;
    std::vector<std::string> operators;

    // Prebuilt binary URLs by platform (darwin-arm64, darwin-x64, linux-x64, win32-x64)
    struct PrebuiltUrls {
        std::string darwinArm64;
        std::string darwinX64;
        std::string linuxX64;
        std::string win32X64;
    } prebuilt;
};

/**
 * @brief Info about an installed addon (from manifest.json)
 */
struct InstalledAddon {
    std::string name;
    std::string version;
    std::string gitUrl;
    std::string gitRef;
    std::string installedAt;  // ISO 8601 timestamp
    std::string builtFrom;    // "prebuilt" or "source"
    fs::path installPath;     // ~/.vivid/addons/<name>
};

/**
 * @brief Manages user-installed addons
 *
 * Directory structure:
 *   ~/.vivid/addons/
 *     manifest.json           - List of installed addons
 *     <addon-name>/
 *       addon.json            - Addon metadata
 *       lib/                  - Libraries (dylib/so/dll)
 *       include/              - Headers
 *       src/                  - Source (git repo for rebuilds)
 *       build/                - CMake build directory
 */
class AddonManager {
public:
    /// @brief Get singleton instance
    static AddonManager& instance();

    // -------------------------------------------------------------------------
    // CLI Commands
    // -------------------------------------------------------------------------

    /**
     * @brief Install an addon from a git URL
     * @param gitUrl Git repository URL (https://github.com/...)
     * @param ref Optional git ref (tag, branch, commit)
     * @return true on success
     */
    bool install(const std::string& gitUrl, const std::string& ref = "");

    /**
     * @brief Remove an installed addon
     * @param name Addon name
     * @return true on success
     */
    bool remove(const std::string& name);

    /**
     * @brief Update an addon (or all addons if name is empty)
     * @param name Addon name (empty = update all)
     * @return true on success
     */
    bool update(const std::string& name = "");

    /**
     * @brief Get list of installed addons
     */
    std::vector<InstalledAddon> listInstalled() const;

    /**
     * @brief Output installed addons as JSON to stdout
     */
    void outputJson() const;

    // -------------------------------------------------------------------------
    // Runtime Loading
    // -------------------------------------------------------------------------

    /**
     * @brief Load all user-installed addons at runtime startup
     *
     * Called from main() before the main loop. Scans ~/.vivid/addons/
     * and dlopen's each addon library with RTLD_GLOBAL so static
     * initializers can register operators.
     */
    void loadUserAddons();

    /**
     * @brief Get include paths for all installed addons
     * Used by hot_reload.cpp for chain compilation
     */
    std::vector<fs::path> getIncludePaths() const;

    /**
     * @brief Get library paths for all installed addons
     * Used by hot_reload.cpp for chain linking
     */
    std::vector<fs::path> getLibraryPaths() const;

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

    /// @brief Get the addons directory (~/.vivid/addons)
    fs::path addonsDir() const { return m_addonsDir; }

    /// @brief Get last error message
    const std::string& error() const { return m_error; }

private:
    AddonManager();
    ~AddonManager() = default;

    // Prevent copying
    AddonManager(const AddonManager&) = delete;
    AddonManager& operator=(const AddonManager&) = delete;

    // -------------------------------------------------------------------------
    // Internal Methods
    // -------------------------------------------------------------------------

    /// @brief Parse addon.json from a path
    std::optional<AddonJson> parseAddonJson(const fs::path& path) const;

    /// @brief Get current platform identifier (darwin-arm64, etc.)
    std::string getPlatform() const;

    /// @brief Try to download and install prebuilt release
    bool tryPrebuiltRelease(const std::string& gitUrl, const std::string& ref,
                            const fs::path& addonDir);

    /// @brief Clone repo and build from source
    bool buildFromSource(const std::string& gitUrl, const std::string& ref,
                         const fs::path& addonDir);

    /// @brief Clone a git repository
    bool cloneRepo(const std::string& url, const std::string& ref, const fs::path& dest);

    /// @brief Run cmake build
    bool cmakeBuild(const fs::path& sourceDir, const fs::path& buildDir,
                    const fs::path& installDir);

    /// @brief Download a file from URL
    bool downloadFile(const std::string& url, const fs::path& dest);

    /// @brief Extract archive (tar.gz or zip)
    bool extractArchive(const fs::path& archive, const fs::path& dest);

    /// @brief Load manifest.json
    void loadManifest();

    /// @brief Save manifest.json
    void saveManifest() const;

    /// @brief Add addon to manifest
    void addToManifest(const InstalledAddon& addon);

    /// @brief Remove addon from manifest
    void removeFromManifest(const std::string& name);

    // -------------------------------------------------------------------------
    // Member Variables
    // -------------------------------------------------------------------------

    fs::path m_addonsDir;                          // ~/.vivid/addons
    std::vector<InstalledAddon> m_installedAddons; // Loaded from manifest.json
    std::string m_error;                           // Last error message
    std::vector<void*> m_loadedLibraries;          // Handles for dlclose
};

} // namespace vivid
