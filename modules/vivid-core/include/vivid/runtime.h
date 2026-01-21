#pragma once

// Vivid - Production Runtime
// Minimal runtime loop for production bundles
// NO HotReload, NO MCP, NO Visualizer - just the core rendering loop

#include <vivid/vivid.h>
#include <webgpu/webgpu.h>
#include <string>
#include <filesystem>

// Forward declaration for GLFW
struct GLFWwindow;

namespace vivid {

// Forward declarations
class Context;
class Display;

/**
 * @brief Configuration for production runtime
 *
 * Controls window size, display mode, and other runtime settings.
 * For production bundles, these come from vivid_config() exported by chain.cpp.
 */
struct RuntimeConfig {
    int windowWidth = 1280;          ///< Initial window width
    int windowHeight = 720;          ///< Initial window height
    int renderWidth = 0;             ///< Render resolution width (0 = window size)
    int renderHeight = 0;            ///< Render resolution height (0 = window size)
    bool fullscreen = false;         ///< Start in fullscreen mode
    bool resizable = true;           ///< Allow window resizing
    DisplayMode displayMode = DisplayMode::Fit;  ///< Display scaling mode
    std::string title;               ///< Window title (empty = app name)
    std::filesystem::path assetsPath; ///< Path to assets folder
};

/**
 * @brief Minimal production runtime - no dev tools
 *
 * This class provides a clean, minimal runtime loop for production bundles.
 * It contains ONLY what's needed to run a chain:
 * - WebGPU initialization
 * - Window management
 * - Main loop with setup/update calls
 * - Display rendering
 *
 * NO HotReload, NO MCP server, NO Visualizer, NO stubs.
 * Production bundles compile chain.cpp directly into the executable
 * and call vivid_setup/vivid_update directly.
 */
class Runtime {
public:
    Runtime();
    ~Runtime();

    // Non-copyable
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /**
     * @brief Initialize the runtime with configuration
     * @param config Runtime configuration
     * @return 0 on success, non-zero on error
     */
    int init(const RuntimeConfig& config);

    /**
     * @brief Run the main loop with setup/update functions
     * @param setup Function called once before main loop
     * @param update Function called every frame
     * @return Exit code (0 = success)
     *
     * This is the main entry point for production bundles:
     * @code
     * Runtime runtime;
     * runtime.init(config);
     * return runtime.run(vivid_setup, vivid_update);
     * @endcode
     */
    int run(SetupFn setup, UpdateFn update);

    /**
     * @brief Get the context (for accessing chain, etc.)
     */
    Context& context() { return *m_ctx; }

    /**
     * @brief Check if runtime is initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Shutdown the runtime and release resources
     */
    void shutdown();

private:
    // Main loop iteration - returns false when should exit
    bool mainLoopIteration(SetupFn setup, UpdateFn update);

    // Initialize WebGPU
    bool initWebGPU();

    // Initialize window
    bool initWindow(const RuntimeConfig& config);

    // Configure surface
    void configureSurface();

    // WebGPU resources
    WGPUInstance m_instance = nullptr;
    WGPUAdapter m_adapter = nullptr;
    WGPUSurface m_surface = nullptr;
    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;
    WGPUTextureFormat m_surfaceFormat = WGPUTextureFormat_BGRA8Unorm;

    // Window
    GLFWwindow* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
    bool m_isFullscreen = false;
    int m_windowedX = 0;
    int m_windowedY = 0;
    int m_windowedWidth = 1280;
    int m_windowedHeight = 720;

    // Core runtime objects
    Context* m_ctx = nullptr;
    Display* m_display = nullptr;

    // State
    bool m_initialized = false;
    bool m_chainInitialized = false;
    RuntimeConfig m_config;
};

} // namespace vivid
