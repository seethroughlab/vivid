// Vivid - Hot Reload Implementation

#include <vivid/hot_reload.h>
#include <vivid/addon_registry.h>
#include <vivid/context.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <array>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif
#endif

namespace vivid {

// Execute a command and capture output
static std::pair<int, std::string> executeCommand(const std::string& cmd) {
    std::string output;
    int exitCode = -1;

#ifdef _WIN32
    // Windows implementation
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (pipe) {
        std::array<char, 256> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }
        exitCode = _pclose(pipe);
    }
#else
    // Unix implementation
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        std::array<char, 256> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }
        exitCode = pclose(pipe);
        // pclose returns the exit status in a special format
        if (WIFEXITED(exitCode)) {
            exitCode = WEXITSTATUS(exitCode);
        }
    }
#endif

    return {exitCode, output};
}

HotReload::HotReload()
    : m_addonRegistry(std::make_unique<AddonRegistry>())
{
    // Create a build directory in temp
#ifdef _WIN32
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    m_buildDir = fs::path(tempPath) / "vivid_build";
#else
    m_buildDir = fs::path("/tmp/vivid_build");
#endif

    // Ensure build directory exists
    std::error_code ec;
    fs::create_directories(m_buildDir, ec);
}

HotReload::~HotReload() {
    unload();
}

void HotReload::setSourceFile(const fs::path& chainPath) {
    if (m_sourcePath != chainPath) {
        m_sourcePath = chainPath;
        m_lastModTime = fs::file_time_type::min();
        forceReload();
    }
}

fs::file_time_type HotReload::getFileModTime() const {
    if (m_sourcePath.empty() || !fs::exists(m_sourcePath)) {
        return fs::file_time_type::min();
    }

    std::error_code ec;
    auto modTime = fs::last_write_time(m_sourcePath, ec);
    if (ec) {
        return fs::file_time_type::min();
    }
    return modTime;
}

void HotReload::forceReload() {
    m_lastModTime = fs::file_time_type::min();
}

bool HotReload::update() {
    if (m_sourcePath.empty()) {
        return false;
    }

    // Check if file exists
    if (!fs::exists(m_sourcePath)) {
        m_error = "Source file not found: " + m_sourcePath.string();
        return false;
    }

    // Check modification time
    auto modTime = getFileModTime();
    if (modTime == m_lastModTime) {
        return false;  // No changes
    }

    m_lastModTime = modTime;

    std::cout << "Detected change in " << m_sourcePath.filename() << ", reloading..." << std::endl;

    // Compile
    if (!compile()) {
        return false;
    }

    // Load
    if (!load()) {
        return false;
    }

    m_error.clear();
    m_needsSetup = true;
    return true;
}

// Find the directory containing the vivid executable
static fs::path getExecutableDir() {
#ifdef __APPLE__
    char pathBuf[PATH_MAX];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        return fs::path(pathBuf).parent_path();
    }
#elif defined(_WIN32)
    char pathBuf[MAX_PATH];
    GetModuleFileNameA(NULL, pathBuf, MAX_PATH);
    return fs::path(pathBuf).parent_path();
#else
    // Linux: read /proc/self/exe
    char pathBuf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
    if (len != -1) {
        pathBuf[len] = '\0';
        return fs::path(pathBuf).parent_path();
    }
#endif
    return fs::current_path();
}

bool HotReload::compile() {
    // Increment build number to avoid OS library caching
    m_buildNumber++;

    // Build library name with build number
    std::string libName = "chain_" + std::to_string(m_buildNumber);

#ifdef _WIN32
    libName += ".dll";
#elif defined(__APPLE__)
    libName = "lib" + libName + ".dylib";
#else
    libName = "lib" + libName + ".so";
#endif

    m_libraryPath = m_buildDir / libName;

    // Determine directories
    fs::path sourceDir = m_sourcePath.parent_path();
    fs::path exeDir = getExecutableDir();

    // Find vivid include directories (look relative to exe or in source tree)
    fs::path vividInclude;
    fs::path addonsLib;
    std::vector<fs::path> depIncludes;

    // Try source tree first (development mode)
    // exe is at build/bin/vivid, so ../ twice gets to vivid root
    fs::path rootDir = exeDir / ".." / "..";
    fs::path devVividInclude = rootDir / "core" / "include";

    bool isDevelopmentMode = fs::exists(devVividInclude / "vivid");

    if (isDevelopmentMode) {
        vividInclude = fs::canonical(devVividInclude);
        addonsLib = exeDir;

        // Set up addon registry for dynamic discovery
        m_addonRegistry->setRootDir(fs::canonical(rootDir));

        // Discover addons needed by this chain
        auto addons = m_addonRegistry->discoverFromChain(m_sourcePath);

        // Add include paths for each discovered addon
        for (const auto& addon : addons) {
            if (fs::exists(addon.includePath)) {
                depIncludes.push_back(addon.includePath);
            }
        }

        // Find dependency includes in build/_deps (GLM, etc.)
        fs::path depsDir = rootDir / "build" / "_deps";
        if (fs::exists(depsDir)) {
            for (const auto& entry : fs::directory_iterator(depsDir)) {
                if (entry.is_directory()) {
                    // Check for include/ subdirectory first
                    fs::path incDir = entry.path() / "include";
                    if (fs::exists(incDir)) {
                        depIncludes.push_back(incDir);
                    }
                    // Also add root for deps like GLM that put headers at root
                    if (fs::exists(entry.path() / "glm" / "glm.hpp")) {
                        depIncludes.push_back(entry.path());
                    }
                }
            }
        }
    } else {
        // Fallback to installed location
        vividInclude = exeDir / ".." / "include";
        addonsLib = exeDir;
    }

    // Build the compile command
    std::stringstream cmd;

#ifdef _WIN32
    // MSVC or Clang on Windows
    cmd << "cl /nologo /EHsc /LD /O2 /std:c++17 ";
    cmd << "/I\"" << sourceDir.string() << "\" ";
    cmd << "/I\"" << vividInclude.string() << "\" ";
    for (const auto& inc : depIncludes) {
        cmd << "/I\"" << inc.string() << "\" ";
    }
    cmd << "/Fe\"" << m_libraryPath.string() << "\" ";
    cmd << "\"" << m_sourcePath.string() << "\" ";
    cmd << "/link ";
    // Link discovered addons
    for (const auto& addon : m_addonRegistry->addons()) {
        fs::path libPath = addonsLib / (addon.libraryName + ".lib");
        if (fs::exists(libPath)) {
            cmd << "\"" << libPath.string() << "\" ";
        }
    }
    cmd << "2>&1";
#else
    // Clang/GCC on Unix
    cmd << "clang++ -std=c++17 -O2 -shared -fPIC ";
    cmd << "-I\"" << sourceDir.string() << "\" ";
    cmd << "-I\"" << vividInclude.string() << "\" ";
    for (const auto& inc : depIncludes) {
        cmd << "-I\"" << inc.string() << "\" ";
    }

#ifdef __APPLE__
    cmd << "-undefined dynamic_lookup ";  // Allow symbols from vivid executable
    cmd << "-L\"" << addonsLib.string() << "\" ";
    // Link discovered addons
    for (const auto& addon : m_addonRegistry->addons()) {
        fs::path libPath = addonsLib / ("lib" + addon.libraryName + ".dylib");
        if (fs::exists(libPath)) {
            cmd << "-l" << addon.libraryName << " ";
        }
    }
    cmd << "-Wl,-rpath,\"" << addonsLib.string() << "\" ";
#else
    cmd << "-L\"" << addonsLib.string() << "\" ";
    // Link discovered addons
    for (const auto& addon : m_addonRegistry->addons()) {
        fs::path libPath = addonsLib / ("lib" + addon.libraryName + ".so");
        if (fs::exists(libPath)) {
            cmd << "-l" << addon.libraryName << " ";
        }
    }
    cmd << "-Wl,-rpath,\"" << addonsLib.string() << "\" ";
#endif

    cmd << "-o \"" << m_libraryPath.string() << "\" ";
    cmd << "\"" << m_sourcePath.string() << "\" ";
    cmd << "2>&1";
#endif

    std::cout << "Compiling: " << m_sourcePath.filename() << std::endl;

    auto [exitCode, output] = executeCommand(cmd.str());

    if (exitCode != 0) {
        m_error = "Compilation failed:\n" + output;
        std::cerr << m_error << std::endl;
        return false;
    }

    if (!output.empty()) {
        // Print warnings
        std::cout << output;
    }

    std::cout << "Compilation successful" << std::endl;
    return true;
}

bool HotReload::load() {
    // Unload previous library
    unload();

    if (!fs::exists(m_libraryPath)) {
        m_error = "Compiled library not found: " + m_libraryPath.string();
        return false;
    }

#ifdef _WIN32
    m_library = LoadLibraryA(m_libraryPath.string().c_str());
    if (!m_library) {
        m_error = "Failed to load library: " + std::to_string(GetLastError());
        return false;
    }

    m_setupFn = reinterpret_cast<SetupFn>(GetProcAddress(static_cast<HMODULE>(m_library), "vivid_setup"));
    m_updateFn = reinterpret_cast<UpdateFn>(GetProcAddress(static_cast<HMODULE>(m_library), "vivid_update"));
#else
    m_library = dlopen(m_libraryPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!m_library) {
        m_error = "Failed to load library: " + std::string(dlerror());
        std::cerr << m_error << std::endl;
        return false;
    }

    m_setupFn = reinterpret_cast<SetupFn>(dlsym(m_library, "vivid_setup"));
    m_updateFn = reinterpret_cast<UpdateFn>(dlsym(m_library, "vivid_update"));
#endif

    if (!m_setupFn || !m_updateFn) {
        m_error = "Failed to find entry points. Make sure chain.cpp uses VIVID_CHAIN macro.";
        unload();
        return false;
    }

    std::cout << "Chain loaded successfully" << std::endl;
    return true;
}

void HotReload::unload() {
    if (m_library) {
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(m_library));
#else
        dlclose(m_library);
#endif
        m_library = nullptr;
    }

    m_setupFn = nullptr;
    m_updateFn = nullptr;
}

} // namespace vivid
