#pragma once
#include <string>

namespace vivid {

struct CompileResult {
    bool success = false;
    std::string libraryPath;     // Path to compiled .so/.dylib/.dll
    std::string errorOutput;     // stderr if compile failed
    std::string buildOutput;     // stdout from build
};

class Compiler {
public:
    // projectPath: path to the user's vivid project directory
    explicit Compiler(const std::string& projectPath);

    // Compile the project's operator library
    // Returns the path to the compiled library on success
    CompileResult compile();

    // Get/set build directory (default: projectPath/build)
    const std::string& buildDirectory() const { return buildDir_; }
    void setBuildDirectory(const std::string& dir) { buildDir_ = dir; }

    // Get last error message
    const std::string& lastError() const { return lastError_; }

private:
    // Run a shell command and capture output
    bool runCommand(const std::string& command, std::string& output, std::string& error);

    std::string projectPath_;
    std::string buildDir_;
    std::string lastError_;
};

} // namespace vivid
