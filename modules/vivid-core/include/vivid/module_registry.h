// Vivid Module Registry
// Dynamic discovery of modules based on chain.cpp includes

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace vivid {

/// Metadata for a discovered module
struct ModuleInfo {
    std::string name;           // e.g., "vivid-audio"
    std::string version;        // e.g., "0.1.0"
    std::string description;    // Human-readable description
    fs::path path;              // Path to library root directory
    fs::path includePath;       // Path to include directory
    std::string libraryName;    // Library name without prefix/suffix (e.g., "vivid-audio")
    std::vector<std::string> dependencies;  // Other modules this module depends on
};

/// Registry for discovering and managing libraries
class ModuleRegistry {
public:
    ModuleRegistry() = default;

    /// Set the root directory for library discovery (development mode)
    /// This is typically the vivid source root with modules/ subdirectory
    void setRootDir(const fs::path& rootDir);

    /// Discover which modules are needed by scanning a chain.cpp file
    /// Looks for #include <vivid/xxx/...> patterns and maps to modules
    std::vector<ModuleInfo> discoverFromChain(const fs::path& chainPath);

    /// Get all known module search paths
    std::vector<fs::path> getSearchPaths() const;

    /// Get info for a specific module by name
    std::optional<ModuleInfo> getModule(const std::string& name) const;

    /// Get all discovered modules
    const std::vector<ModuleInfo>& modules() const { return m_modules; }

    /// Register a bundled library directly (for installed/bundled apps)
    /// This adds a minimal ModuleInfo so the library gets linked
    void registerBundledLibrary(const std::string& libraryName, const fs::path& libDir);

private:
    /// Scan a source file for #include directives and extract module names
    std::vector<std::string> scanIncludes(const fs::path& sourcePath);

    /// Map an include namespace to module name (e.g., "video" -> "vivid-video")
    std::string namespaceToModule(const std::string& ns);

    /// Load module.json metadata from a module directory
    std::optional<ModuleInfo> loadModuleJson(const fs::path& modulePath);

    /// Find a module by name in search paths
    std::optional<fs::path> findModule(const std::string& name);

    fs::path m_rootDir;
    std::vector<ModuleInfo> m_modules;
    std::vector<fs::path> m_searchPaths;
};

} // namespace vivid
