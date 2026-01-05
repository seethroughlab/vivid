// Vivid Library Registry
// Dynamic discovery of libraries based on chain.cpp includes

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <optional>

namespace fs = std::filesystem;

namespace vivid {

/// Metadata for a discovered library
struct LibraryInfo {
    std::string name;           // e.g., "vivid-audio"
    std::string version;        // e.g., "0.1.0"
    std::string description;    // Human-readable description
    fs::path path;              // Path to library root directory
    fs::path includePath;       // Path to include directory
    std::string libraryName;    // Library name without prefix/suffix (e.g., "vivid-audio")
};

/// Registry for discovering and managing libraries
class LibraryRegistry {
public:
    LibraryRegistry() = default;

    /// Set the root directory for library discovery (development mode)
    /// This is typically the vivid source root with libs/ subdirectory
    void setRootDir(const fs::path& rootDir);

    /// Discover which libraries are needed by scanning a chain.cpp file
    /// Looks for #include <vivid/xxx/...> patterns and maps to libraries
    std::vector<LibraryInfo> discoverFromChain(const fs::path& chainPath);

    /// Get all known library search paths
    std::vector<fs::path> getSearchPaths() const;

    /// Get info for a specific library by name
    std::optional<LibraryInfo> getLibrary(const std::string& name) const;

    /// Get all discovered libraries
    const std::vector<LibraryInfo>& libraries() const { return m_libraries; }

private:
    /// Scan a source file for #include directives and extract library names
    std::vector<std::string> scanIncludes(const fs::path& sourcePath);

    /// Map an include namespace to library name (e.g., "video" -> "vivid-video")
    std::string namespaceToLibrary(const std::string& ns);

    /// Load library.json metadata from a library directory
    std::optional<LibraryInfo> loadLibraryJson(const fs::path& libraryPath);

    /// Find a library by name in search paths
    std::optional<fs::path> findLibrary(const std::string& name);

    fs::path m_rootDir;
    std::vector<LibraryInfo> m_libraries;
    std::vector<fs::path> m_searchPaths;
};

} // namespace vivid
