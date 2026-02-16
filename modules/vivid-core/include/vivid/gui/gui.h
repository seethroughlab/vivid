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
#include <vivid/gui/ring_buffer.h>
#include <vivid/gui/input_state.h>
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

    // Graph widget colors
    glm::vec4 graphBackground = {0.12f, 0.12f, 0.15f, 1.0f};
    glm::vec4 graphGrid = {0.25f, 0.25f, 0.28f, 0.5f};
    glm::vec4 graphBorder = {0.3f, 0.3f, 0.35f, 1.0f};

    // Layout
    float padding = 8.0f;           ///< Panel padding
    float spacing = 6.0f;           ///< Vertical spacing between widgets
    float widgetHeight = 24.0f;     ///< Default widget height
    float labelWidth = 80.0f;       ///< Width reserved for labels (when labelPosition=Left)
    float cornerRadius = 4.0f;      ///< Corner radius for rounded elements
    float borderWidth = 1.0f;       ///< Border width
    float valueWidth = 60.0f;       ///< Width reserved for value display (when valuePosition=Right)

    // Panel title
    float titleHeight = 48.0f;      ///< Height of panel title bar

    // Slider layout options
    LabelPosition labelPosition = LabelPosition::Left;   ///< Where to place labels
    ValuePosition valuePosition = ValuePosition::Center; ///< Where to display values

    /**
     * @brief Create a GuiStyle from a UIStyle, mapping overlapping fields
     * @param style Source UIStyle
     * @return GuiStyle with colors and layout from UIStyle
     */
    static GuiStyle fromUIStyle(const struct UIStyle& style);
};

/**
 * @brief Immediate-mode GUI context
 *
 * Create one per frame, call widget methods to render and interact.
 * Widgets are rendered to an OverlayCanvas and respond to user input.
 */
class Gui {
public:
    /**
     * @brief Construct GUI context
     * @param canvas OverlayCanvas for rendering
     * @param input Frame input for interaction
     */
    Gui(OverlayCanvas& canvas, const FrameInput& input);

    /**
     * @brief Construct GUI context (for use within GUI module)
     * @param canvas OverlayCanvas for rendering
     * @param input GUI input state for interaction
     */
    Gui(OverlayCanvas& canvas, const gui::InputState& input);

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
    /// @name Color Picker
    /// @{

    /**
     * @brief Color picker result with drag state information
     */
    struct ColorPickerResult {
        bool changed = false;       ///< Color changed this frame
        bool dragStarted = false;   ///< Drag started this frame
        bool dragEnded = false;     ///< Drag ended this frame
        glm::vec4 startColor;       ///< Color when drag started
    };

    /**
     * @brief XY pad result with drag state information
     */
    struct XYPadResult {
        bool changed = false;       ///< Value changed this frame
        bool dragStarted = false;   ///< Drag started this frame
        bool dragEnded = false;     ///< Drag ended this frame
        glm::vec2 startValue;       ///< Value when drag started
    };

    /**
     * @brief Vec3 row result with drag state information
     */
    struct Vec3RowResult {
        bool changed = false;       ///< Value changed this frame
        bool dragStarted = false;   ///< Drag started this frame
        bool dragEnded = false;     ///< Drag ended this frame
        glm::vec3 startValue;       ///< Value when drag started
        int activeComponent = -1;   ///< Which component is active (0=X, 1=Y, 2=Z, -1=none)
    };

    /**
     * @brief HSV color picker with expandable sliders
     * @param label Color picker label
     * @param color Pointer to color (RGBA, 0-1 range)
     * @param expanded Pointer to expanded state (managed by caller)
     * @return ColorPickerResult with change and drag state
     *
     * When collapsed, shows a color swatch. Click to expand.
     * When expanded, shows H/S/V/A sliders with hue gradient.
     */
    ColorPickerResult colorPickerHSV(const char* label, glm::vec4* color, bool* expanded);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Vector Widgets
    /// @{

    /**
     * @brief Display a 2D XY pad for vec2 input
     * @param label Widget label
     * @param value Pointer to vec2 value
     * @param min Minimum value for both axes
     * @param max Maximum value for both axes
     * @param size Size of the pad in pixels (square, 0 = use default)
     * @return XYPadResult with change and drag state
     */
    XYPadResult xyPad(const char* label, glm::vec2* value, float min, float max, float size = 0);

    /**
     * @brief Display a 2D XY pad with separate min/max per axis
     * @param label Widget label
     * @param value Pointer to vec2 value
     * @param minX Minimum X value
     * @param maxX Maximum X value
     * @param minY Minimum Y value
     * @param maxY Maximum Y value
     * @param size Size of the pad in pixels (square, 0 = use default)
     * @return XYPadResult with change and drag state
     */
    XYPadResult xyPadEx(const char* label, glm::vec2* value,
                        float minX, float maxX, float minY, float maxY, float size = 0);

    /**
     * @brief Display three mini-sliders in a row for vec3 input
     * @param label Widget label
     * @param value Pointer to vec3 value
     * @param min Minimum value for all components
     * @param max Maximum value for all components
     * @return Vec3RowResult with change and drag state
     */
    Vec3RowResult vec3Row(const char* label, glm::vec3* value, float min, float max);

    /**
     * @brief Display three mini-sliders with separate ranges per component
     * @param label Widget label
     * @param value Pointer to vec3 value
     * @param mins Minimum values per component
     * @param maxs Maximum values per component
     * @return Vec3RowResult with change and drag state
     */
    Vec3RowResult vec3RowEx(const char* label, glm::vec3* value,
                            const glm::vec3& mins, const glm::vec3& maxs);

    /// @}
    // -------------------------------------------------------------------------
    /// @name ADSR Envelope
    /// @{

    /**
     * @brief ADSR envelope result with drag state information
     */
    struct ADSRResult {
        bool changed = false;       ///< Value changed this frame
        bool dragStarted = false;   ///< Drag started this frame
        bool dragEnded = false;     ///< Drag ended this frame
        float startA = 0, startD = 0, startS = 0, startR = 0;  ///< Values when drag started
    };

    /**
     * @brief Display an ADSR envelope editor
     * @param label Widget label
     * @param attack Pointer to attack time (seconds)
     * @param decay Pointer to decay time (seconds)
     * @param sustain Pointer to sustain level (0-1)
     * @param release Pointer to release time (seconds)
     * @param maxTime Maximum time for A/D/R (seconds, default 2.0)
     * @return ADSRResult with change and drag state
     *
     * Displays an envelope curve with draggable control points,
     * plus four mini-sliders for precise value entry.
     */
    ADSRResult adsrEnvelope(const char* label,
                            float* attack, float* decay,
                            float* sustain, float* release,
                            float maxTime = 2.0f);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Graph Widgets
    /// @{

    /**
     * @brief Graph series configuration for multi-line graphs
     */
    struct GraphSeries {
        const float* data = nullptr;     ///< Pointer to data array
        size_t count = 0;                ///< Number of data points
        size_t offset = 0;               ///< Start index for ring buffers
        glm::vec4 color = {0.4f, 0.7f, 0.9f, 1.0f};  ///< Line color
        const char* label = nullptr;     ///< Optional series label
        float lineWidth = 1.5f;          ///< Line thickness
        bool filled = false;             ///< Fill area under line
    };

    /**
     * @brief Graph configuration
     */
    struct GraphConfig {
        float yMin = 0.0f;               ///< Minimum Y value (if not auto-scaling)
        float yMax = 1.0f;               ///< Maximum Y value (if not auto-scaling)
        bool autoScaleY = true;          ///< Automatically determine Y range from data
        bool showGrid = true;            ///< Show horizontal grid lines
        bool showYLabels = true;         ///< Show Y-axis value labels
        const char* yFormat = "%.1f";    ///< Printf format for Y labels
        bool showTooltip = true;         ///< Show value tooltip on hover
    };

    /**
     * @brief Graph interaction result
     */
    struct GraphResult {
        bool hovered = false;            ///< Mouse is over the graph
        int hoveredSampleIndex = -1;     ///< Index of hovered sample (-1 if none)
        float hoveredValue = 0.0f;       ///< Value at hovered sample
    };

    /**
     * @brief Display a time-series graph with multiple series
     * @param label Widget label
     * @param series Array of series configurations
     * @param seriesCount Number of series
     * @param config Graph configuration
     * @param height Graph height in pixels (0 = use default)
     * @return GraphResult with interaction state
     */
    GraphResult graph(const char* label, const GraphSeries* series, size_t seriesCount,
                      const GraphConfig& config, float height = 0);

    /**
     * @brief Display a time-series graph with multiple series (default config)
     */
    GraphResult graph(const char* label, const GraphSeries* series, size_t seriesCount) {
        return graph(label, series, seriesCount, GraphConfig{}, 0);
    }

    /**
     * @brief Display a simple single-series graph (convenience overload)
     * @param label Widget label
     * @param data Pointer to data array
     * @param count Number of data points
     * @param config Graph configuration
     * @param height Graph height in pixels (0 = use default)
     * @return GraphResult with interaction state
     */
    GraphResult graph(const char* label, const float* data, size_t count,
                      const GraphConfig& config, float height = 0);

    /**
     * @brief Display a simple single-series graph (default config)
     */
    GraphResult graph(const char* label, const float* data, size_t count) {
        return graph(label, data, count, GraphConfig{}, 0);
    }

    /**
     * @brief Display a graph from a ring buffer
     * @tparam T Value type
     * @tparam Capacity Ring buffer capacity
     * @param label Widget label
     * @param buffer Ring buffer containing data
     * @param config Graph configuration
     * @param height Graph height in pixels (0 = use default)
     * @return GraphResult with interaction state
     */
    template<typename T, size_t Capacity>
    GraphResult graph(const char* label, const RingBuffer<T, Capacity>& buffer,
                      const GraphConfig& config, float height = 0) {
        GraphSeries series;
        series.data = buffer.data();
        series.count = buffer.size();
        series.offset = buffer.offset();
        return graph(label, &series, 1, config, height);
    }

    /**
     * @brief Display a graph from a ring buffer (default config)
     */
    template<typename T, size_t Capacity>
    GraphResult graph(const char* label, const RingBuffer<T, Capacity>& buffer) {
        return graph(label, buffer, GraphConfig{}, 0);
    }

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
    gui::InputState m_input;  // Stored by value; constructed from FrameInput or gui::InputState
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

    // Interaction state for tracking active widgets across frames
    // Uses widget IDs (hash of label) to track which widget is active.
    // Thread-local for thread safety - each thread has its own state.
    // This allows the same drag operation to persist across Gui instances
    // created on consecutive frames (which is the normal immediate-mode pattern).
    struct InteractionState {
        uint32_t activeSlider = 0;          // ID of slider being dragged
        uint32_t openDropdown = 0;          // ID of open dropdown
        float sliderStartValue = 0;         // Value when drag started
        float panelScrollTarget = 0;        // Panel scroll offset
        uint32_t scrollingPanel = 0;        // ID of panel being scrolled
        // Color picker drag tracking
        uint32_t activeColorSlider = 0;     // ID of color slider being dragged
        glm::vec4 colorStartValue;          // Color when drag started (RGBA)
        // XY pad drag tracking
        uint32_t activeXYPad = 0;           // ID of XY pad being dragged
        glm::vec2 xyPadStartValue;          // Value when drag started
        // Vec3 row drag tracking
        uint32_t activeVec3Slider = 0;      // ID of vec3 mini-slider being dragged
        int vec3ActiveComponent = -1;       // Which component (0=X, 1=Y, 2=Z)
        glm::vec3 vec3StartValue;           // Value when drag started
        // ADSR envelope drag tracking
        uint32_t activeADSR = 0;            // ID of ADSR widget being dragged
        int adsrActiveComponent = -1;       // Which component (0=A, 1=D, 2=S, 3=R)
        float adsrStartA = 0, adsrStartD = 0, adsrStartS = 0, adsrStartR = 0;  // Values when drag started
    };
    static thread_local InteractionState s_state;  // Thread-local interaction state

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

    // Color picker drag tracking
    bool m_colorDragEnded = false;
    uint32_t m_lastColorDragId = 0;

    // XY pad drag tracking
    bool m_xyPadDragEnded = false;
    uint32_t m_lastXYPadDragId = 0;

    // Vec3 row drag tracking
    bool m_vec3DragEnded = false;
    uint32_t m_lastVec3DragId = 0;

    // ADSR drag tracking
    bool m_adsrDragEnded = false;
    uint32_t m_lastADSRDragId = 0;

    // Helper methods
    uint32_t hashId(const char* label) const;
    float contentWidth() const;
    void advanceCursor(float height);
    bool isMouseInRect(float x, float y, float w, float h) const;

    // Shared slider implementation (returns full SliderResult)
    SliderResult sliderImpl(const char* label, float* value, float min, float max);

    // Widget rendering helpers
    void drawWidgetBackground(float x, float y, float w, float h, bool hovered, bool active);
    void drawLabel(float x, float y, const char* text);
};

} // namespace vivid
