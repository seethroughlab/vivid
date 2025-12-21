#pragma once

/**
 * @file asset_loader.h
 * @brief Centralized asset loading for shaders and other resources
 *
 * Provides a single abstraction for loading assets from disk. Handles:
 * - Platform-specific executable path detection
 * - Search path management for development vs installed builds
 * - Optional caching for frequently-loaded assets
 * - Future: Emscripten virtual filesystem support for web export
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace vivid {

/**
 * @brief Centralized asset loading singleton
 *
 * Usage:
 * @code
 * // Load shader source
 * std::string shader = AssetLoader::instance().loadText("shaders/noise.wgsl");
 *
 * // Load binary asset
 * auto data = AssetLoader::instance().loadBinary("fonts/default.ttf");
 * @endcode
 */
class AssetLoader {
public:
    /// @brief Get singleton instance
    static AssetLoader& instance();

    // -------------------------------------------------------------------------
    /// @name Text Assets
    /// @{

    /**
     * @brief Load text asset (shaders, config files)
     * @param path Relative path to asset (e.g., "shaders/noise.wgsl")
     * @return File contents, or empty string if not found
     */
    std::string loadText(const std::string& path);

    /**
     * @brief Load shader by name (convenience for common pattern)
     * @param name Shader filename (e.g., "noise.wgsl")
     * @return Shader source, or empty string if not found
     *
     * Searches in shader directories automatically.
     */
    std::string loadShader(const std::string& name);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Binary Assets
    /// @{

    /**
     * @brief Load binary asset (images, fonts, etc.)
     * @param path Relative path to asset
     * @return File contents, or empty vector if not found
     */
    std::vector<uint8_t> loadBinary(const std::string& path);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Path Management
    /// @{

    /**
     * @brief Check if asset exists at path
     * @param path Relative path to asset
     * @return True if asset can be found
     */
    bool exists(const std::string& path);

    /**
     * @brief Get absolute path to asset
     * @param path Relative path to asset
     * @return Absolute path, or empty if not found
     */
    std::filesystem::path resolve(const std::string& path);

    /**
     * @brief Add a search path for assets
     * @param path Directory to search in
     */
    void addSearchPath(const std::filesystem::path& path);

    /**
     * @brief Set the executable directory (auto-detected if not set)
     * @param path Path to executable directory
     */
    void setExecutableDir(const std::filesystem::path& path);

    /**
     * @brief Get the executable directory
     */
    std::filesystem::path executableDir() const { return m_executableDir; }

    /**
     * @brief Set the project directory (where chain.cpp lives)
     * @param path Path to project directory
     *
     * This adds the project directory and its assets/ subfolder to the search paths.
     */
    void setProjectDir(const std::filesystem::path& path);

    /**
     * @brief Get the project directory
     */
    std::filesystem::path projectDir() const { return m_projectDir; }

    /**
     * @brief Register a named asset path prefix
     * @param name Prefix name (e.g., "shared", "fonts")
     * @param path Directory path (absolute or relative to project)
     *
     * Allows using "prefix:filename" syntax in asset paths.
     * Example: registerAssetPath("fonts", "/usr/share/fonts") enables
     * loading "fonts:OpenSans.ttf" which resolves to "/usr/share/fonts/OpenSans.ttf"
     */
    void registerAssetPath(const std::string& name, const std::filesystem::path& path);

    /**
     * @brief Get all registered asset paths (for bundling)
     * @return Map of prefix name to resolved directory path
     */
    std::unordered_map<std::string, std::filesystem::path> getRegisteredPaths() const {
        return m_registeredPaths;
    }

    /**
     * @brief Clear all registered asset paths
     */
    void clearRegisteredPaths() { m_registeredPaths.clear(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Cache Management
    /// @{

    /**
     * @brief Enable or disable caching
     * @param enable Whether to cache loaded assets
     */
    void setCacheEnabled(bool enable) { m_cacheEnabled = enable; }

    /**
     * @brief Clear all cached assets (for hot-reload)
     */
    void clearCache();

    /**
     * @brief Get list of all assets that have been loaded
     * @return Vector of asset paths (as originally requested)
     *
     * Useful for bundling - returns only assets that were actually used.
     */
    std::vector<std::string> getLoadedAssets() const;

    /**
     * @brief Get resolved paths of all loaded assets
     * @return Vector of absolute filesystem paths
     */
    std::vector<std::filesystem::path> getLoadedAssetPaths() const;

    /// @}

private:
    AssetLoader();
    ~AssetLoader() = default;

    // Non-copyable
    AssetLoader(const AssetLoader&) = delete;
    AssetLoader& operator=(const AssetLoader&) = delete;

    void detectExecutableDir();
    std::filesystem::path findAsset(const std::string& path);

    std::filesystem::path m_executableDir;
    std::filesystem::path m_projectDir;
    std::vector<std::filesystem::path> m_searchPaths;

    bool m_cacheEnabled = true;
    std::unordered_map<std::string, std::string> m_textCache;
    std::unordered_map<std::string, std::vector<uint8_t>> m_binaryCache;

    // Track loaded assets: requested path -> resolved absolute path
    std::unordered_map<std::string, std::filesystem::path> m_loadedAssets;

    // Registered asset path prefixes: prefix name -> directory path
    std::unordered_map<std::string, std::filesystem::path> m_registeredPaths;
};

} // namespace vivid
