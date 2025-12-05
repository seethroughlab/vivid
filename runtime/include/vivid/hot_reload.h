// Hot Reload System for Vivid
// Watches chain.cpp files and recompiles on changes

#pragma once

#include <string>
#include <functional>
#include <chrono>
#include <vector>

// Filesystem support
#if __has_include(<filesystem>)
    #include <filesystem>
    namespace vivid { namespace fs = std::filesystem; }
#elif __has_include(<experimental/filesystem>)
    #include <experimental/filesystem>
    namespace vivid { namespace fs = std::experimental::filesystem; }
#else
    #error "No filesystem support"
#endif

namespace vivid {

class Context;

// Function signatures for user chain code
using SetupFn = void(*)(Context&);
using UpdateFn = void(*)(Context&);

// File watcher using polling (cross-platform, no dependencies)
class FileWatcher {
public:
    FileWatcher() = default;
    ~FileWatcher() = default;

    // Set the file to watch
    void watch(const fs::path& path);

    // Check if file has changed since last check
    // Returns true if file was modified
    bool checkForChanges();

    // Get the watched file path
    const fs::path& path() const { return path_; }

    // Check if file exists
    bool exists() const;

private:
    fs::path path_;
    fs::file_time_type lastModTime_;
    bool initialized_ = false;
};

// Dynamic library loader (cross-platform)
class DynamicLibrary {
public:
    DynamicLibrary() = default;
    ~DynamicLibrary();

    // Non-copyable
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    // Move semantics
    DynamicLibrary(DynamicLibrary&& other) noexcept;
    DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

    // Load a shared library
    bool load(const fs::path& path);

    // Unload the library
    void unload();

    // Check if loaded
    bool isLoaded() const { return handle_ != nullptr; }

    // Get a symbol from the library
    void* getSymbol(const char* name);

    // Get typed function pointer
    template<typename T>
    T getFunction(const char* name) {
        return reinterpret_cast<T>(getSymbol(name));
    }

    // Get last error message
    const std::string& lastError() const { return lastError_; }

private:
    void* handle_ = nullptr;
    std::string lastError_;
};

// Compiler for chain.cpp files
class Compiler {
public:
    Compiler();
    ~Compiler() = default;

    // Set the Vivid runtime include path
    void setIncludePath(const fs::path& path);

    // Set additional include paths
    void addIncludePath(const fs::path& path);

    // Set library search paths
    void addLibraryPath(const fs::path& path);

    // Set libraries to link
    void addLibrary(const std::string& lib);

    // Compile a source file to a shared library
    // Returns true on success, false on failure
    bool compile(const fs::path& source,
                 const fs::path& output);

    // Get compiler output (errors, warnings)
    const std::string& output() const { return output_; }

    // Get last error
    const std::string& lastError() const { return lastError_; }

private:
    std::vector<fs::path> includePaths_;
    std::vector<fs::path> libraryPaths_;
    std::vector<std::string> libraries_;
    std::string compilerPath_;
    std::string output_;
    std::string lastError_;

    // MSVC-specific paths (Windows only)
    std::string vcToolsPath_;
    std::string vsInstallPath_;

    std::string buildCompileCommand(const fs::path& source,
                                    const fs::path& output);
};

// Hot reload manager - combines file watching, compiling, and loading
class HotReload {
public:
    HotReload();
    ~HotReload();

    // Set up for a project directory
    // Looks for chain.cpp in the directory
    bool init(const fs::path& projectPath);

    // Set vivid runtime path (for includes and libraries)
    void setRuntimePath(const fs::path& path);

    // Poll for changes and reload if necessary
    // Returns true if a reload occurred
    bool poll();

    // Force a reload (recompile and load)
    bool reload();

    // Check if code is loaded and ready
    bool isReady() const { return setup_ != nullptr && update_ != nullptr; }

    // Get the setup function
    SetupFn setup() const { return setup_; }

    // Get the update function
    UpdateFn update() const { return update_; }

    // Get last error
    const std::string& lastError() const { return lastError_; }

    // Get compiler output
    const std::string& compilerOutput() const { return compiler_.output(); }

    // Check if there was a compile error
    bool hasCompileError() const { return hasCompileError_; }

private:
    FileWatcher watcher_;
    Compiler compiler_;
    DynamicLibrary library_;

    fs::path projectPath_;
    fs::path runtimePath_;
    fs::path sourcePath_;
    fs::path libraryPath_;

    SetupFn setup_ = nullptr;
    UpdateFn update_ = nullptr;

    std::string lastError_;
    bool hasCompileError_ = false;
    int buildNumber_ = 0;  // Incremented each build to create unique library names

    bool compileAndLoad();
};

} // namespace vivid
