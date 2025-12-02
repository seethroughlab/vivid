#include "compiler.h"
#include "addon_registry.h"
#include <iostream>
#include <array>
#include <cstdio>
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

// Windows compatibility for popen/pclose
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#define popen _popen
#define pclose _pclose
#endif

namespace fs = std::filesystem;

namespace vivid {

// Find cmake executable - needed on Windows where it may not be in PATH
static std::string findCMake() {
#ifdef _WIN32
    // Common Visual Studio cmake locations
    std::vector<std::string> candidates = {
        "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe",
        "C:/Program Files/Microsoft Visual Studio/2022/Professional/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe",
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe",
        "C:/Program Files/CMake/bin/cmake.exe",
        "C:/Program Files (x86)/CMake/bin/cmake.exe",
    };

    for (const auto& path : candidates) {
        if (fs::exists(path)) {
            // Convert to short path to avoid spaces issues with popen/cmd.exe
            char shortPath[MAX_PATH];
            DWORD len = GetShortPathNameA(path.c_str(), shortPath, MAX_PATH);
            if (len > 0 && len < MAX_PATH) {
                return std::string(shortPath);
            }
            // Fallback: wrap in quotes (may not work with all popen implementations)
            return "\"" + path + "\"";
        }
    }
#endif
    // Fall back to assuming cmake is in PATH
    return "cmake";
}

// Convert Windows backslashes to forward slashes for CMake compatibility
static std::string toCMakePath(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    return result;
}

Compiler::Compiler(const std::string& projectPath)
    : projectPath_(projectPath),
      addonRegistry_(std::make_unique<AddonRegistry>()) {
    // Default build directory is projectPath/build
    buildDir_ = projectPath_ + "/build";

    // Load addon registry from build/addons/meta
    std::string addonsMetaDir = getAddonsDir() + "/meta";
    if (fs::exists(addonsMetaDir)) {
        addonRegistry_->loadFromDirectory(addonsMetaDir);
        addonRegistry_->setAddonsBasePath(getAddonsDir());
    }
}

Compiler::~Compiler() = default;

AddonRegistry& Compiler::addonRegistry() {
    return *addonRegistry_;
}

std::string Compiler::getAddonsDir() const {
    // Addons are in build/addons relative to vivid root
    fs::path vividInclude = getVividIncludeDir();
    fs::path addonsDir = fs::path(vividInclude).parent_path() / "addons";
    return addonsDir.string();
}

CompileResult Compiler::compile() {
    CompileResult result;

    // Check if project has a CMakeLists.txt
    std::string userCMakeLists = projectPath_ + "/CMakeLists.txt";
    std::string cmakeSourceDir;

    if (fs::exists(userCMakeLists)) {
        // User has their own CMakeLists.txt - use it
        cmakeSourceDir = projectPath_;
        std::cout << "[Compiler] Using user CMakeLists.txt\n";
    } else {
        // No CMakeLists.txt - auto-generate one
        std::string generatedPath;
        if (!generateCMakeLists(generatedPath)) {
            result.errorOutput = "Failed to generate CMakeLists.txt - no .cpp files found in " + projectPath_;
            lastError_ = result.errorOutput;
            std::cerr << "[Compiler] " << result.errorOutput << "\n";
            return result;
        }
        cmakeSourceDir = fs::path(generatedPath).parent_path().string();

        // Clear CMake cache if it exists from a different source directory
        // This handles the case where project previously had CMakeLists.txt
        std::string cacheFile = buildDir_ + "/CMakeCache.txt";
        if (fs::exists(cacheFile)) {
            fs::remove(cacheFile);
        }

        std::cout << "[Compiler] Auto-generated CMakeLists.txt\n";
    }

    // Create build directory if needed
    fs::create_directories(buildDir_);

    // Get vivid include directory (use forward slashes for CMake)
    std::string vividIncludeDir = toCMakePath(getVividIncludeDir());

    std::cout << "[Compiler] Configuring " << projectPath_ << "...\n";

    // Get stb include directory (relative to vivid include dir)
    fs::path stbIncludeDir = fs::path(vividIncludeDir).parent_path() / "_deps" / "stb-src";
    std::string stbIncludeDirStr = toCMakePath(stbIncludeDir.string());

    // Get addons directory
    std::string addonsDir = toCMakePath(getAddonsDir());

    // Configure with CMake - pass vivid, stb, and addons include directories
    std::string cmake = findCMake();
#ifdef _WIN32
    std::string buildTypeArg = "";  // MSVC uses --config at build time
#else
    std::string buildTypeArg = "-DCMAKE_BUILD_TYPE=Release ";
#endif
    std::string configCmd = cmake + " -B \"" + toCMakePath(buildDir_) + "\" -S \"" + toCMakePath(cmakeSourceDir) + "\" "
                           + buildTypeArg +
                           "-DVIVID_INCLUDE_DIR=\"" + vividIncludeDir + "\" "
                           "-DSTB_INCLUDE_DIR=\"" + stbIncludeDirStr + "\" "
                           "-DVIVID_ADDONS_DIR=\"" + addonsDir + "\"";

#ifdef _WIN32
    // On Windows, pass the vivid.lib import library so DLLs can link against the exe's symbols
    fs::path vividLibPath = fs::path(vividIncludeDir).parent_path() / "runtime" / "Debug" / "vivid.lib";
    if (!fs::exists(vividLibPath)) {
        // Try Release build
        vividLibPath = fs::path(vividIncludeDir).parent_path() / "runtime" / "Release" / "vivid.lib";
    }
    if (fs::exists(vividLibPath)) {
        configCmd += " -DVIVID_LIBRARY=\"" + toCMakePath(vividLibPath.string()) + "\"";
    }
#endif

    configCmd += " 2>&1";

    std::string configOutput, configError;
    // Phase 0 = configure
    if (!runCommand(configCmd, configOutput, configError, progressCallback_ ? 0 : -1)) {
        result.errorOutput = "CMake configure failed:\n" + configOutput;
        lastError_ = result.errorOutput;
        std::cerr << "[Compiler] " << result.errorOutput << "\n";
        return result;
    }
    result.buildOutput += configOutput;

    std::cout << "[Compiler] Building...\n";

    // Build - use Debug config on Windows to match vivid.exe Debug build
#ifdef _WIN32
    std::string buildConfig = "Debug";
#else
    std::string buildConfig = "Release";
#endif
    std::string buildCmd = cmake + " --build \"" + buildDir_ + "\" --config " + buildConfig + " 2>&1";

    std::string buildOutput, buildError;
    // Phase 1 = build
    if (!runCommand(buildCmd, buildOutput, buildError, progressCallback_ ? 1 : -1)) {
        result.errorOutput = "CMake build failed:\n" + buildOutput;
        lastError_ = result.errorOutput;
        std::cerr << "[Compiler] " << result.errorOutput << "\n";
        return result;
    }
    result.buildOutput += buildOutput;

    // Find the built library
    // Look for .dylib on macOS, .so on Linux, .dll on Windows
#if defined(__APPLE__)
    std::string libExt = ".dylib";
#elif defined(_WIN32)
    std::string libExt = ".dll";
#else
    std::string libExt = ".so";
#endif

    // First try common locations for the operators library
    std::string libPath;
    std::vector<std::string> candidates = {
        buildDir_ + "/lib/liboperators" + libExt,
        buildDir_ + "/liboperators" + libExt,
        buildDir_ + "/operators" + libExt,
        buildDir_ + "/" + buildConfig + "/operators" + libExt,  // Windows: Debug or Release folder
        buildDir_ + "/Release/operators" + libExt,
        buildDir_ + "/Debug/operators" + libExt,
        buildDir_ + "/lib/operators" + libExt,
    };

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) {
            libPath = candidate;
            break;
        }
    }

    // Search recursively if not found in common locations
    if (libPath.empty()) {
        for (const auto& entry : fs::recursive_directory_iterator(buildDir_)) {
            if (entry.is_regular_file()) {
                std::string path = entry.path().string();
                std::string filename = entry.path().filename().string();
                if (path.find(libExt) != std::string::npos &&
                    filename.find("operators") != std::string::npos) {
                    libPath = path;
                    break;
                }
            }
        }
    }

    if (libPath.empty()) {
        result.errorOutput = "Could not find compiled library in " + buildDir_;
        lastError_ = result.errorOutput;
        std::cerr << "[Compiler] " << result.errorOutput << "\n";
        return result;
    }

    result.success = true;
    result.libraryPath = libPath;
    lastError_.clear();

    std::cout << "[Compiler] Build successful: " << libPath << "\n";
    return result;
}

// Helper to render a progress bar in terminal
static void renderProgressBar(int percent, const std::string& phase, const std::string& file) {
    const int barWidth = 30;
    int filled = (percent * barWidth) / 100;

    // Build progress bar string
    std::string bar = "[";
    for (int i = 0; i < barWidth; ++i) {
        if (i < filled) bar += "█";
        else bar += "░";
    }
    bar += "]";

    // Clear line and print progress
    std::cout << "\r\033[K";  // Clear line
    std::cout << "[Compiler] " << phase << " " << bar << " " << percent << "%";
    if (!file.empty()) {
        std::cout << " - " << file;
    }
    std::cout << std::flush;
}

bool Compiler::runCommand(const std::string& command, std::string& output, std::string& error,
                          int phase) {
    std::array<char, 4096> buffer;
    output.clear();
    error.clear();

    // Use popen to run command and capture output
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        error = "Failed to run command: " + command;
        return false;
    }

    // Regex to parse CMake progress: [ XX%] or [XXX%]
    std::regex progressRegex(R"(\[\s*(\d+)%\])");
    // Regex to extract filename from "Building CXX object ... /file.cpp.o"
    std::regex buildingRegex(R"(Building (?:CXX|C) object .*/([^/]+\.(?:cpp|cc|cxx|c))\.o)");

    std::string phaseName = (phase == 0) ? "Configuring" : "Building";
    std::string currentLine;
    int lastPercent = -1;
    std::string currentFile;

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        output += buffer.data();
        currentLine = buffer.data();

        // Only parse progress if we have a callback or phase is specified
        if (phase >= 0 && progressCallback_) {
            std::smatch match;

            // Try to extract percentage
            if (std::regex_search(currentLine, match, progressRegex)) {
                int percent = std::stoi(match[1].str());

                // Try to extract filename from build lines
                std::smatch fileMatch;
                if (std::regex_search(currentLine, fileMatch, buildingRegex)) {
                    currentFile = fileMatch[1].str();
                }

                // Only update if percentage changed
                if (percent != lastPercent) {
                    lastPercent = percent;
                    progressCallback_(phase, percent, currentFile);
                    renderProgressBar(percent, phaseName, currentFile);
                }
            }
        }
    }

    // Clear progress line when done
    if (phase >= 0 && lastPercent >= 0) {
        std::cout << "\r\033[K" << std::flush;  // Clear the progress line
    }

    // Get exit status
    int status = pclose(pipe.release());

#if defined(_WIN32)
    bool success = (status == 0);
#else
    bool success = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif

    return success;
}

std::vector<std::string> Compiler::findSourceFiles() const {
    std::vector<std::string> sources;
    for (const auto& entry : fs::directory_iterator(projectPath_)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
                sources.push_back(entry.path().filename().string());
            }
        }
    }
    std::sort(sources.begin(), sources.end());
    return sources;
}

bool Compiler::generateCMakeLists(std::string& generatedPath) {
    // Find source files
    auto sources = findSourceFiles();
    if (sources.empty()) {
        return false;
    }

    // Detect required addons using the registry
    auto requiredAddons = addonRegistry_->scanSourceForAddons(projectPath_);

    // Get absolute path to project (use forward slashes for CMake)
    std::string absoluteProjectPath = toCMakePath(fs::canonical(projectPath_).string());

    // Determine project name from folder
    std::string projectName = fs::path(projectPath_).filename().string();
    // Sanitize for CMake (replace dashes/spaces with underscores)
    for (char& c : projectName) {
        if (c == '-' || c == ' ') c = '_';
    }

    // Build source file list string with absolute paths
    std::string sourceList;
    for (const auto& src : sources) {
        sourceList += "    \"" + absoluteProjectPath + "/" + src + "\"\n";
    }

    // Generate CMakeLists.txt content
    std::ostringstream cmake;
    cmake << "# Auto-generated by Vivid Runtime - DO NOT EDIT\n";
    cmake << "# Place your own CMakeLists.txt in the project root to override\n\n";
    cmake << "cmake_minimum_required(VERSION 3.20)\n";
    cmake << "project(" << projectName << "_operators)\n\n";
    cmake << "set(CMAKE_CXX_STANDARD 20)\n";
    cmake << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n";
    cmake << "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n\n";
    cmake << "# Vivid headers and library (passed by runtime)\n";
    cmake << "set(VIVID_INCLUDE_DIR \"\" CACHE PATH \"Vivid include directory\")\n";
    cmake << "set(STB_INCLUDE_DIR \"\" CACHE PATH \"STB include directory\")\n";
    cmake << "set(VIVID_ADDONS_DIR \"\" CACHE PATH \"Vivid addons directory\")\n";
    cmake << "set(VIVID_LIBRARY \"\" CACHE FILEPATH \"Vivid import library (Windows only)\")\n\n";
    cmake << "# GLM for math\n";
    cmake << "find_package(glm CONFIG QUIET)\n";
    cmake << "if(NOT glm_FOUND)\n";
    cmake << "    include(FetchContent)\n";
    cmake << "    FetchContent_Declare(\n";
    cmake << "        glm\n";
    cmake << "        GIT_REPOSITORY https://github.com/g-truc/glm.git\n";
    cmake << "        GIT_TAG 1.0.1\n";
    cmake << "    )\n";
    cmake << "    FetchContent_MakeAvailable(glm)\n";
    cmake << "endif()\n\n";

    cmake << "# Source files (auto-detected)\n";
    cmake << "add_library(operators SHARED\n";
    cmake << sourceList;
    cmake << ")\n\n";
    cmake << "target_include_directories(operators PRIVATE\n";
    cmake << "    ${VIVID_INCLUDE_DIR}\n";
    cmake << "    ${STB_INCLUDE_DIR}\n";
    cmake << "    ${VIVID_ADDONS_DIR}/include\n";
    cmake << ")\n\n";
    cmake << "target_link_libraries(operators PRIVATE\n";
    cmake << "    glm::glm\n";
    cmake << ")\n\n";
    cmake << "# On Windows, link against vivid.lib to import symbols from the exe\n";
    cmake << "if(WIN32 AND VIVID_LIBRARY)\n";
    cmake << "    target_link_libraries(operators PRIVATE ${VIVID_LIBRARY})\n";
    cmake << "endif()\n\n";

    // Add pre-built addon libraries
    if (!requiredAddons.empty()) {
        cmake << "# === AUTO-DETECTED ADDONS ===\n";
        cmake << "# Linking against pre-built static libraries for fast hot-reload\n\n";

        for (const auto& addonName : requiredAddons) {
            const AddonInfo* addon = addonRegistry_->getAddon(addonName);
            if (!addon) continue;

            cmake << "# Addon: " << addonName << " - " << addon->description << "\n";
            cmake << "target_link_libraries(operators PRIVATE\n";

            // Static libraries
            for (const auto& lib : addon->staticLibs) {
                cmake << "    \"${VIVID_ADDONS_DIR}/lib/" << lib << "\"\n";
            }

            // System libraries
            for (const auto& lib : addon->systemLibs) {
                cmake << "    " << lib << "\n";
            }

            cmake << ")\n";

            // macOS frameworks
            if (!addon->frameworks.empty()) {
                cmake << "if(APPLE)\n";
                cmake << "    target_link_libraries(operators PRIVATE\n";
                for (const auto& fw : addon->frameworks) {
                    cmake << "        \"-framework " << fw << "\"\n";
                }
                cmake << "    )\n";
                cmake << "endif()\n";
            }

            // Copy runtime DLLs (Windows)
            for (const auto& dll : addon->runtimeDlls) {
                cmake << "# Copy " << dll << " to output directory\n";
                cmake << "add_custom_command(TARGET operators POST_BUILD\n";
                cmake << "    COMMAND ${CMAKE_COMMAND} -E copy_if_different\n";
                cmake << "        \"${VIVID_ADDONS_DIR}/lib/" << dll << "\"\n";
                cmake << "        \"$<TARGET_FILE_DIR:operators>\"\n";
                cmake << "    COMMENT \"Copying " << dll << "\"\n";
                cmake << ")\n";
            }

            cmake << "\n";
        }

        cmake << "# === END ADDONS ===\n\n";
    }

    cmake << "set_target_properties(operators PROPERTIES\n";
    cmake << "    OUTPUT_NAME \"operators\"\n";
    cmake << "    LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib\"\n";
    cmake << ")\n\n";
    cmake << "if(APPLE)\n";
    cmake << "    target_link_options(operators PRIVATE -undefined dynamic_lookup)\n";
    cmake << "endif()\n";

    // Create a subdirectory for the generated CMakeLists.txt
    std::string generatedDir = buildDir_ + "/_generated";
    fs::create_directories(generatedDir);
    generatedPath = generatedDir + "/CMakeLists.txt";

    std::ofstream file(generatedPath);
    if (!file.is_open()) {
        return false;
    }
    file << cmake.str();
    file.close();

    return true;
}

std::string Compiler::getVividIncludeDir() const {
    // Start from project path, go up to find build/include
    fs::path projectDir(projectPath_);

    // Check common locations relative to project
    std::vector<fs::path> candidates = {
        projectDir / ".." / ".." / "build" / "include",  // examples/foo -> build/include
        projectDir / ".." / "build" / "include",
        projectDir / ".." / "include",
    };

    for (const auto& candidate : candidates) {
        fs::path vividHeader = candidate / "vivid" / "vivid.h";
        if (fs::exists(vividHeader)) {
            return fs::canonical(candidate).string();
        }
    }

    // Fallback to the CMakeLists.txt pattern (relative path)
    return (projectDir / ".." / ".." / "build" / "include").string();
}

std::string Compiler::getVividRootDir() const {
    // Start from project path, go up to find the vivid root (contains addons/)
    fs::path projectDir(projectPath_);

    std::vector<fs::path> candidates = {
        projectDir / ".." / "..",  // examples/foo -> vivid root
        projectDir / "..",
        projectDir / ".." / ".." / "..",
    };

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate / "addons")) {
            return fs::canonical(candidate).string();
        }
    }

    // Fallback
    return (projectDir / ".." / "..").string();
}

} // namespace vivid
