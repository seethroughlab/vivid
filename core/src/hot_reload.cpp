// Vivid - Hot Reload Implementation

#include <vivid/hot_reload.h>
#include <vivid/addon_registry.h>
#include <vivid/addon_manager.h>
#include <vivid/context.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <array>
#include <chrono>
#include <regex>

#ifdef _WIN32
#define NOMINMAX
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

#ifdef _WIN32
// Find Visual Studio installation and return path to vcvarsall.bat
static std::string findVcVarsAll() {
    // vswhere.exe is always at this location when VS is installed
    const char* vswhere = "C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe";

    if (!fs::exists(vswhere)) {
        return "";
    }

    // Run vswhere to get the VS installation path
    std::string cmd = std::string("\"") + vswhere + "\" -latest -property installationPath 2>nul";
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }

    std::array<char, 512> buffer;
    std::string installPath;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        installPath += buffer.data();
    }
    _pclose(pipe);

    // Trim whitespace
    while (!installPath.empty() && (installPath.back() == '\n' || installPath.back() == '\r' || installPath.back() == ' ')) {
        installPath.pop_back();
    }

    if (installPath.empty()) {
        return "";
    }

    // vcvarsall.bat is in VC\Auxiliary\Build
    std::string vcvarsall = installPath + "\\VC\\Auxiliary\\Build\\vcvarsall.bat";
    if (fs::exists(vcvarsall)) {
        return vcvarsall;
    }

    return "";
}
#endif

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

bool HotReload::checkNeedsReload() {
    if (m_sourcePath.empty()) {
        return false;
    }

    // Check if file exists
    if (!fs::exists(m_sourcePath)) {
        return false;
    }

    // Check modification time
    auto modTime = getFileModTime();
    return modTime != m_lastModTime;
}

bool HotReload::reload() {
    if (m_sourcePath.empty()) {
        return false;
    }

    // Check if file exists
    if (!fs::exists(m_sourcePath)) {
        m_error = "Source file not found: " + m_sourcePath.string();
        return false;
    }

    // Update modification time
    m_lastModTime = getFileModTime();

    std::cout << "Detected change in " << m_sourcePath.filename() << ", reloading..." << std::endl;

    // Compile
    if (!compile()) {
        return false;
    }

    // Load (this unloads the old library first)
    if (!load()) {
        return false;
    }

    m_error.clear();
    m_needsSetup = true;
    return true;
}

bool HotReload::update() {
    // Legacy API - check and reload in one call
    // WARNING: Old library is unloaded before returning, so caller must
    // destroy chain operators BEFORE calling this!
    if (!checkNeedsReload()) {
        return false;
    }
    return reload();
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

    // Build library name with process ID and build number
    // Using PID ensures different vivid instances don't conflict
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
#else
    pid_t pid = getpid();
#endif
    std::string libName = "chain_" + std::to_string(pid) + "_" + std::to_string(m_buildNumber);

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
    // exe could be at:
    //   build/bin/vivid (single-config: make/ninja)
    //   build/bin/Debug/vivid.exe (multi-config: MSVC)
    // Search upwards to find the vivid root (contains core/include/vivid)
    fs::path rootDir;
    fs::path devVividInclude;
    bool isDevelopmentMode = false;

    for (int i = 2; i <= 4; ++i) {
        fs::path candidate = exeDir;
        for (int j = 0; j < i; ++j) {
            candidate = candidate / "..";
        }
        fs::path candidateInclude = candidate / "core" / "include";
        if (fs::exists(candidateInclude / "vivid")) {
            rootDir = candidate;
            devVividInclude = candidateInclude;
            isDevelopmentMode = true;
            break;
        }
    }

    if (isDevelopmentMode) {
        vividInclude = fs::canonical(devVividInclude);
        // On multi-config generators (MSVC), libs are in build/lib/Debug or build/lib/Release
        // On single-config generators, they're in build/lib
        fs::path buildDir = rootDir / "build";
        fs::path libDir = buildDir / "lib";
#ifdef _WIN32
        // Check for Debug/Release subdirectories (MSVC multi-config)
        if (fs::exists(libDir / "Debug")) {
            addonsLib = libDir / "Debug";
        } else if (fs::exists(libDir / "Release")) {
            addonsLib = libDir / "Release";
        } else {
            addonsLib = libDir;
        }
#else
        addonsLib = libDir;
#endif

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
                    // Add imgui for ImGui support in user chains
                    if (fs::exists(entry.path() / "imgui.h")) {
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

    // Add user addon paths from ~/.vivid/addons/
    auto& addonMgr = AddonManager::instance();
    std::vector<fs::path> userAddonIncludes = addonMgr.getIncludePaths();
    std::vector<fs::path> userAddonLibs = addonMgr.getLibraryPaths();

    // Build the compile command
    std::stringstream cmd;

#ifdef _WIN32
    // MSVC on Windows - need to set up the VS environment first
    std::string vcvarsall = findVcVarsAll();
    if (vcvarsall.empty()) {
        m_error = "Could not find Visual Studio installation. Please install Visual Studio with C++ workload.";
        std::cerr << m_error << std::endl;
        return false;
    }

    // Build cl command
    // Use /MDd for Debug runtime to match vivid.exe's Debug build
    // Using /MD (Release) with a Debug exe causes CRT mismatch crashes
    std::stringstream clCmd;
#ifdef _DEBUG
    clCmd << "cl /nologo /EHsc /LD /Od /MDd /std:c++17 ";
#else
    clCmd << "cl /nologo /EHsc /LD /O2 /MD /std:c++17 ";
#endif
    clCmd << "/I\"" << sourceDir.string() << "\" ";
    clCmd << "/I\"" << vividInclude.string() << "\" ";
    for (const auto& inc : depIncludes) {
        clCmd << "/I\"" << inc.string() << "\" ";
    }
    // User addon includes
    for (const auto& inc : userAddonIncludes) {
        clCmd << "/I\"" << inc.string() << "\" ";
    }
    clCmd << "/Fe\"" << m_libraryPath.string() << "\" ";
    clCmd << "\"" << m_sourcePath.string() << "\" ";
    clCmd << "/link ";

    // Link against vivid-core.lib import library (required for core symbols like Context, Chain)
    // vivid-core.lib is generated in build/lib/Debug (same as addon libs)
    fs::path vividCoreLib = addonsLib / "vivid-core.lib";
    if (fs::exists(vividCoreLib)) {
        clCmd << "\"" << vividCoreLib.string() << "\" ";
    }

    // Link against vivid.lib import library (for symbols in vivid.exe if needed)
    // vivid.lib is generated in build/lib/Debug (same as addon libs)
    fs::path vividLib = addonsLib / "vivid.lib";
    if (fs::exists(vividLib)) {
        clCmd << "\"" << vividLib.string() << "\" ";
    }

    // Link discovered addons
    for (const auto& addon : m_addonRegistry->addons()) {
        fs::path libPath = addonsLib / (addon.libraryName + ".lib");
        if (fs::exists(libPath)) {
            clCmd << "\"" << libPath.string() << "\" ";
        }
    }

    // Link user addons from ~/.vivid/addons/
    for (const auto& libDir : userAddonLibs) {
        clCmd << "/LIBPATH:\"" << libDir.string() << "\" ";
        // Find .lib files in this directory
        for (const auto& entry : fs::directory_iterator(libDir)) {
            if (entry.path().extension() == ".lib") {
                std::string filename = entry.path().filename().string();
                // Skip import libraries for dependencies
                if (filename.find("onnxruntime") != std::string::npos) continue;
                clCmd << "\"" << entry.path().string() << "\" ";
            }
        }
    }

    // Wrap in cmd /c with vcvarsall setup
    // Use x64 architecture
    cmd << "cmd /c \"\"" << vcvarsall << "\" x64 >nul 2>&1 && " << clCmd.str() << " 2>&1\"";
#else
    // Clang/GCC on Unix
    cmd << "clang++ -std=c++17 -O2 -shared -fPIC ";
    cmd << "-I\"" << sourceDir.string() << "\" ";
    cmd << "-I\"" << vividInclude.string() << "\" ";
    for (const auto& inc : depIncludes) {
        cmd << "-I\"" << inc.string() << "\" ";
    }
    // User addon includes
    for (const auto& inc : userAddonIncludes) {
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
    // User addon library paths
    for (const auto& libDir : userAddonLibs) {
        cmd << "-L\"" << libDir.string() << "\" ";
        // Find dylib files and link them
        for (const auto& entry : fs::directory_iterator(libDir)) {
            if (entry.path().extension() == ".dylib") {
                std::string filename = entry.path().filename().string();
                // Skip dependencies like ONNX Runtime
                if (filename.find("onnxruntime") != std::string::npos) continue;
                // Extract library name: libfoo.dylib -> foo
                if (filename.rfind("lib", 0) == 0) {
                    std::string libName = filename.substr(3);
                    size_t dotPos = libName.rfind('.');
                    if (dotPos != std::string::npos) {
                        libName = libName.substr(0, dotPos);
                    }
                    cmd << "-l" << libName << " ";
                }
            }
        }
        cmd << "-Wl,-rpath,\"" << libDir.string() << "\" ";
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
    // User addon library paths
    for (const auto& libDir : userAddonLibs) {
        cmd << "-L\"" << libDir.string() << "\" ";
        // Find .so files and link them
        for (const auto& entry : fs::directory_iterator(libDir)) {
            std::string filename = entry.path().filename().string();
            if (filename.find(".so") != std::string::npos) {
                // Skip dependencies like ONNX Runtime
                if (filename.find("onnxruntime") != std::string::npos) continue;
                // Extract library name: libfoo.so -> foo
                if (filename.rfind("lib", 0) == 0) {
                    std::string libName = filename.substr(3);
                    size_t dotPos = libName.find('.');
                    if (dotPos != std::string::npos) {
                        libName = libName.substr(0, dotPos);
                    }
                    cmd << "-l" << libName << " ";
                }
            }
        }
        cmd << "-Wl,-rpath,\"" << libDir.string() << "\" ";
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
        parseCompilerOutput(output);
        std::cerr << m_error << std::endl;
        return false;
    }

    // Clear any previous errors on success
    m_compileErrors.clear();

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

// Parse compiler output to extract structured errors
// Supports clang/gcc format: file:line:col: severity: message
// and MSVC format: file(line): severity code: message
void HotReload::parseCompilerOutput(const std::string& output) {
    m_compileErrors.clear();

    // Clang/GCC format: /path/file.cpp:42:10: error: message
    std::regex clangRegex(R"(([^:\s]+):(\d+):(\d+):\s*(error|warning|note):\s*(.+))");

    // MSVC format: C:\path\file.cpp(42): error C1234: message
    std::regex msvcRegex(R"(([^\(]+)\((\d+)\):\s*(error|warning)\s*\w*:\s*(.+))");

    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        std::smatch match;

        if (std::regex_search(line, match, clangRegex)) {
            CompileError err;
            err.file = match[1].str();
            err.line = std::stoi(match[2].str());
            err.column = std::stoi(match[3].str());
            err.severity = match[4].str();
            err.message = match[5].str();
            m_compileErrors.push_back(err);
        } else if (std::regex_search(line, match, msvcRegex)) {
            CompileError err;
            err.file = match[1].str();
            err.line = std::stoi(match[2].str());
            err.column = 0;  // MSVC doesn't always provide column
            err.severity = match[3].str();
            err.message = match[4].str();
            m_compileErrors.push_back(err);
        }
    }
}

// Convert a single error to JSON
std::string CompileError::toJson() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"file\":\"" << file << "\",";
    ss << "\"line\":" << line << ",";
    ss << "\"column\":" << column << ",";
    ss << "\"severity\":\"" << severity << "\",";

    // Escape quotes and backslashes in message
    std::string escapedMsg = message;
    size_t pos = 0;
    while ((pos = escapedMsg.find('\\', pos)) != std::string::npos) {
        escapedMsg.replace(pos, 1, "\\\\");
        pos += 2;
    }
    pos = 0;
    while ((pos = escapedMsg.find('"', pos)) != std::string::npos) {
        escapedMsg.replace(pos, 1, "\\\"");
        pos += 2;
    }

    ss << "\"message\":\"" << escapedMsg << "\"";
    ss << "}";
    return ss.str();
}

// Get all errors as JSON array
std::string HotReload::getErrorsJson() const {
    std::ostringstream ss;
    ss << "{\"errors\":[";
    for (size_t i = 0; i < m_compileErrors.size(); ++i) {
        if (i > 0) ss << ",";
        ss << m_compileErrors[i].toJson();
    }
    ss << "]}";
    return ss.str();
}

} // namespace vivid
