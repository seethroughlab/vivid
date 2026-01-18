#pragma once

// Vivid - Hot Reload
// Watches for file changes, recompiles, and reloads chain code

#include <vivid/vivid.h>  // For ChainConfig
#include <string>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>
#include <optional>

namespace fs = std::filesystem;

namespace vivid {

class Context;
class ModuleRegistry;

/**
 * @brief Structured representation of a compile error
 */
struct CompileError {
    std::string file;       ///< Source file path
    int line = 0;           ///< Line number (1-based)
    int column = 0;         ///< Column number (1-based)
    std::string severity;   ///< "error", "warning", "note"
    std::string message;    ///< Error message text
    std::string context;    ///< Optional source context

    /// Convert to JSON string
    std::string toJson() const;
};

// Chain function types
using SetupFn = void(*)(Context&);
using UpdateFn = void(*)(Context&);
using ConfigFn = ChainConfig(*)();

class HotReload {
public:
    HotReload();
    ~HotReload();

    // Non-copyable
    HotReload(const HotReload&) = delete;
    HotReload& operator=(const HotReload&) = delete;

    // Set the source file to watch (triggers reload on next check)
    void setSourceFile(const fs::path& chainPath);

    // Set source path for watching only (doesn't trigger reload)
    // Use when chain is already loaded and you just want to watch for changes
    void setSourcePath(const fs::path& chainPath);

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

    // Safe hot-reload API (preserves old chain on failure):
    // 1. Call tryCompile() to attempt compilation without affecting old chain
    // 2. If true, destroy old chain and call loadCompiled() to load new code
    // 3. If false, old chain is still valid - just show the error
    bool tryCompile();        // Compile only, returns true on success
    bool loadCompiled();      // Load last compiled library (unloads old first)

    // Get the current chain functions (may be null if not loaded)
    SetupFn getSetupFn() const { return m_setupFn; }
    UpdateFn getUpdateFn() const { return m_updateFn; }
    ConfigFn getConfigFn() const { return m_configFn; }

    // Get chain config (returns default config if not provided by chain)
    ChainConfig getConfig() const {
        return m_configFn ? m_configFn() : ChainConfig{};
    }

    // Check if chain provides a config
    bool hasConfig() const { return m_configFn != nullptr; }

    // Check if chain is loaded and valid
    bool isLoaded() const { return m_setupFn != nullptr && m_updateFn != nullptr; }

    // Get the last error message (compilation or loading)
    const std::string& getError() const { return m_error; }
    bool hasError() const { return !m_error.empty(); }

    // Get structured compile errors (parsed from compiler output)
    const std::vector<CompileError>& getCompileErrors() const { return m_compileErrors; }

    // Get errors as JSON string
    std::string getErrorsJson() const;

    // Force a reload
    void forceReload();

    // Set the vivid root directory explicitly (for embedded use)
    // When set, skips the automatic search for vivid installation
    void setRootDir(const fs::path& rootDir);

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
    ConfigFn m_configFn = nullptr;

    fs::file_time_type m_lastModTime;
    static inline int s_buildNumber = 0;  // Shared across all instances to avoid path collisions

    std::string m_error;
    std::vector<CompileError> m_compileErrors;  // Parsed errors
    bool m_needsSetup = false;      // True after reload, before setup is called

    std::unique_ptr<ModuleRegistry> m_moduleRegistry;
    fs::path m_rootDir;  // Explicit vivid root (if set, skips auto-search)

    // Parse compiler output into structured errors
    void parseCompilerOutput(const std::string& output);
};

} // namespace vivid
