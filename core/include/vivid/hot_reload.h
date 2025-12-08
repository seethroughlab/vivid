#pragma once

// Vivid - Hot Reload
// Watches for file changes, recompiles, and reloads chain code

#include <string>
#include <filesystem>
#include <functional>
#include <memory>

namespace fs = std::filesystem;

namespace vivid {

class Context;
class AddonRegistry;

// Chain function types
using SetupFn = void(*)(Context&);
using UpdateFn = void(*)(Context&);

class HotReload {
public:
    HotReload();
    ~HotReload();

    // Non-copyable
    HotReload(const HotReload&) = delete;
    HotReload& operator=(const HotReload&) = delete;

    // Set the source file to watch
    void setSourceFile(const fs::path& chainPath);

    // Check for changes and reload if needed
    // Returns true if chain was reloaded
    // WARNING: This unloads the old library - destroy chain operators BEFORE calling!
    bool update();

    // Split API for safe hot-reload:
    // 1. Call checkNeedsReload() to see if file changed
    // 2. If true, destroy your chain operators (while old code is still loaded)
    // 3. Then call reload() to compile and load new code
    bool checkNeedsReload();  // Returns true if source file changed
    bool reload();            // Compile and load (unloads old library first)

    // Get the current chain functions (may be null if not loaded)
    SetupFn getSetupFn() const { return m_setupFn; }
    UpdateFn getUpdateFn() const { return m_updateFn; }

    // Check if chain is loaded and valid
    bool isLoaded() const { return m_setupFn != nullptr && m_updateFn != nullptr; }

    // Get the last error message (compilation or loading)
    const std::string& getError() const { return m_error; }
    bool hasError() const { return !m_error.empty(); }

    // Force a reload
    void forceReload();

private:
    bool compile();
    bool load();
    void unload();
    fs::file_time_type getFileModTime() const;

    fs::path m_sourcePath;          // Path to chain.cpp
    fs::path m_buildDir;            // Build directory for compiled libraries
    fs::path m_libraryPath;         // Path to current compiled library

    void* m_library = nullptr;      // Handle to loaded library (dlopen result)
    SetupFn m_setupFn = nullptr;
    UpdateFn m_updateFn = nullptr;

    fs::file_time_type m_lastModTime;
    int m_buildNumber = 0;          // Incremented each build to avoid caching

    std::string m_error;
    bool m_needsSetup = false;      // True after reload, before setup is called

    std::unique_ptr<AddonRegistry> m_addonRegistry;
};

} // namespace vivid
