// Vivid Application
// Main application class handling window, WebGPU, and main loop

#pragma once

#include <vivid/video_exporter.h>
#include <string>
#include <filesystem>

namespace vivid {

// Configuration passed from command-line arguments
struct AppConfig {
    std::filesystem::path projectPath;
    std::string snapshotPath;
    int snapshotFrame = 5;
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

    // Frame limit
    int maxFrames = 0;  // 0 = unlimited
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
