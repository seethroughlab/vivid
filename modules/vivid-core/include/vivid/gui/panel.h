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
#include <functional>

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
     * @param bounds Panel bounds in logical pixels (x, y, w, h)
     * @param input Frame input state
     * @param style UI style for colors and layout
     *
     * All coordinates are in logical pixels. The canvas handles scaling
     * to physical pixels internally based on contentScale.
     */
    virtual void render(OverlayCanvas& canvas, const glm::vec4& bounds,
                       const FrameInput& input, const UIStyle& style) = 0;

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
    bool isFocused() const { return m_focus.focused; }
    void setFocused(bool focused) { m_focus.focused = focused; }

    /**
     * @brief Check if mouse is hovering over panel
     */
    bool isHovered() const { return m_focus.hovered; }

    /**
     * @brief Check if panel is being interacted with (dragging, resizing)
     */
    bool isInteracting() const { return m_dragResize.isActive(); }

    /**
     * @brief Reset interaction state (dragging/resizing)
     *
     * Call this when a panel is docked to clear stale drag state.
     */
    void resetInteractionState() { m_dragResize.reset(); }

    /**
     * @brief Get/set bounds
     */
    const glm::vec4& bounds() const { return m_config.bounds; }
    void setBounds(const glm::vec4& bounds) { m_config.bounds = bounds; }

    /**
     * @brief Check if input was consumed by this panel
     */
    bool consumedInput() const { return m_inputRouting.consumedInput; }

    /**
     * @brief Set whether this panel can start new interactions
     *
     * Set to false for panels that are behind others at the mouse position.
     * This prevents multiple overlapping panels from starting drags.
     */
    void setCanStartInteraction(bool can) { m_inputRouting.canStartInteraction = can; }
    bool canStartInteraction() const { return m_inputRouting.canStartInteraction; }

    /**
     * @brief Control title bar visibility
     *
     * Set to false when panel is in a tabbed group (tab bar shows title).
     * Set to true for floating panels.
     */
    void setShowTitleBar(bool show) { m_display.showTitleBar = show; }
    bool showTitleBar() const { return m_display.showTitleBar; }

    /**
     * @brief Control rounded corners at the top
     *
     * Set to true when panel is docked at an edge (no rounded corners at top).
     * Set to false for floating panels (rounded corners on all sides).
     */
    void setSquareTopCorners(bool square) { m_display.squareTopCorners = square; }
    bool squareTopCorners() const { return m_display.squareTopCorners; }

    /**
     * @brief Set callback for when close button is clicked
     */
    using CloseCallback = std::function<void(Panel*)>;
    void setOnClose(CloseCallback callback) { m_onClose = std::move(callback); }

    /**
     * @brief Check if close button was clicked this frame
     * Call from render() after renderChrome() to check if panel should close
     */
    bool closeButtonClicked() const { return m_closeButton.clicked; }

    /**
     * @brief Input ownership model
     *
     * Each frame, exactly one panel "owns" input. The owner is determined by
     * PanelManager based on z-order and interaction state. Panels should check
     * ownsInput() before processing mouse/keyboard events.
     */
    void setInputOwnership(bool owns) { m_inputRouting.ownsInput = owns; }
    bool ownsInput() const { return m_inputRouting.ownsInput; }

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

    // -------------------------------------------------------------------------
    // Interaction State (grouped into logical structs)
    // -------------------------------------------------------------------------

    /// Focus and hover state
    struct FocusState {
        bool focused = false;   ///< Has keyboard focus
        bool hovered = false;   ///< Mouse is over panel
    };
    FocusState m_focus;

    /// Drag/resize state (mutually exclusive actions)
    struct DragResizeState {
        bool dragging = false;
        int resizing = 0;  // Bit flags: 1=left, 2=right, 4=top, 8=bottom
        glm::vec2 dragOffset = {0, 0};
        glm::vec4 startBounds = {0, 0, 0, 0};
        glm::vec2 startMouse = {0, 0};

        bool isActive() const { return dragging || resizing != 0; }
        void reset() {
            dragging = false;
            resizing = 0;
            dragOffset = {0, 0};
            startBounds = {0, 0, 0, 0};
            startMouse = {0, 0};
        }
    };
    DragResizeState m_dragResize;

    /// Input routing state (set by PanelManager each frame)
    struct InputRouting {
        bool consumedInput = false;       ///< Panel consumed input this frame
        bool canStartInteraction = true;  ///< Can start new drag/resize (based on z-order)
        bool ownsInput = false;           ///< Has exclusive input ownership
    };
    InputRouting m_inputRouting;

    /// Display options (affect rendering)
    struct DisplayOptions {
        bool showTitleBar = true;       ///< False when in tabbed group
        bool squareTopCorners = false;  ///< True when docked
    };
    DisplayOptions m_display;

    /// Close button state
    struct CloseButtonState {
        bool clicked = false;   ///< Clicked this frame
        bool hovered = false;   ///< Mouse is over button
    };
    CloseButtonState m_closeButton;
    CloseCallback m_onClose;

    /// Mouse tracking for click detection
    bool m_lastMouseDown = false;

    /**
     * @brief Resolve render bounds for floating vs docked panels
     *
     * Call this at the start of render() to get the correct bounds to use.
     * For floating panels (m_showTitleBar = true): handles drag/resize and returns m_config.bounds
     * For docked panels: returns passed bounds and sets m_hovered state
     *
     * @param input Frame input state
     * @param bounds Bounds passed from parent (used when docked)
     * @return The bounds to use for rendering
     */
    glm::vec4 beginRender(const FrameInput& input, const glm::vec4& bounds);

    /**
     * @brief Render common panel chrome (background, border, title bar)
     *
     * @param canvas OverlayCanvas for drawing
     * @param x Panel x position in logical pixels
     * @param y Panel y position in logical pixels
     * @param w Panel width in logical pixels
     * @param h Panel height in logical pixels
     * @param style UI style for colors
     * @param showTitleBar Whether to render title bar
     * @param input Frame input for close button click detection (optional)
     *
     * All coordinates are in logical pixels. The canvas handles scaling internally.
     */
    void renderChrome(OverlayCanvas& canvas, float x, float y, float w, float h,
                      const UIStyle& style, bool showTitleBar = true,
                      const FrameInput* input = nullptr);
};

} // namespace vivid
