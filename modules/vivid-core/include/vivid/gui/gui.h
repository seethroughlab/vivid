#pragma once

/**
 * @file gui.h
 * @brief Immediate-mode GUI widget system
 *
 * Provides high-level widgets (buttons, sliders, dropdowns, etc.) built on OverlayCanvas.
 * Designed for easy integration into Vivid projects.
 *
 * @par Example
 * @code
 * void update(Context& ctx) {
 *     static float scale = 1.0f;
 *     static int mode = 0;
 *
 *     Gui gui(ctx);
 *     gui.beginPanel("Controls", 10, 10, 200, 300);
 *     gui.slider("Scale", &scale, 0.1f, 5.0f);
 *     gui.dropdown("Mode", &mode, {"Normal", "Add", "Multiply"});
 *     if (gui.button("Reset")) {
 *         scale = 1.0f;
 *     }
 *     gui.endPanel();
 * }
 * @endcode
 */

#include <vivid/gui/overlay_canvas.h>
#include <vivid/frame_input.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace vivid {

class Context;

/**
 * @brief Visual style configuration for GUI widgets
 */
/**
 * @brief Label position options for sliders
 */
enum class LabelPosition {
    Left,   ///< Label to the left of widget (default)
    Above   ///< Label above the widget
};

/**
 * @brief Value display position for sliders
 */
enum class ValuePosition {
    Center, ///< Centered on slider (default)
    Right,  ///< To the right of slider
    None    ///< Don't display value
};

/**
 * @brief Visual style configuration for GUI widgets
 */
struct GuiStyle {
    // Colors
    glm::vec4 panelBackground = {0.15f, 0.15f, 0.18f, 0.95f};
    glm::vec4 panelBorder = {0.3f, 0.3f, 0.35f, 1.0f};
    glm::vec4 panelHeader = {0.2f, 0.2f, 0.25f, 1.0f};

    glm::vec4 widgetBackground = {0.25f, 0.25f, 0.3f, 1.0f};
    glm::vec4 widgetHover = {0.35f, 0.35f, 0.4f, 1.0f};
    glm::vec4 widgetActive = {0.2f, 0.4f, 0.8f, 1.0f};
    glm::vec4 widgetBorder = {0.4f, 0.4f, 0.45f, 1.0f};

    glm::vec4 sliderFill = {0.3f, 0.5f, 0.8f, 1.0f};
    glm::vec4 sliderFillActive = {0.4f, 0.6f, 0.9f, 1.0f};

    glm::vec4 checkmark = {0.3f, 0.7f, 0.3f, 1.0f};

    glm::vec4 text = {0.9f, 0.9f, 0.9f, 1.0f};
    glm::vec4 textDim = {0.6f, 0.6f, 0.6f, 1.0f};
    glm::vec4 textDisabled = {0.4f, 0.4f, 0.4f, 1.0f};

    // Layout
    float padding = 8.0f;           ///< Panel padding
    float spacing = 6.0f;           ///< Vertical spacing between widgets
    float widgetHeight = 24.0f;     ///< Default widget height
    float labelWidth = 80.0f;       ///< Width reserved for labels (when labelPosition=Left)
    float cornerRadius = 4.0f;      ///< Corner radius for rounded elements
    float borderWidth = 1.0f;       ///< Border width
    float valueWidth = 60.0f;       ///< Width reserved for value display (when valuePosition=Right)

    // Panel title
    float titleHeight = 28.0f;      ///< Height of panel title bar

    // Slider layout options
    LabelPosition labelPosition = LabelPosition::Left;   ///< Where to place labels
    ValuePosition valuePosition = ValuePosition::Center; ///< Where to display values
};

/**
 * @brief Immediate-mode GUI context
 *
 * Create one per frame, call widget methods to render and interact.
 * Widgets are rendered to an OverlayCanvas and respond to FrameInput.
 */
class Gui {
public:
    /**
     * @brief Construct GUI context
     * @param canvas OverlayCanvas for rendering
     * @param input Frame input for interaction
     */
    Gui(OverlayCanvas& canvas, const FrameInput& input);

    // -------------------------------------------------------------------------
    /// @name Panels
    /// @{

    /**
     * @brief Begin a panel (container for widgets)
     * @param title Panel title (displayed in header)
     * @param x Left position in screen pixels
     * @param y Top position in screen pixels
     * @param w Width in pixels
     * @param h Height in pixels
     *
     * Widgets added between beginPanel/endPanel use auto-layout within the panel.
     */
    void beginPanel(const char* title, float x, float y, float w, float h);

    /**
     * @brief End the current panel
     */
    void endPanel();

    /**
     * @brief Begin a raw layout area (no panel chrome)
     * @param x Left position
     * @param y Top position
     * @param w Width
     * @param h Height (for clipping)
     *
     * Use this when you want to render widgets without the panel background/header.
     * Useful for integrating Gui widgets into custom UI layouts.
     */
    void beginArea(float x, float y, float w, float h);

    /**
     * @brief End the current layout area
     */
    void endArea();

    /**
     * @brief Set the cursor Y position manually
     * @param y New cursor Y position
     */
    void setCursorY(float y);

    /**
     * @brief Get the current cursor Y position
     */
    float cursorY() const { return m_panel.cursorY; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Widgets
    /// @{

    /**
     * @brief Display a text label
     * @param text Text to display
     */
    void label(const char* text);

    /**
     * @brief Display a clickable button
     * @param label Button text
     * @return true if clicked this frame
     */
    bool button(const char* label);

    /**
     * @brief Display a checkbox
     * @param label Checkbox label
     * @param value Pointer to bool value (modified on click)
     * @return true if value changed this frame
     */
    bool checkbox(const char* label, bool* value);

    /**
     * @brief Display a horizontal slider (float)
     * @param label Slider label
     * @param value Pointer to float value
     * @param min Minimum value
     * @param max Maximum value
     * @return true if value changed this frame
     */
    bool slider(const char* label, float* value, float min, float max);

    /**
     * @brief Display a horizontal slider (int)
     * @param label Slider label
     * @param value Pointer to int value
     * @param min Minimum value
     * @param max Maximum value
     * @return true if value changed this frame
     */
    bool slider(const char* label, int* value, int min, int max);

    /**
     * @brief Display a dropdown selector
     * @param label Dropdown label
     * @param index Pointer to selected index
     * @param options List of option strings
     * @return true if selection changed this frame
     */
    bool dropdown(const char* label, int* index, const std::vector<std::string>& options);

    /**
     * @brief Display a color picker
     * @param label Color picker label
     * @param color Pointer to color (RGBA)
     * @return true if color changed this frame
     */
    bool colorPicker(const char* label, glm::vec4* color);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Layout Helpers
    /// @{

    /**
     * @brief Add vertical spacing
     * @param height Spacing in pixels (0 = use style.spacing)
     */
    void spacing(float height = 0);

    /**
     * @brief Draw a horizontal separator line
     */
    void separator();

    /// @}
    // -------------------------------------------------------------------------
    /// @name ID Scoping
    /// @{

    /**
     * @brief Push an ID prefix onto the stack
     * @param id String to prefix widget IDs with
     *
     * Use this to create unique widget IDs when the same label might
     * appear multiple times (e.g., in a loop).
     *
     * @code
     * for (auto& param : params) {
     *     gui.pushId(param.name.c_str());
     *     gui.slider("value", &param.value, 0, 1);
     *     gui.popId();
     * }
     * @endcode
     */
    void pushId(const char* id);

    /**
     * @brief Pop an ID prefix from the stack
     */
    void popId();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Scroll Control
    /// @{

    /**
     * @brief Begin a scrollable content area
     * @param height Visible height of the scroll area
     * @param contentHeight Total content height (for scroll range)
     * @param scrollOffset Pointer to scroll offset (modified by scroll input)
     */
    void beginScrollArea(float height, float contentHeight, float* scrollOffset);

    /**
     * @brief End the scrollable content area
     */
    void endScrollArea();

    /**
     * @brief Check if last widget is visible in current scroll area
     * @return true if the widget is at least partially visible
     */
    bool isLastWidgetVisible() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Extended Slider API
    /// @{

    /**
     * @brief Check if a slider drag just ended this frame
     * @return true if a slider was released this frame
     *
     * Use this to trigger callbacks when the user finishes dragging:
     * @code
     * if (gui.slider("Scale", &scale, 0, 10)) {
     *     // Value is changing
     * }
     * if (gui.sliderDragEnded()) {
     *     // User finished dragging - save/commit the change
     * }
     * @endcode
     */
    bool sliderDragEnded() const { return m_sliderDragEnded; }

    /**
     * @brief Get the value when the current slider drag started
     * @return Starting value (only valid when sliderDragEnded() is true)
     */
    float sliderStartValue() const { return s_state.sliderStartValue; }

    /**
     * @brief Check if a slider is currently being dragged
     */
    bool isSliderDragging() const { return s_state.activeSlider != 0; }

    /**
     * @brief Slider result with drag state information
     */
    struct SliderResult {
        bool changed = false;       ///< Value changed this frame
        bool dragStarted = false;   ///< Drag started this frame
        bool dragEnded = false;     ///< Drag ended this frame
        float startValue = 0;       ///< Value when drag started
    };

    /**
     * @brief Extended slider with full drag tracking
     * @param label Slider label
     * @param value Pointer to float value
     * @param min Minimum value
     * @param max Maximum value
     * @return SliderResult with change and drag state
     */
    SliderResult sliderEx(const char* label, float* value, float min, float max);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Style
    /// @{

    /**
     * @brief Get mutable reference to style
     */
    GuiStyle& style() { return m_style; }

    /**
     * @brief Get const reference to style
     */
    const GuiStyle& style() const { return m_style; }

    /// @}

private:
    OverlayCanvas& m_canvas;
    const FrameInput& m_input;
    GuiStyle m_style;

    // Panel layout state
    struct PanelState {
        float x = 0, y = 0, w = 0, h = 0;
        float cursorY = 0;          // Current Y position for next widget
        float scrollOffset = 0;     // Scroll position
        float contentHeight = 0;    // Total content height (for scroll)
        std::string title;
    };
    PanelState m_panel;
    bool m_inPanel = false;

    // Persistent interaction state (static to survive across frames)
    // Uses widget IDs (hash of label) to track which widget is active
    static struct InteractionState {
        uint32_t activeSlider = 0;          // ID of slider being dragged
        uint32_t openDropdown = 0;          // ID of open dropdown
        uint32_t expandedColorPicker = 0;   // ID of expanded color picker
        float sliderStartValue = 0;         // Value when drag started
        float sliderStartMouseX = 0;        // Mouse X when drag started
        float panelScrollTarget = 0;        // Panel scroll offset
        uint32_t scrollingPanel = 0;        // ID of panel being scrolled
    } s_state;

    // Input tracking
    bool m_mouseClicked = false;
    bool m_mouseReleased = false;
    glm::vec2 m_mousePos;

    // ID stack for scoping
    std::vector<std::string> m_idStack;

    // Scroll area state
    struct ScrollAreaState {
        bool active = false;
        float visibleTop = 0;
        float visibleBottom = 0;
        float* scrollOffset = nullptr;
    };
    ScrollAreaState m_scrollArea;
    float m_lastWidgetTop = 0;
    float m_lastWidgetBottom = 0;

    // Slider drag tracking
    bool m_sliderDragEnded = false;
    uint32_t m_lastSliderDragId = 0;

    // Helper methods
    uint32_t hashId(const char* label) const;
    float contentWidth() const;
    void advanceCursor(float height);
    bool isMouseInRect(float x, float y, float w, float h) const;

    // Widget rendering helpers
    void drawWidgetBackground(float x, float y, float w, float h, bool hovered, bool active);
    void drawLabel(float x, float y, const char* text);
};

} // namespace vivid
