#pragma once

/**
 * @file window_manager.h
 * @brief Multi-window and span display management
 *
 * WindowManager handles:
 * - Primary window with ImGui overlay
 * - Secondary output windows (projectors, LED panels)
 * - Span mode across multiple monitors
 * - Per-window content routing
 */

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <functional>

namespace vivid {

class Chain;

/**
 * @brief Information about a connected monitor
 */
struct MonitorInfo {
    int index = 0;              ///< Monitor index (0 = primary)
    int x = 0, y = 0;           ///< Position in virtual desktop
    int width = 0, height = 0;  ///< Resolution
    int refreshRate = 60;       ///< Refresh rate in Hz
    std::string name;           ///< Monitor name from GLFW
};

/**
 * @brief Configuration for an output window
 */
struct OutputWindow {
    int handle = -1;                        ///< Unique handle for this window
    GLFWwindow* window = nullptr;           ///< GLFW window handle
    WGPUSurface surface = nullptr;          ///< WebGPU surface for this window
    WGPUSurfaceConfiguration surfaceConfig; ///< Surface configuration
    int width = 0, height = 0;              ///< Current window size
    int posX = 0, posY = 0;                 ///< Window position
    int monitorIndex = -1;                  ///< Monitor this window is on (-1 = unknown)
    bool borderless = false;                ///< No window decorations
    bool fullscreen = false;                ///< Fullscreen mode
    bool active = true;                     ///< Window is active/visible
    bool isPrimary = false;                 ///< Primary window (has ImGui)
    bool adopted = false;                   ///< Window was adopted (don't destroy on cleanup)

    // Content routing
    std::string sourceOperator;             ///< Operator to display (empty = chain output)

    // Span region (normalized 0-1)
    glm::vec4 sourceRegion = {0, 0, 1, 1};  ///< x, y, width, height in source texture
};

/**
 * @brief Manages multiple windows and WebGPU surfaces
 *
 * WindowManager abstracts window creation, surface management, and
 * multi-display output for installations and performances.
 *
 * @par Example - Secondary Output
 * @code
 * WindowManager wm(instance, device, queue);
 * wm.createPrimaryWindow(1280, 720, "Vivid");
 *
 * // Create fullscreen output on second monitor
 * int output = wm.createOutputWindow(1, true);
 * wm.setWindowFullscreen(output, true, 1);
 * @endcode
 *
 * @par Example - Span Mode
 * @code
 * WindowManager wm(instance, device, queue);
 * wm.createPrimaryWindow(1280, 720, "Vivid");
 * wm.enableSpanMode(2, 1);  // 2 monitors side-by-side
 * wm.autoConfigureSpan();   // Auto-detect and position
 * @endcode
 */
class WindowManager {
public:
    /**
     * @brief Construct WindowManager
     * @param instance WebGPU instance
     * @param adapter WebGPU adapter (for surface capability queries)
     * @param device WebGPU device
     * @param queue WebGPU queue
     */
    WindowManager(WGPUInstance instance, WGPUAdapter adapter, WGPUDevice device, WGPUQueue queue);

    /**
     * @brief Destructor - cleans up all windows and surfaces
     */
    ~WindowManager();

    // -------------------------------------------------------------------------
    /// @name Primary Window
    /// @{

    /**
     * @brief Create the primary window (with ImGui support)
     * @param width Initial width
     * @param height Initial height
     * @param title Window title
     * @return True if successful
     */
    bool createPrimaryWindow(int width, int height, const char* title);

    /**
     * @brief Adopt an existing window as primary (for integration with main.cpp)
     * @param window Existing GLFW window
     * @param surface Existing WebGPU surface
     * @param width Window width
     * @param height Window height
     * @return True if successful
     *
     * Use this when main.cpp creates the window/surface for adapter request.
     * The WindowManager will NOT destroy these resources on cleanup.
     */
    bool adoptPrimaryWindow(GLFWwindow* window, WGPUSurface surface, int width, int height);

    /// @brief Get the primary GLFW window
    GLFWwindow* primaryWindow() const;

    /// @brief Get the primary window's surface
    WGPUSurface primarySurface() const;

    /// @brief Get the primary window handle (always 0)
    int primaryHandle() const { return 0; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Secondary Windows
    /// @{

    /**
     * @brief Create a secondary output window
     * @param monitorIndex Monitor to create on (-1 = primary monitor)
     * @param borderless Remove window decorations
     * @return Window handle, or -1 on failure
     */
    int createOutputWindow(int monitorIndex = -1, bool borderless = true);

    /**
     * @brief Destroy an output window
     * @param handle Window handle from createOutputWindow
     */
    void destroyOutputWindow(int handle);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Window Configuration
    /// @{

    /**
     * @brief Set window position
     * @param handle Window handle
     * @param x X position in screen coordinates
     * @param y Y position in screen coordinates
     */
    void setWindowPos(int handle, int x, int y);

    /**
     * @brief Set window size
     * @param handle Window handle
     * @param w Width in pixels
     * @param h Height in pixels
     */
    void setWindowSize(int handle, int w, int h);

    /**
     * @brief Set window fullscreen mode
     * @param handle Window handle
     * @param fullscreen Enable fullscreen
     * @param monitorIndex Target monitor (-1 = current)
     */
    void setWindowFullscreen(int handle, bool fullscreen, int monitorIndex = -1);

    /**
     * @brief Set window borderless mode
     * @param handle Window handle
     * @param borderless Remove decorations
     */
    void setWindowBorderless(int handle, bool borderless);

    /**
     * @brief Set which operator this window displays
     * @param handle Window handle
     * @param operatorName Operator name, or empty for chain output
     */
    void setWindowSource(int handle, const std::string& operatorName);

    /**
     * @brief Set the source region for this window (for span mode)
     * @param handle Window handle
     * @param x X offset (normalized 0-1)
     * @param y Y offset (normalized 0-1)
     * @param w Width (normalized 0-1)
     * @param h Height (normalized 0-1)
     */
    void setWindowRegion(int handle, float x, float y, float w, float h);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Span Mode
    /// @{

    /**
     * @brief Enable span mode across multiple monitors
     * @param columns Number of horizontal monitors
     * @param rows Number of vertical monitors
     *
     * In span mode, the chain renders at the combined resolution
     * and each monitor shows a portion of the output.
     */
    void enableSpanMode(int columns, int rows);

    /**
     * @brief Disable span mode
     */
    void disableSpanMode();

    /**
     * @brief Check if span mode is active
     */
    bool isSpanMode() const { return m_spanMode; }

    /**
     * @brief Get span grid dimensions
     * @return (columns, rows)
     */
    glm::ivec2 spanGrid() const { return {m_spanColumns, m_spanRows}; }

    /**
     * @brief Set bezel gap compensation
     * @param hPixels Horizontal gap in pixels
     * @param vPixels Vertical gap in pixels
     */
    void setBezelGap(int hPixels, int vPixels);

    /**
     * @brief Auto-configure span based on detected monitors
     *
     * Detects connected monitors and creates/positions windows
     * to span across them seamlessly.
     */
    void autoConfigureSpan();

    /**
     * @brief Get total span resolution
     * @return Combined resolution across all monitors
     */
    glm::ivec2 spanResolution() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Monitor Detection
    /// @{

    /**
     * @brief Get information about all connected monitors
     */
    std::vector<MonitorInfo> detectMonitors() const;

    /**
     * @brief Get number of connected monitors
     */
    int monitorCount() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Render Loop Integration
    /// @{

    /**
     * @brief Poll events for all windows
     *
     * Call once per frame before processing.
     */
    void pollEvents();

    /**
     * @brief Check if any window requested close
     */
    bool shouldClose() const;

    /**
     * @brief Present chain output to all windows
     * @param chain The chain (for operator texture lookup)
     * @param defaultOutput Default output texture (chain output)
     *
     * Each window receives either the default output or its
     * configured operator's output, with region sampling applied.
     */
    void presentAll(Chain* chain, WGPUTextureView defaultOutput);

    /**
     * @brief Configure surface for a window
     * @param handle Window handle
     */
    void configureSurface(int handle);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Queries
    /// @{

    /// @brief Get total number of windows (including primary)
    int windowCount() const { return static_cast<int>(m_windows.size()); }

    /// @brief Get window by handle
    const OutputWindow* window(int handle) const;

    /// @brief Get mutable window by handle
    OutputWindow* windowMutable(int handle);

    /// @}

private:
    WGPUInstance m_instance;
    WGPUAdapter m_adapter;
    WGPUDevice m_device;
    WGPUQueue m_queue;

    std::vector<OutputWindow> m_windows;
    int m_nextHandle = 0;

    // Span configuration
    bool m_spanMode = false;
    int m_spanColumns = 1;
    int m_spanRows = 1;
    int m_bezelGapH = 0;
    int m_bezelGapV = 0;

    // Blit resources (shared across all windows)
    WGPURenderPipeline m_blitPipeline = nullptr;
    WGPUBindGroupLayout m_blitBindGroupLayout = nullptr;
    WGPUSampler m_blitSampler = nullptr;
    WGPUBuffer m_regionUniformBuffer = nullptr;

    // Internal helpers
    void createBlitResources();
    void destroyBlitResources();
    void createSurface(OutputWindow& win);
    void destroySurface(OutputWindow& win);
    void blitToWindow(OutputWindow& win, WGPUTextureView source);
    void updateSpanRegions();
    OutputWindow* findWindow(int handle);
};

} // namespace vivid
