#pragma once

/**
 * @file library_manager.h
 * @brief User library management (install, remove, update, load)
 *
 * Manages third-party libraries installed via `vivid libs install <git-url>`.
 * Libraries are installed to ~/.vivid/libs/ and loaded at runtime startup.
 */

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace vivid {

/**
 * @brief Metadata parsed from library.json
 */
struct LibraryJson {
    std::string name;
    std::string version;
    std::string description;
    std::string repository;
    std::string license;
    std::vector<std::string> dependencies;

    // Prebuilt binary URLs by platform (darwin-arm64, darwin-x64, linux-x64, win32-x64)
    struct PrebuiltUrls {
        std::string darwinArm64;
        std::string darwinX64;
        std::string linuxX64;
        std::string win32X64;
    } prebuilt;
};

/**
 * @brief Info about an installed library (from manifest.json)
 */
struct InstalledLibrary {
    std::string name;
    std::string version;
    std::string gitUrl;
    std::string gitRef;
    std::string installedAt;  // ISO 8601 timestamp
    std::string builtFrom;    // "prebuilt" or "source"
    fs::path installPath;     // ~/.vivid/libs/<name>
};

/**
 * @brief Manages user-installed libraries
 *
 * Directory structure:
 *   ~/.vivid/libs/
 *     manifest.json           - List of installed libraries
 *     <lib-name>/
 *       library.json          - Library metadata
 *       lib/                  - Libraries (dylib/so/dll)
 *       include/              - Headers
 *       src/                  - Source (git repo for rebuilds)
 *       build/                - CMake build directory
 */
class LibraryManager {
public:
    /// @brief Get singleton instance
    static LibraryManager& instance();

    // -------------------------------------------------------------------------
    // CLI Commands
    // -------------------------------------------------------------------------

    /**
     * @brief Install a library from a git URL
     * @param gitUrl Git repository URL (https://github.com/...)
     * @param ref Optional git ref (tag, branch, commit)
     * @return true on success
     */
    bool install(const std::string& gitUrl, const std::string& ref = "");

    /**
     * @brief Remove an installed library
     * @param name Library name
     * @return true on success
     */
    bool remove(const std::string& name);

    /**
     * @brief Update a library (or all libraries if name is empty)
     * @param name Library name (empty = update all)
     * @return true on success
     */
    bool update(const std::string& name = "");

    /**
     * @brief Get list of installed libraries
     */
    std::vector<InstalledLibrary> listInstalled() const;

    /**
     * @brief Output installed libraries as JSON to stdout
     */
    void outputJson() const;

    // -------------------------------------------------------------------------
    // Runtime Loading
    // -------------------------------------------------------------------------

    /**
     * @brief Load all user-installed libraries at runtime startup
     *
     * Called from main() before the main loop. Scans ~/.vivid/libs/
     * and dlopen's each library with RTLD_GLOBAL so static
     * initializers can register operators.
     */
    void loadUserLibraries();

    /**
     * @brief Get include paths for all installed libraries
     * Used by hot_reload.cpp for chain compilation
     */
    std::vector<fs::path> getIncludePaths() const;

    /**
     * @brief Get library paths for all installed libraries
     * Used by hot_reload.cpp for chain linking
     */
    std::vector<fs::path> getLibraryPaths() const;

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

    /// @brief Get the libraries directory (~/.vivid/libs)
    fs::path libsDir() const { return m_libsDir; }

    /// @brief Get last error message
    const std::string& error() const { return m_error; }

private:
    LibraryManager();
    ~LibraryManager() = default;

    // Prevent copying
    LibraryManager(const LibraryManager&) = delete;
    LibraryManager& operator=(const LibraryManager&) = delete;

    // -------------------------------------------------------------------------
    // Internal Methods
    // -------------------------------------------------------------------------

    /// @brief Parse library.json from a path
    std::optional<LibraryJson> parseLibraryJson(const fs::path& path) const;

    /// @brief Get current platform identifier (darwin-arm64, etc.)
    std::string getPlatform() const;

    /// @brief Try to download and install prebuilt release
    bool tryPrebuiltRelease(const std::string& gitUrl, const std::string& ref,
                            const fs::path& libDir);

    /// @brief Clone repo and build from source
    bool buildFromSource(const std::string& gitUrl, const std::string& ref,
                         const fs::path& libDir);

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

    /// @brief Add library to manifest
    void addToManifest(const InstalledLibrary& lib);

    /// @brief Remove library from manifest
    void removeFromManifest(const std::string& name);

    // -------------------------------------------------------------------------
    // Member Variables
    // -------------------------------------------------------------------------

    fs::path m_libsDir;                                // ~/.vivid/libs
    std::vector<InstalledLibrary> m_installedLibraries; // Loaded from manifest.json
    std::string m_error;                               // Last error message
    std::vector<void*> m_loadedLibraries;              // Handles for dlclose
};

} // namespace vivid
