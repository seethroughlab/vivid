#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace vivid {

struct CompileResult {
    bool success = false;
    std::string libraryPath;     // Path to compiled .so/.dylib/.dll
    std::string errorOutput;     // stderr if compile failed
    std::string buildOutput;     // stdout from build
};

// Progress callback: phase (0=configure, 1=build), percentage (0-100), current file
using ProgressCallback = std::function<void(int phase, int percent, const std::string& file)>;

// Forward declarations
class AddonRegistry;

class Compiler {
public:
    // projectPath: path to the user's vivid project directory
    explicit Compiler(const std::string& projectPath);
    ~Compiler();

    // Compile the project's operator library
    // Returns the path to the compiled library on success
    CompileResult compile();

    // Set progress callback for build progress updates
    void setProgressCallback(ProgressCallback callback) { progressCallback_ = callback; }

    // Get/set build directory (default: projectPath/build)
    const std::string& buildDirectory() const { return buildDir_; }
    void setBuildDirectory(const std::string& dir) { buildDir_ = dir; }

    // Get last error message
    const std::string& lastError() const { return lastError_; }

    // Access the addon registry
    AddonRegistry& addonRegistry();

private:
    // Run a shell command and capture output, with optional progress streaming
    bool runCommand(const std::string& command, std::string& output, std::string& error,
                    int phase = -1);  // phase: 0=configure, 1=build, -1=no progress

    // Find source files (.cpp, .cc, .cxx) in project directory
    std::vector<std::string> findSourceFiles() const;

    // Generate CMakeLists.txt for project, returns path to generated file
    bool generateCMakeLists(std::string& generatedPath);

    // Locate vivid headers relative to project
    std::string getVividIncludeDir() const;

    // Locate vivid root directory (contains addons/)
    std::string getVividRootDir() const;

    // Get path to addons directory
    std::string getAddonsDir() const;

    std::string projectPath_;
    std::string buildDir_;
    std::string lastError_;
    ProgressCallback progressCallback_;
    std::unique_ptr<AddonRegistry> addonRegistry_;
};

} // namespace vivid
