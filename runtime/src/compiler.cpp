#include "compiler.h"
#include <iostream>
#include <array>
#include <cstdio>
#include <memory>
#include <filesystem>

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
    std::string cmakeLists = projectPath_ + "/CMakeLists.txt";
    if (!fs::exists(cmakeLists)) {
        result.errorOutput = "No CMakeLists.txt found in " + projectPath_;
        lastError_ = result.errorOutput;
        std::cerr << "[Compiler] " << result.errorOutput << "\n";
        return result;
    }

    // Create build directory if needed
    fs::create_directories(buildDir_);

    std::cout << "[Compiler] Configuring " << projectPath_ << "...\n";

    // Configure with CMake
    std::string configCmd = "cmake -B \"" + buildDir_ + "\" -S \"" + projectPath_ + "\" "
                           "-DCMAKE_BUILD_TYPE=Release 2>&1";

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

} // namespace vivid
