#pragma once

/**
 * @file devtools.h
 * @brief Main DevTools orchestrator
 *
 * Entry point for the vivid-devtools module. Owns:
 * - PanelManager (all panels)
 * - Shared OverlayCanvas
 * - Font management
 * - Video/snapshot export
 *
 * Provides backward-compatible APIs for vivid_ide_* and vivid_visualizer_* functions.
 */

#include <vivid/gui/panel_manager.h>
#include <vivid/devtools/shortcut_manager.h>
#include <vivid/devtools/preferences_panel.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/ui_style.h>
#include <vivid/video_exporter.h>
#include <vivid/frame_input.h>
#include <webgpu/webgpu.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

struct GLFWwindow;

namespace vivid {

class Context;
class Operator;
class FontAtlas;

/**
 * @brief Main DevTools orchestrator
 *
 * Singleton that manages all devtools functionality:
 * - NodeGraph panel for operator visualization
 * - Inspector panel for parameter editing
 * - Performance panel for real-time metrics
 * - StatusBar panel for record/snapshot controls
 *
 * Usage (via exports):
 * @code
 * vivid_devtools_init(&ctx, surfaceFormat);
 *
 * // Each frame:
 * vivid_devtools_update();
 * vivid_devtools_render(pass, &input, &ctx);
 *
 * // Cleanup:
 * vivid_devtools_shutdown();
 * @endcode
 */
class DevTools {
public:
    /**
     * @brief Get the singleton instance
     */
    static DevTools& instance();

    // -------------------------------------------------------------------------
    /// @name Lifecycle
    /// @{

    /**
     * @brief Initialize DevTools
     * @param ctx Vivid context
     * @param surfaceFormat Surface texture format
     * @return true on success
     */
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat);

    /**
     * @brief Shutdown and release resources
     */
    void shutdown();

    /**
     * @brief Update state (called each frame)
     */
    void update();

    /**
     * @brief Render all visible panels
     * @param pass Render pass encoder
     * @param input Frame input state
     * @param ctx Vivid context
     */
    void render(WGPURenderPassEncoder pass, const FrameInput& input, Context& ctx);

    /**
     * @brief Check if DevTools is available (initialized)
     */
    bool isAvailable() const { return m_initialized; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Input
    /// @{

    /**
     * @brief Check if DevTools consumed input this frame
     */
    bool consumedInput() const;

    /**
     * @brief Check if DevTools is currently being interacted with
     */
    bool isInteracting() const;

    /**
     * @brief Handle character input
     * @param codepoint Unicode codepoint
     */
    void onChar(uint32_t codepoint);

    /**
     * @brief Handle key down event
     * @param key Key code
     * @param mods Modifier flags
     * @return true if input was consumed by a shortcut
     */
    bool onKeyDown(int key, int mods);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Shortcuts
    /// @{

    /**
     * @brief Get the shortcut manager
     */
    ShortcutManager& shortcuts() { return m_shortcuts; }
    const ShortcutManager& shortcuts() const { return m_shortcuts; }

    /**
     * @brief Set callback for fullscreen toggle (handled by app.cpp)
     */
    using FullscreenCallback = std::function<void()>;
    void onFullscreenToggle(FullscreenCallback callback);

    /**
     * @brief Set callback for help panel toggle
     */
    using HelpCallback = std::function<void()>;
    void onHelpToggle(HelpCallback callback);

    /**
     * @brief Show the preferences dialog
     */
    void showPreferences();

    /**
     * @brief Hide the preferences dialog
     */
    void hidePreferences();

    /**
     * @brief Check if preferences dialog is visible
     */
    bool isPreferencesVisible() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Panel Control
    /// @{

    /**
     * @brief Show a panel
     * @param panelId Panel ID (e.g., "nodegraph", "inspector", "performance", "statusbar")
     */
    void showPanel(const std::string& panelId);

    /**
     * @brief Hide a panel
     */
    void hidePanel(const std::string& panelId);

    /**
     * @brief Toggle panel visibility
     */
    void togglePanel(const std::string& panelId);

    /**
     * @brief Check if a panel is visible
     */
    bool isPanelVisible(const std::string& panelId) const;

    /**
     * @brief Set GLFW window for clipboard
     */
    void setWindow(GLFWwindow* window);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Visualizer
    /// @{

    /**
     * @brief Toggle visualizer panels (nodegraph + inspector)
     *
     * Backward-compatible with pressing Tab
     */
    void toggleVisualizer();

    /**
     * @brief Check if visualizer panels are visible
     */
    bool isVisualizerVisible() const;

    /**
     * @brief Enter solo mode (show single operator output)
     */
    void enterSoloMode(Operator* op, const std::string& name);

    /**
     * @brief Exit solo mode
     */
    void exitSoloMode();

    /**
     * @brief Check if in solo mode
     */
    bool inSoloMode() const { return m_inSoloMode; }

    /**
     * @brief Get solo operator name
     */
    const std::string& soloOperatorName() const { return m_soloOperatorName; }

    /**
     * @brief Update solo output (call before blit)
     */
    void updateSoloOutput(Context& ctx);

    /**
     * @brief Select a node by name (for editor sync)
     */
    void selectNode(const std::string& name);

    /**
     * @brief Set focused node (3x preview)
     */
    void setFocusedNode(const std::string& name);

    /**
     * @brief Clear focused node
     */
    void clearFocusedNode();

    /**
     * @brief Set pending change count (Claude workflow)
     */
    void setPendingChangeCount(size_t count);

    /**
     * @brief Set MCP warning message
     */
    void setMcpWarning(const std::string& warning);

    /**
     * @brief Set parameter change callback
     */
    using ParamChangeCallback = std::function<void(const std::string&, const std::string&,
                                                    const float[4], const float[4], int)>;
    void onParamChange(ParamChangeCallback callback);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Video/Snapshot Export
    /// @{

    /**
     * @brief Save a snapshot
     */
    void saveSnapshot(Context& ctx);

    /**
     * @brief Check if snapshot was requested
     */
    bool snapshotRequested() const { return m_snapshotRequested; }

    /**
     * @brief Get video exporter
     */
    VideoExporter* getExporter() { return &m_exporter; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Style
    /// @{

    /**
     * @brief Get the UI style
     */
    const UIStyle& style() const { return m_style; }

    /**
     * @brief Get mutable style (for theme changes)
     */
    UIStyle& style() { return m_style; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Background Grid
    /// @{

    /**
     * @brief Toggle full-window background grid
     *
     * The background grid provides a dimmed backdrop with grid lines,
     * making it easier to focus on devtools panels. Independent of
     * the node graph visualizer.
     */
    void toggleBackgroundGrid();

    /**
     * @brief Check if background grid is visible
     */
    bool isBackgroundGridVisible() const { return m_gridOpacity > 0.0f; }

    /**
     * @brief Get background grid opacity (0.0 = hidden, 1.0 = fully visible)
     */
    float gridOpacity() const { return m_gridOpacity; }

    /**
     * @brief Set background grid opacity
     */
    void setGridOpacity(float opacity);

    /// @}

private:
    DevTools() = default;
    ~DevTools();

    // Non-copyable
    DevTools(const DevTools&) = delete;
    DevTools& operator=(const DevTools&) = delete;

    // Helper methods
    void registerDefaultShortcuts();
    void renderBackgroundGrid(OverlayCanvas& canvas, float screenWidth, float screenHeight);

    // State
    bool m_initialized = false;
    Context* m_ctx = nullptr;

    // Panel management
    std::unique_ptr<PanelManager> m_panelManager;
    std::unique_ptr<OverlayCanvas> m_canvas;

    // Keyboard shortcuts
    ShortcutManager m_shortcuts;

    // Preferences dialog
    std::unique_ptr<PreferencesPanel> m_preferencesPanel;

    // Fonts (shared across all panels)
    std::unique_ptr<FontAtlas> m_fonts[2];

    // Style
    UIStyle m_style;

    // Solo mode state
    Operator* m_soloOperator = nullptr;
    std::string m_soloOperatorName;
    bool m_inSoloMode = false;

    // Video export
    VideoExporter m_exporter;
    bool m_snapshotRequested = false;

    // Parameter callback
    ParamChangeCallback m_paramChangeCallback;

    // Pending changes (Claude workflow)
    size_t m_pendingChangeCount = 0;
    std::string m_mcpWarning;

    // Window handle for clipboard
    GLFWwindow* m_window = nullptr;

    // Shortcut callbacks
    FullscreenCallback m_fullscreenCallback;
    HelpCallback m_helpCallback;

    // Background grid (full-window focus backdrop, 0.0-1.0 opacity)
    float m_gridOpacity = 0.0f;
};

} // namespace vivid
