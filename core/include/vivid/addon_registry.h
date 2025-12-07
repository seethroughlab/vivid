// Vivid Addon Registry
// Dynamic discovery of addons based on chain.cpp includes

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace vivid {

/// Metadata for a discovered addon
struct AddonInfo {
    std::string name;           // e.g., "vivid-effects-2d"
    std::string version;        // e.g., "0.1.0"
    std::string description;    // Human-readable description
    fs::path path;              // Path to addon root directory
    fs::path includePath;       // Path to include directory
    std::string libraryName;    // Library name without prefix/suffix (e.g., "vivid-effects-2d")
    std::vector<std::string> operators;  // List of operator names
};

/// Registry for discovering and managing addons
class AddonRegistry {
public:
    AddonRegistry() = default;

    /// Set the root directory for addon discovery (development mode)
    /// This is typically the vivid source root with addons/ subdirectory
    void setRootDir(const fs::path& rootDir);

    /// Discover which addons are needed by scanning a chain.cpp file
    /// Looks for #include <vivid/xxx/...> patterns and maps to addons
    std::vector<AddonInfo> discoverFromChain(const fs::path& chainPath);

    /// Get all known addon search paths
    std::vector<fs::path> getSearchPaths() const;

    /// Get info for a specific addon by name
    std::optional<AddonInfo> getAddon(const std::string& name) const;

    /// Get all discovered addons
    const std::vector<AddonInfo>& addons() const { return m_addons; }

private:
    /// Scan a source file for #include directives and extract addon names
    std::vector<std::string> scanIncludes(const fs::path& sourcePath);

    /// Map an include namespace to addon name (e.g., "video" -> "vivid-video")
    std::string namespaceToAddon(const std::string& ns);

    /// Load addon.json metadata from an addon directory
    std::optional<AddonInfo> loadAddonJson(const fs::path& addonPath);

    /// Find an addon by name in search paths
    std::optional<fs::path> findAddon(const std::string& name);

    fs::path m_rootDir;
    std::vector<AddonInfo> m_addons;
    std::vector<fs::path> m_searchPaths;
};

} // namespace vivid
