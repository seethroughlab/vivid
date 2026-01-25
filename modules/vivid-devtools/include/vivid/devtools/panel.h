#pragma once

/**
 * @file panel.h
 * @brief Abstract Panel base class for devtools panels
 *
 * All devtools panels (Terminal, Editor, NodeGraph, Inspector) inherit from this base.
 * Provides common functionality: drag/resize, focus management, visibility control.
 */

#include <vivid/gui/overlay_canvas.h>
#include <vivid/frame_input.h>
#include <glm/glm.hpp>
#include <string>

namespace vivid {

class Context;  // Forward declaration

/**
 * @brief Dock position for panels
 */
enum class DockSide {
    None,       ///< Floating panel (can be anywhere)
    Left,       ///< Docked to left edge
    Right,      ///< Docked to right edge
    Top,        ///< Docked to top edge
    Bottom,     ///< Docked to bottom edge
    Fill        ///< Fills available space (like NodeGraph)
};

/**
 * @brief Panel configuration
 */
struct PanelConfig {
    std::string id;           ///< Unique identifier (e.g., "terminal", "inspector")
    std::string title;        ///< Display title (e.g., "Terminal", "Inspector")
    glm::vec4 bounds;         ///< Position and size (x, y, w, h) in logical pixels
    DockSide dockSide;        ///< Dock position
    bool visible;             ///< Whether panel is visible
    bool resizable;           ///< Whether panel can be resized
    bool draggable;           ///< Whether panel can be dragged
    float minWidth;           ///< Minimum width
    float minHeight;          ///< Minimum height
};

/**
 * @brief Abstract base class for devtools panels
 *
 * Provides common functionality:
 * - Drag and resize handling
 * - Focus management
 * - Visibility control
 * - Common rendering helpers
 *
 * Subclasses implement:
 * - init() for panel-specific initialization
 * - render() for panel-specific rendering
 * - handleInput() for panel-specific input handling
 * - onChar()/onKeyDown() for keyboard input
 */
class Panel {
public:
    Panel();
    virtual ~Panel();

    // Non-copyable
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    // -------------------------------------------------------------------------
    /// @name Lifecycle
    /// @{

    /**
     * @brief Initialize the panel
     * @param ctx Vivid context (for GPU resources, fonts)
     * @param surfaceFormat Surface texture format
     * @return true on success
     */
    virtual bool init(Context& ctx, WGPUTextureFormat surfaceFormat) = 0;

    /**
     * @brief Shutdown and release resources
     */
    virtual void shutdown() = 0;

    /**
     * @brief Update panel state (called each frame before render)
     */
    virtual void update() {}

    /// @}
    // -------------------------------------------------------------------------
    /// @name Rendering
    /// @{

    /**
     * @brief Render the panel
     * @param canvas OverlayCanvas for drawing
     * @param bounds Panel bounds in physical pixels (x, y, w, h)
     * @param input Frame input state
     * @param scale Content scale factor (for HiDPI)
     * @param style UI style for colors and layout
     */
    virtual void render(OverlayCanvas& canvas, const glm::vec4& bounds,
                       const FrameInput& input, float scale, const UIStyle& style) = 0;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Input Handling
    /// @{

    /**
     * @brief Handle input events
     * @param input Frame input state
     * @return true if input was consumed
     */
    virtual bool handleInput(const FrameInput& input) { return false; }

    /**
     * @brief Handle character input (for text entry)
     * @param codepoint Unicode codepoint
     */
    virtual void onChar(uint32_t codepoint) {}

    /**
     * @brief Handle key down event
     * @param key Key code
     * @param mods Modifier flags
     */
    virtual void onKeyDown(int key, int mods) {}

    /// @}
    // -------------------------------------------------------------------------
    /// @name State
    /// @{

    /**
     * @brief Get panel configuration
     */
    const PanelConfig& config() const { return m_config; }

    /**
     * @brief Get/set visibility
     */
    bool isVisible() const { return m_config.visible; }
    void setVisible(bool visible) { m_config.visible = visible; }
    void toggleVisible() { m_config.visible = !m_config.visible; }

    /**
     * @brief Get/set focus state
     */
    bool isFocused() const { return m_focused; }
    void setFocused(bool focused) { m_focused = focused; }

    /**
     * @brief Check if mouse is hovering over panel
     */
    bool isHovered() const { return m_hovered; }

    /**
     * @brief Check if panel is being interacted with (dragging, resizing)
     */
    bool isInteracting() const { return m_dragging || m_resizing != 0; }

    /**
     * @brief Get/set bounds
     */
    const glm::vec4& bounds() const { return m_config.bounds; }
    void setBounds(const glm::vec4& bounds) { m_config.bounds = bounds; }

    /**
     * @brief Check if input was consumed by this panel
     */
    bool consumedInput() const { return m_consumedInput; }

    /**
     * @brief Set whether this panel can start new interactions
     *
     * Set to false for panels that are behind others at the mouse position.
     * This prevents multiple overlapping panels from starting drags.
     */
    void setCanStartInteraction(bool can) { m_canStartInteraction = can; }
    bool canStartInteraction() const { return m_canStartInteraction; }

    /**
     * @brief Input ownership model
     *
     * Each frame, exactly one panel "owns" input. The owner is determined by
     * PanelManager based on z-order and interaction state. Panels should check
     * ownsInput() before processing mouse/keyboard events.
     */
    void setInputOwnership(bool owns) { m_ownsInput = owns; }
    bool ownsInput() const { return m_ownsInput; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Drag and Resize
    /// @{

    /**
     * @brief Handle drag and resize interactions
     *
     * Call this from render() to enable drag (from title bar) and resize (from edges).
     * Updates m_config.bounds, m_dragging, m_resizing, m_hovered states.
     *
     * @param input Frame input state (logical coordinates)
     * @param screenW Screen width in logical pixels
     * @param screenH Screen height in logical pixels
     * @param titleBarHeight Height of title bar (drag zone) in logical pixels
     * @param allowNewInteraction If false, won't start new drags/resizes (but continues existing ones)
     */
    void handleDragAndResize(const FrameInput& input, float screenW, float screenH,
                             float titleBarHeight = 28.0f, bool allowNewInteraction = true);

    /// @}

protected:
    // Configuration
    PanelConfig m_config;

    // Interaction state
    bool m_focused = false;
    bool m_hovered = false;
    bool m_dragging = false;
    int m_resizing = 0;  // Bit flags: 1=left, 2=right, 4=top, 8=bottom
    bool m_consumedInput = false;
    bool m_canStartInteraction = true;  // Set by panel manager based on z-order
    bool m_ownsInput = false;           // Set by panel manager each frame

    // Drag/resize state
    glm::vec2 m_dragOffset = {0, 0};
    glm::vec4 m_resizeStartBounds = {0, 0, 0, 0};
    glm::vec2 m_resizeStartMouse = {0, 0};

    // Mouse tracking
    bool m_lastMouseDown = false;  // For click detection

    /**
     * @brief Render common panel chrome (background, border, title bar)
     *
     * @param canvas OverlayCanvas for drawing
     * @param x Panel x position in physical pixels
     * @param y Panel y position in physical pixels
     * @param w Panel width in physical pixels
     * @param h Panel height in physical pixels
     * @param scale Content scale factor
     * @param style UI style for colors
     * @param showTitleBar Whether to render title bar
     */
    void renderChrome(OverlayCanvas& canvas, float x, float y, float w, float h,
                      float scale, const UIStyle& style, bool showTitleBar = true);
};

} // namespace vivid
