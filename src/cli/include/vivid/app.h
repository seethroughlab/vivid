// Vivid Application
// Main application class handling window, WebGPU, and main loop

#pragma once

#include <vivid/video_exporter.h>
#include <string>
#include <filesystem>
#include <vector>
#include <set>

namespace vivid {

// Configuration passed from command-line arguments
struct AppConfig {
    std::filesystem::path projectPath;
    std::string snapshotPath;
    std::set<int> snapshotFrames;  // Frames to capture (empty = frame 5 only)
    bool headless = false;
    int windowWidth = 1280;
    int windowHeight = 720;
    int renderWidth = 0;   // 0 = use window size
    int renderHeight = 0;
    bool startFullscreen = false;

    // Video recording
    std::string recordPath;
    float recordFps = 60.0f;
    float recordDuration = 0.0f;  // 0 = unlimited
    bool recordAudio = false;
    ExportCodec recordCodec = ExportCodec::H264;

    // Audio snapshot
    std::string audioSnapshotPath;
    float audioSnapshotDuration = 1.0f;  // seconds

    // Frame limit
    int maxFrames = 0;  // 0 = unlimited

    // Start with UI visible (includes chain visualizer and IDE panel)
    bool showUI = false;

    // Check mode (vivid check): run assertions and exit
    bool checkMode = false;
    std::string assertionPath;   // path to vivid-assertions.json
    bool verboseCheck = false;   // print each assertion line
    float checkDuration = 0.0f;  // run chain for N seconds before evaluating (0 = use checkFrame)

    // Inspect mode (vivid inspect): dump inspection JSON and exit
    bool inspectMode = false;
    std::string inspectOutDir;   // optional output directory for inspect
    bool inspectPerOperator = false;  // include per-operator texture analysis
    float inspectDuration = 0.0f;    // multi-sample: duration in seconds (0 = single-shot)
    int inspectSamples = 1;          // multi-sample: number of samples to collect

    // Build mode (vivid build): compile chain and report structured errors
    bool buildMode = false;

    // Params mode (vivid params): list all tweakable parameters as JSON
    bool paramsMode = false;

    // Graph mode (vivid graph): dump chain topology as JSON
    bool graphMode = false;

    // Shared by check/inspect: which frame to evaluate at
    int checkFrame = -1;         // -1 = use assertion file's value or default 10

    // Export mode (vivid export): headless A/V export with optional script
    bool exportMode = false;
    std::string exportOutput;        // output video file path
    std::string exportScript;        // playback script JSON (optional)
    float exportDuration = 0.0f;     // duration in seconds (overrides script)
    float exportFps = 60.0f;         // frame rate (overrides script)
    bool exportAudio = false;        // include audio track
    ExportCodec exportCodec = ExportCodec::H264;
    bool exportQuiet = false;        // suppress progress output

    // Exit on any compile error (for agent/CI workflows)
    bool exitOnError = false;
};

// Main application class
// Owns window, WebGPU context, and runs the main loop
class Application {
public:
    Application() = default;
    ~Application();

    // Initialize the application with given config
    // Returns 0 on success, non-zero on error
    int init(const AppConfig& config);

    // Run the main loop
    // Returns exit code (0 = success)
    int run();

    // Cleanup (called by destructor, can be called explicitly)
    void shutdown();

private:
    struct Impl;
    Impl* m_impl = nullptr;
    bool m_initialized = false;
};

} // namespace vivid
