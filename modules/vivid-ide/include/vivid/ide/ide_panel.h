#pragma once

/**
 * @file ide_panel.h
 * @brief Main IDE panel orchestrator
 *
 * Manages terminal and editor panels, provides tab switching,
 * handles panel dragging/resizing, and routes keyboard input.
 */

#include <vivid/ide/terminal_panel.h>
#include <vivid/ide/editor_panel.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/frame_input.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <functional>

namespace vivid {

class Context;  // Forward declaration

/**
 * @brief Active tab in the IDE panel
 */
enum class IdeTab {
    Terminal,
    Editor
};

/**
 * @brief IDE panel containing terminal and editor
 *
 * The IdePanel is a moveable/resizable panel that contains:
 * - Terminal tab: Shell access for Claude Code, build commands
 * - Editor tab: chain.cpp editing with syntax highlighting
 *
 * Features:
 * - Tab switching via Cmd+1/2 or clicking tabs
 * - Draggable title bar
 * - Resizable edges
 * - Keyboard focus routing to active panel
 *
 * Usage:
 * @code
 * IdePanel ide;
 * ide.init(device, queue, surfaceFormat);
 * ide.setWorkingDirectory("/path/to/project");
 * ide.openFile("/path/to/project/chain.cpp");
 *
 * // Each frame:
 * ide.update();
 * ide.render(pass, input, width, height);
 * @endcode
 */
class IdePanel {
public:
    IdePanel();
    ~IdePanel();

    // Non-copyable
    IdePanel(const IdePanel&) = delete;
    IdePanel& operator=(const IdePanel&) = delete;

    /**
     * @brief Initialize IDE panel and create sub-panels
     * @param ctx Vivid context (for font loading and GPU resources)
     * @param surfaceFormat Surface texture format
     * @return true on success
     */
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat);

    /**
     * @brief Shutdown and release resources
     */
    void shutdown();

    /**
     * @brief Set working directory for terminal
     * @param path Working directory path
     */
    void setWorkingDirectory(const std::string& path);

    /**
     * @brief Open a file in the editor
     * @param path File path
     * @return true if opened successfully
     */
    bool openFile(const std::string& path);

    /**
     * @brief Update state (process PTY, etc.)
     * Call each frame.
     */
    void update();

    /**
     * @brief Render IDE panel
     * @param pass Render pass encoder
     * @param input Frame input
     * @param screenWidth Screen width in logical pixels
     * @param screenHeight Screen height in logical pixels
     */
    void render(WGPURenderPassEncoder pass, const FrameInput& input,
                float screenWidth, float screenHeight);

    /**
     * @brief Check if IDE panel consumed input this frame
     */
    bool consumedInput() const { return m_consumedInput; }

    /**
     * @brief Check if mouse is hovering over IDE panel
     */
    bool isHovered() const { return m_hovered; }

    /**
     * @brief Check if IDE panel is being dragged
     */
    bool isDragging() const { return m_dragging; }

    /**
     * @brief Check if IDE panel is being resized
     */
    bool isResizing() const { return m_resizing != 0; }

    // -------------------------------------------------------------------------
    /// @name Panel State
    /// @{

    /**
     * @brief Get/set visibility
     */
    bool isVisible() const { return m_visible; }
    void setVisible(bool visible) { m_visible = visible; }

    /**
     * @brief Toggle visibility
     */
    void toggleVisible() { m_visible = !m_visible; }

    /**
     * @brief Get/set panel bounds (x, y, width, height)
     */
    glm::vec4 bounds() const { return m_bounds; }
    void setBounds(const glm::vec4& bounds) { m_bounds = bounds; }

    /**
     * @brief Get/set active tab
     */
    IdeTab activeTab() const { return m_activeTab; }
    void setActiveTab(IdeTab tab);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Sub-panel Access
    /// @{

    /**
     * @brief Get terminal panel
     */
    TerminalPanel& terminal() { return *m_terminal; }
    const TerminalPanel& terminal() const { return *m_terminal; }

    /**
     * @brief Get editor panel
     */
    EditorPanel& editor() { return *m_editor; }
    const EditorPanel& editor() const { return *m_editor; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Callbacks
    /// @{

    /**
     * @brief Set callback for compile error (from editor save + hot-reload)
     */
    void onCompileError(std::function<void(bool success, const std::string& error)> callback);

    /**
     * @brief Set compile status (success or error message)
     */
    void setCompileStatus(bool success, const std::string& message);

    /// @}

private:
    void renderTabBar(OverlayCanvas& canvas, float x, float y, float w, float scale);
    void renderTitleBar(OverlayCanvas& canvas, float x, float y, float w, float scale);
    void handleDragAndResize(const FrameInput& input, float screenW, float screenH);

    std::unique_ptr<TerminalPanel> m_terminal;
    std::unique_ptr<EditorPanel> m_editor;
    std::unique_ptr<OverlayCanvas> m_canvas;

    // Panel state
    bool m_visible = false;
    bool m_initialized = false;
    glm::vec4 m_bounds = {20, 60, 900, 600};  // x, y, w, h - larger default for Claude Code TUI
    IdeTab m_activeTab = IdeTab::Terminal;

    // Interaction state
    bool m_consumedInput = false;
    bool m_hovered = false;
    bool m_dragging = false;
    int m_resizing = 0;  // Bit flags: 1=left, 2=right, 4=top, 8=bottom
    glm::vec2 m_dragOffset = {0, 0};
    glm::vec4 m_resizeStartBounds = {0, 0, 0, 0};
    glm::vec2 m_resizeStartMouse = {0, 0};

    // Working directory
    std::string m_workingDir;

    // Compile status
    bool m_compileSuccess = true;
    std::string m_compileMessage;
    std::function<void(bool, const std::string&)> m_onCompileError;
};

} // namespace vivid
