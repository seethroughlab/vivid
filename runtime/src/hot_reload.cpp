// Hot Reload Implementation

#include "vivid/hot_reload.h"
#include "addon_registry.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <cstdlib>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <unistd.h>
#endif

namespace vivid {
namespace fs = std::filesystem;

// ============================================
// FileWatcher Implementation
// ============================================

void FileWatcher::watch(const fs::path& path) {
    path_ = path;
    initialized_ = false;
    if (exists()) {
        lastModTime_ = fs::last_write_time(path_);
        initialized_ = true;
    }
}

bool FileWatcher::checkForChanges() {
    if (!exists()) {
        return false;
    }

    auto currentTime = fs::last_write_time(path_);

    if (!initialized_) {
        lastModTime_ = currentTime;
        initialized_ = true;
        return true;  // First time seeing the file, treat as changed
    }

    if (currentTime != lastModTime_) {
        lastModTime_ = currentTime;
        return true;
    }

    return false;
}

bool FileWatcher::exists() const {
    return fs::exists(path_);
}

// ============================================
// DynamicLibrary Implementation
// ============================================

DynamicLibrary::~DynamicLibrary() {
    unload();
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    : handle_(other.handle_), lastError_(std::move(other.lastError_)) {
    other.handle_ = nullptr;
}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        unload();
        handle_ = other.handle_;
        lastError_ = std::move(other.lastError_);
        other.handle_ = nullptr;
    }
    return *this;
}

bool DynamicLibrary::load(const fs::path& path) {
    unload();

#if defined(_WIN32)
    handle_ = LoadLibraryW(path.c_str());
    if (!handle_) {
        DWORD error = GetLastError();
        char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, buf, sizeof(buf), nullptr);
        lastError_ = buf;
        return false;
    }
#else
    // Clear any existing errors
    dlerror();

    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        const char* error = dlerror();
        lastError_ = error ? error : "Unknown error";
        return false;
    }
#endif

    lastError_.clear();
    return true;
}

void DynamicLibrary::unload() {
    if (!handle_) return;

#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle_));
#else
    dlclose(handle_);
#endif

    handle_ = nullptr;
}

void* DynamicLibrary::getSymbol(const char* name) {
    if (!handle_) {
        lastError_ = "Library not loaded";
        return nullptr;
    }

#if defined(_WIN32)
    void* symbol = GetProcAddress(static_cast<HMODULE>(handle_), name);
    if (!symbol) {
        DWORD error = GetLastError();
        char buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error, 0, buf, sizeof(buf), nullptr);
        lastError_ = buf;
    }
#else
    dlerror();  // Clear errors
    void* symbol = dlsym(handle_, name);
    const char* error = dlerror();
    if (error) {
        lastError_ = error;
        return nullptr;
    }
#endif

    return symbol;
}

// ============================================
// Compiler Implementation
// ============================================

Compiler::Compiler() {
    // Detect compiler
#if defined(_WIN32)
    // Try to find MSVC or clang-cl
    compilerPath_ = "cl.exe";  // Will rely on PATH
#elif defined(__APPLE__)
    compilerPath_ = "clang++";
#else
    compilerPath_ = "g++";
#endif
}

void Compiler::setIncludePath(const fs::path& path) {
    if (includePaths_.empty()) {
        includePaths_.push_back(path);
    } else {
        includePaths_[0] = path;
    }
}

void Compiler::addIncludePath(const fs::path& path) {
    includePaths_.push_back(path);
}

void Compiler::addLibraryPath(const fs::path& path) {
    libraryPaths_.push_back(path);
}

void Compiler::addLibrary(const std::string& lib) {
    libraries_.push_back(lib);
}

std::string Compiler::buildCompileCommand(const fs::path& source,
                                          const fs::path& output) {
    std::ostringstream cmd;

#if defined(_WIN32)
    // MSVC command
    cmd << "\"" << compilerPath_ << "\" /nologo /EHsc /O2 /LD ";
    for (const auto& inc : includePaths_) {
        cmd << "/I\"" << inc.string() << "\" ";
    }
    cmd << "\"" << source.string() << "\" ";
    cmd << "/Fe\"" << output.string() << "\" ";
    for (const auto& libPath : libraryPaths_) {
        cmd << "/LIBPATH:\"" << libPath.string() << "\" ";
    }
    for (const auto& lib : libraries_) {
        cmd << lib << ".lib ";
    }
#else
    // Clang/GCC command
    cmd << compilerPath_ << " -std=c++17 -O2 -shared -fPIC ";

#if defined(__APPLE__)
    cmd << "-dynamiclib -undefined dynamic_lookup ";
    cmd << "-DPLATFORM_MACOS=1 ";
#endif

    for (const auto& inc : includePaths_) {
        cmd << "-I\"" << inc.string() << "\" ";
    }

    cmd << "\"" << source.string() << "\" ";
    cmd << "-o \"" << output.string() << "\" ";

    for (const auto& libPath : libraryPaths_) {
        cmd << "-L\"" << libPath.string() << "\" ";
#if defined(__APPLE__)
        // Add rpath so the dylib can find dependencies
        cmd << "-Wl,-rpath,\"" << fs::absolute(libPath).string() << "\" ";
#else
        cmd << "-Wl,-rpath,\"" << libPath.string() << "\" ";
#endif
    }

    for (const auto& lib : libraries_) {
        // Check if it's a full path or just a library name
        if (lib.find('/') != std::string::npos || lib.find('\\') != std::string::npos) {
            // Full path - use directly
            cmd << "\"" << lib << "\" ";
        } else {
            // Library name - use -l flag
            cmd << "-l" << lib << " ";
        }
    }
#endif

    // Redirect stderr to stdout for capture
    cmd << " 2>&1";

    return cmd.str();
}

bool Compiler::compile(const fs::path& source, const fs::path& output) {
    if (!fs::exists(source)) {
        lastError_ = "Source file not found: " + source.string();
        return false;
    }

    std::string cmd = buildCompileCommand(source, output);
    std::cout << "[Hot Reload] Compiling: " << source.filename().string() << std::endl;

    // Execute compiler
    std::array<char, 4096> buffer;
    std::string result;

#if defined(_WIN32)
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif

    if (!pipe) {
        lastError_ = "Failed to execute compiler";
        return false;
    }

    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }

#if defined(_WIN32)
    int exitCode = _pclose(pipe);
#else
    int exitCode = pclose(pipe);
#endif

    output_ = result;

    if (exitCode != 0) {
        lastError_ = "Compilation failed with exit code " + std::to_string(exitCode);
        std::cerr << "[Hot Reload] Compile error:\n" << result << std::endl;
        return false;
    }

    if (!result.empty()) {
        std::cout << "[Hot Reload] Compiler output:\n" << result << std::endl;
    }

    std::cout << "[Hot Reload] Compiled successfully: " << output.filename().string() << std::endl;
    return true;
}

// ============================================
// HotReload Implementation
// ============================================

HotReload::HotReload()
    : addonRegistry_(std::make_unique<AddonRegistry>()) {
}

HotReload::~HotReload() {
    library_.unload();
}

void HotReload::setRuntimePath(const fs::path& path) {
    runtimePath_ = path;
}

bool HotReload::init(const fs::path& projectPath) {
    projectPath_ = projectPath;

    // Look for chain.cpp in project directory
    sourcePath_ = projectPath / "chain.cpp";

    if (!fs::exists(sourcePath_)) {
        lastError_ = "chain.cpp not found in " + projectPath.string();
        return false;
    }

    // Set up compiler paths
    // Include paths: vivid headers
    if (!runtimePath_.empty()) {
        compiler_.setIncludePath(runtimePath_ / "include");

        // Add Diligent Engine includes
        fs::path diligentPath = runtimePath_.parent_path() / "external" / "DiligentEngine";
        if (fs::exists(diligentPath)) {
            compiler_.addIncludePath(diligentPath / "DiligentCore" / "Primitives" / "interface");
            compiler_.addIncludePath(diligentPath / "DiligentCore" / "Common" / "interface");
            compiler_.addIncludePath(diligentPath / "DiligentCore" / "Graphics" / "GraphicsEngine" / "interface");
            compiler_.addIncludePath(diligentPath / "DiligentCore" / "Graphics" / "GraphicsAccessories" / "interface");
            compiler_.addIncludePath(diligentPath / "DiligentCore" / "Graphics" / "GraphicsTools" / "interface");
            compiler_.addIncludePath(diligentPath / "DiligentTools" / "TextureLoader" / "interface");
            compiler_.addIncludePath(diligentPath / "DiligentFX" / "PBR" / "interface");
            compiler_.addIncludePath(diligentPath / "DiligentFX" / "Shaders" / "Common" / "public");
            compiler_.addIncludePath(diligentPath / "DiligentFX" / "Shaders" / "PBR" / "public");
            compiler_.addIncludePath(diligentPath / "DiligentFX" / "Shaders" / "PBR" / "private");
        }

        // GLM
        fs::path glmPath = runtimePath_.parent_path() / "external" / "glm";
        if (fs::exists(glmPath)) {
            compiler_.addIncludePath(glmPath);
        }

        // GLFW (if needed)
        fs::path glfwPath = runtimePath_.parent_path() / "external" / "glfw" / "include";
        if (fs::exists(glfwPath)) {
            compiler_.addIncludePath(glfwPath);
        }
    }

    // Create build directory for outputs
    fs::path buildDir = projectPath / ".vivid-build";
    if (!fs::exists(buildDir)) {
        fs::create_directories(buildDir);
    }

    // Set up addon paths - try multiple locations
    // 1. From runtime path's parent (source tree: /path/to/vivid/build/addons)
    // 2. From project path's parent (working dir based: /path/to/vivid/build/addons)
    fs::path addonsBuildDir;
    if (!runtimePath_.empty()) {
        addonsBuildDir = runtimePath_.parent_path() / "build";
    }
    if (addonsBuildDir.empty() || !fs::exists(addonsBuildDir / "addons")) {
        // Try from project path
        addonsBuildDir = fs::absolute(projectPath_).parent_path().parent_path() / "build";
    }
    if (!fs::exists(addonsBuildDir / "addons")) {
        // Try current working directory based
        addonsBuildDir = fs::current_path() / "build";
    }

    if (fs::exists(addonsBuildDir / "addons")) {
        addonsLibDir_ = addonsBuildDir / "addons" / "lib";
        addonsIncludeDir_ = addonsBuildDir / "addons" / "include";
        fs::path addonsMetaDir = addonsBuildDir / "addons" / "meta";

        // Load addon registry
        int addonCount = addonRegistry_->loadFromDirectory(addonsMetaDir);
        if (addonCount > 0) {
            std::cout << "[Hot Reload] Loaded " << addonCount << " addon(s)" << std::endl;
            std::cout << "[Hot Reload] Addon lib dir: " << addonsLibDir_ << std::endl;

            // Add addon include directory
            if (fs::exists(addonsIncludeDir_)) {
                compiler_.addIncludePath(addonsIncludeDir_);
            }
        }
    } else {
        std::cout << "[Hot Reload] No addons directory found at: " << addonsBuildDir / "addons" << std::endl;
    }

    // Set up file watcher
    watcher_.watch(sourcePath_);

    // Initial compile
    return compileAndLoad();
}

bool HotReload::poll() {
    if (watcher_.checkForChanges()) {
        std::cout << "[Hot Reload] File changed, reloading..." << std::endl;
        return reload();
    }
    return false;
}

bool HotReload::reload() {
    return compileAndLoad();
}

void HotReload::setupAddonCompilerPaths() {
    requiredAddons_.clear();

    // Scan source for addon includes
    requiredAddons_ = addonRegistry_->scanSourceForAddons(sourcePath_);

    if (!requiredAddons_.empty()) {
        std::cout << "[Hot Reload] Detected addons: ";
        for (size_t i = 0; i < requiredAddons_.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << requiredAddons_[i];
        }
        std::cout << std::endl;

        // Add library path for addons
        if (fs::exists(addonsLibDir_)) {
            compiler_.addLibraryPath(addonsLibDir_);
        }

        // Add libraries for each addon
        std::string platform = AddonRegistry::currentPlatform();
        for (const auto& addonName : requiredAddons_) {
            const AddonInfo* addon = addonRegistry_->getAddon(addonName);
            if (addon && addon->supportsPlatform(platform)) {
                auto libs = addon->getLibraries(platform);
                for (const auto& lib : libs) {
                    // Use full path for static libraries
                    fs::path libPath = addonsLibDir_ / lib;
                    if (fs::exists(libPath)) {
                        std::cout << "[Hot Reload] Adding library: " << libPath << std::endl;
                        compiler_.addLibrary(libPath.string());
                    } else {
                        std::cerr << "[Hot Reload] Library not found: " << libPath << std::endl;
                    }
                }
            } else if (addon) {
                std::cerr << "[Hot Reload] Addon " << addonName << " does not support platform " << platform << std::endl;
            } else {
                std::cerr << "[Hot Reload] Addon not found: " << addonName << std::endl;
            }
        }
    }
}

bool HotReload::compileAndLoad() {
    hasCompileError_ = false;
    setup_ = nullptr;
    update_ = nullptr;

    // Unload old library
    library_.unload();

    // Scan for addon usage and configure compiler
    setupAddonCompilerPaths();

    // Create unique library name (to avoid caching issues)
    buildNumber_++;
    fs::path buildDir = projectPath_ / ".vivid-build";

#if defined(_WIN32)
    std::string libExt = ".dll";
#elif defined(__APPLE__)
    std::string libExt = ".dylib";
#else
    std::string libExt = ".so";
#endif

    libraryPath_ = buildDir / ("chain_" + std::to_string(buildNumber_) + libExt);

    // Compile
    if (!compiler_.compile(sourcePath_, libraryPath_)) {
        hasCompileError_ = true;
        lastError_ = compiler_.lastError();
        return false;
    }

    // Load
    if (!library_.load(libraryPath_)) {
        lastError_ = "Failed to load library: " + library_.lastError();
        return false;
    }

    // Get symbols
    setup_ = library_.getFunction<SetupFn>("vivid_setup");
    update_ = library_.getFunction<UpdateFn>("vivid_update");

    if (!setup_) {
        std::cerr << "[Hot Reload] Warning: vivid_setup not found" << std::endl;
    }
    if (!update_) {
        lastError_ = "vivid_update function not found in library";
        return false;
    }

    std::cout << "[Hot Reload] Loaded successfully" << std::endl;

    // Clean up old build artifacts (keep last 3)
    std::vector<fs::path> oldBuilds;
    for (const auto& entry : fs::directory_iterator(buildDir)) {
        if (entry.path().extension() == libExt &&
            entry.path() != libraryPath_) {
            oldBuilds.push_back(entry.path());
        }
    }

    // Sort by name (which includes build number) and remove old ones
    std::sort(oldBuilds.begin(), oldBuilds.end());
    while (oldBuilds.size() > 2) {
        fs::remove(oldBuilds.front());
        oldBuilds.erase(oldBuilds.begin());
    }

    return true;
}

} // namespace vivid
