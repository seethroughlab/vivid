#include "compiler.h"
#include <iostream>
#include <array>
#include <cstdio>
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

namespace fs = std::filesystem;

namespace vivid {

// Addon configuration - maps include patterns to addon paths and setup functions
struct AddonInfo {
    std::string includePattern;  // Pattern to detect in source (e.g., "<vivid/models/")
    std::string addonPath;       // Relative path from vivid root (e.g., "addons/vivid-models")
    std::string setupFunction;   // CMake function to call (e.g., "vivid_use_models")
};

static const std::vector<AddonInfo> AVAILABLE_ADDONS = {
    {"<vivid/models/", "addons/vivid-models", "vivid_use_models"},
    {"<vivid/storage/", "addons/vivid-storage", "vivid_use_storage"},
    {"<vivid/imgui/", "addons/vivid-imgui", "vivid_use_imgui"},
    {"<vivid/nuklear/", "addons/vivid-nuklear", "vivid_use_nuklear"},
};

Compiler::Compiler(const std::string& projectPath)
    : projectPath_(projectPath) {
    // Default build directory is projectPath/build
    buildDir_ = projectPath_ + "/build";
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

    // Get vivid include directory
    std::string vividIncludeDir = getVividIncludeDir();

    std::cout << "[Compiler] Configuring " << projectPath_ << "...\n";

    // Get stb include directory (relative to vivid include dir)
    fs::path stbIncludeDir = fs::path(vividIncludeDir).parent_path() / "_deps" / "stb-src";

    // Configure with CMake - pass vivid and stb include directories
    std::string configCmd = "cmake -B \"" + buildDir_ + "\" -S \"" + cmakeSourceDir + "\" "
                           "-DCMAKE_BUILD_TYPE=Release "
                           "-DVIVID_INCLUDE_DIR=\"" + vividIncludeDir + "\" "
                           "-DSTB_INCLUDE_DIR=\"" + stbIncludeDir.string() + "\" 2>&1";

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

    // Build
    std::string buildCmd = "cmake --build \"" + buildDir_ + "\" --config Release 2>&1";

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
        buildDir_ + "/Release/operators" + libExt,
        buildDir_ + "/lib/operators" + libExt,
    };

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate)) {
            libPath = candidate;
            break;
        }
    }

    // Search recursively if not found in common locations
    // Look specifically for "operators" library to avoid picking up dependency libs
    if (libPath.empty()) {
        for (const auto& entry : fs::recursive_directory_iterator(buildDir_)) {
            if (entry.is_regular_file()) {
                std::string path = entry.path().string();
                std::string filename = entry.path().filename().string();
                // Look for liboperators or operators specifically
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

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
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

std::vector<AddonInfo> Compiler::detectAddons() const {
    std::vector<AddonInfo> detected;

    // Scan source files for addon includes
    for (const auto& entry : fs::directory_iterator(projectPath_)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".cc" && ext != ".cxx" && ext != ".h" && ext != ".hpp") continue;

        std::ifstream file(entry.path());
        if (!file.is_open()) continue;

        std::string line;
        while (std::getline(file, line)) {
            // Check for #include directives
            if (line.find("#include") == std::string::npos) continue;

            for (const auto& addon : AVAILABLE_ADDONS) {
                if (line.find(addon.includePattern) != std::string::npos) {
                    // Check if we already added this addon
                    bool alreadyAdded = false;
                    for (const auto& d : detected) {
                        if (d.addonPath == addon.addonPath) {
                            alreadyAdded = true;
                            break;
                        }
                    }
                    if (!alreadyAdded) {
                        detected.push_back(addon);
                        std::cout << "[Compiler] Detected addon: " << addon.addonPath << "\n";
                    }
                }
            }
        }
    }

    return detected;
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

bool Compiler::generateCMakeLists(std::string& generatedPath) {
    // Find source files
    auto sources = findSourceFiles();
    if (sources.empty()) {
        return false;
    }

    // Detect required addons based on includes
    auto addons = detectAddons();

    // Get absolute path to project and vivid root
    std::string absoluteProjectPath = fs::canonical(projectPath_).string();
    std::string vividRoot = getVividRootDir();

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
    cmake << "# Vivid headers (passed by runtime)\n";
    cmake << "set(VIVID_INCLUDE_DIR \"\" CACHE PATH \"Vivid include directory\")\n";
    cmake << "set(STB_INCLUDE_DIR \"\" CACHE PATH \"STB include directory\")\n\n";
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

    // Include detected addons
    if (!addons.empty()) {
        cmake << "# Vivid addons (auto-detected from includes)\n";
        for (const auto& addon : addons) {
            cmake << "include(\"" << vividRoot << "/" << addon.addonPath << "/addon.cmake\")\n";
        }
        cmake << "\n";
    }

    cmake << "# Source files (auto-detected)\n";
    cmake << "add_library(operators SHARED\n";
    cmake << sourceList;
    cmake << ")\n\n";
    cmake << "target_include_directories(operators PRIVATE\n";
    cmake << "    ${VIVID_INCLUDE_DIR}\n";
    cmake << "    ${STB_INCLUDE_DIR}\n";
    cmake << ")\n\n";
    cmake << "target_link_libraries(operators PRIVATE\n";
    cmake << "    glm::glm\n";
    cmake << ")\n\n";

    // Apply addon configurations to the operators target
    if (!addons.empty()) {
        cmake << "# Apply addon configurations\n";
        for (const auto& addon : addons) {
            cmake << addon.setupFunction << "(operators)\n";
        }
        cmake << "\n";
    }

    cmake << "set_target_properties(operators PROPERTIES\n";
    cmake << "    OUTPUT_NAME \"operators\"\n";
    cmake << "    LIBRARY_OUTPUT_DIRECTORY \"${CMAKE_BINARY_DIR}/lib\"\n";
    cmake << ")\n\n";
    cmake << "if(APPLE)\n";
    cmake << "    target_link_options(operators PRIVATE -undefined dynamic_lookup)\n";
    cmake << "endif()\n";

    // Create a subdirectory for the generated CMakeLists.txt
    // CMake expects the source directory to contain a file named CMakeLists.txt
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

} // namespace vivid
