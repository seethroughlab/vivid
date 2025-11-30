#include "compiler.h"
#include <iostream>
#include <array>
#include <cstdio>
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace fs = std::filesystem;

namespace vivid {

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

    // Configure with CMake - pass vivid include directory
    std::string configCmd = "cmake -B \"" + buildDir_ + "\" -S \"" + cmakeSourceDir + "\" "
                           "-DCMAKE_BUILD_TYPE=Release "
                           "-DVIVID_INCLUDE_DIR=\"" + vividIncludeDir + "\" 2>&1";

    std::string configOutput, configError;
    if (!runCommand(configCmd, configOutput, configError)) {
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
    if (!runCommand(buildCmd, buildOutput, buildError)) {
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

    // Search for library in build directory
    std::string libPath;
    for (const auto& entry : fs::recursive_directory_iterator(buildDir_)) {
        if (entry.is_regular_file()) {
            std::string path = entry.path().string();
            if (path.find(libExt) != std::string::npos &&
                path.find("lib") != std::string::npos) {
                libPath = path;
                break;
            }
        }
    }

    if (libPath.empty()) {
        // Try common locations
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

bool Compiler::runCommand(const std::string& command, std::string& output, std::string& error) {
    std::array<char, 4096> buffer;
    output.clear();
    error.clear();

    // Use popen to run command and capture output
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) {
        error = "Failed to run command: " + command;
        return false;
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        output += buffer.data();
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

    // Get absolute path to project
    std::string absoluteProjectPath = fs::canonical(projectPath_).string();

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
    cmake << "set(VIVID_INCLUDE_DIR \"\" CACHE PATH \"Vivid include directory\")\n\n";
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
    cmake << ")\n\n";
    cmake << "target_link_libraries(operators PRIVATE\n";
    cmake << "    glm::glm\n";
    cmake << ")\n\n";
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
