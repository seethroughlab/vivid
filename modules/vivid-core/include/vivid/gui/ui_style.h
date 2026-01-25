#pragma once
#include <glm/glm.hpp>

namespace vivid {

/// Layer constants for z-ordering UI elements.
/// Higher values render on top of lower values.
/// Use these constants or any integer value with setLayer().
namespace UILayer {
    constexpr int Background = 0;      ///< Grid, background elements
    constexpr int Nodes = 100;         ///< Node boxes, connections, links
    constexpr int NodeContent = 200;   ///< Textured thumbnails, operator previews
    constexpr int Panels = 300;        ///< Inspector panel, debug panel
    constexpr int FloatingPanels = 350; ///< Floating panels (terminal, editor, console)
    constexpr int Menus = 400;         ///< Dropdown menus, context menus
    constexpr int ModalOverlay = 450;  ///< Modal dialog darkened overlay
    constexpr int ModalDialog = 460;   ///< Modal dialog content
    constexpr int Tooltips = 500;      ///< Tooltips (highest priority)
}

/// Centralized UI style - colors and HiDPI-scaled layout values.
///
/// Usage:
/// @code
/// UIStyle style;
/// style.scale = ctx.contentScale();  // Set from context for HiDPI
///
/// // Use colors directly
/// canvas.fillRect(x, y, w, h, style.panelBg);
///
/// // Use scaled layout helpers
/// float p = style.padding();  // Returns 8.0f * scale
/// @endcode
struct UIStyle {
    float scale = 1.0f;  ///< HiDPI scale factor (1.0 = standard, 2.0 = Retina)

    // -------------------------------------------------------------------------
    // Panel colors
    // -------------------------------------------------------------------------
    glm::vec4 panelBg{0.12f, 0.12f, 0.15f, 0.95f};      ///< Panel background
    glm::vec4 panelBorder{0.3f, 0.3f, 0.35f, 1.0f};     ///< Panel border/outline
    glm::vec4 headerBg{0.16f, 0.16f, 0.2f, 1.0f};       ///< Panel header background

    // -------------------------------------------------------------------------
    // Button colors
    // -------------------------------------------------------------------------
    glm::vec4 buttonBg{0.25f, 0.25f, 0.3f, 1.0f};       ///< Button background
    glm::vec4 buttonHover{0.35f, 0.35f, 0.4f, 1.0f};    ///< Button hover state
    glm::vec4 buttonBorder{0.4f, 0.4f, 0.45f, 1.0f};    ///< Button border

    // -------------------------------------------------------------------------
    // Slider colors
    // -------------------------------------------------------------------------
    glm::vec4 sliderBg{0.2f, 0.2f, 0.25f, 1.0f};        ///< Slider track background
    glm::vec4 sliderFill{0.4f, 0.6f, 0.9f, 1.0f};       ///< Slider filled portion
    glm::vec4 sliderActive{0.5f, 0.7f, 1.0f, 1.0f};     ///< Slider while dragging

    // -------------------------------------------------------------------------
    // Text colors
    // -------------------------------------------------------------------------
    glm::vec4 textPrimary{0.9f, 0.9f, 0.9f, 1.0f};      ///< Primary text
    glm::vec4 textDim{0.5f, 0.5f, 0.55f, 1.0f};         ///< Dimmed/secondary text
    glm::vec4 textTitle{0.5f, 0.8f, 1.0f, 1.0f};        ///< Title/header text

    // -------------------------------------------------------------------------
    // Accent/status colors
    // -------------------------------------------------------------------------
    glm::vec4 accent{0.4f, 0.7f, 0.9f, 1.0f};           ///< Accent color (blue)
    glm::vec4 accentActive{0.5f, 0.8f, 1.0f, 1.0f};     ///< Active accent
    glm::vec4 warning{0.9f, 0.9f, 0.4f, 1.0f};          ///< Warning (yellow)
    glm::vec4 error{0.9f, 0.4f, 0.4f, 1.0f};            ///< Error (red)
    glm::vec4 success{0.4f, 0.9f, 0.4f, 1.0f};          ///< Success (green)

    // -------------------------------------------------------------------------
    // Checkbox colors
    // -------------------------------------------------------------------------
    glm::vec4 checkboxBg{0.2f, 0.2f, 0.25f, 1.0f};      ///< Checkbox background
    glm::vec4 checkboxBorder{0.4f, 0.4f, 0.45f, 1.0f};  ///< Checkbox border
    glm::vec4 checkboxCheck{0.4f, 0.7f, 0.9f, 1.0f};    ///< Checkmark color

    // -------------------------------------------------------------------------
    // Syntax highlighting (for code editors)
    // -------------------------------------------------------------------------
    glm::vec4 syntaxKeyword{0.6f, 0.8f, 1.0f, 1.0f};    ///< Keywords (if, for, class)
    glm::vec4 syntaxComment{0.5f, 0.6f, 0.5f, 1.0f};    ///< Comments (// or /* */)
    glm::vec4 syntaxString{0.8f, 0.6f, 0.5f, 1.0f};     ///< String literals
    glm::vec4 syntaxNumber{0.8f, 0.8f, 0.5f, 1.0f};     ///< Numeric literals
    glm::vec4 syntaxFunction{0.9f, 0.8f, 0.6f, 1.0f};   ///< Function names
    glm::vec4 syntaxType{0.6f, 0.9f, 0.7f, 1.0f};       ///< Type names
    glm::vec4 syntaxOperator{0.9f, 0.9f, 0.9f, 1.0f};   ///< Operators (+, -, etc.)
    glm::vec4 syntaxPreproc{0.8f, 0.6f, 0.8f, 1.0f};    ///< Preprocessor (#include)

    // -------------------------------------------------------------------------
    // Terminal colors
    // -------------------------------------------------------------------------
    glm::vec4 terminalFg{0.9f, 0.9f, 0.9f, 1.0f};       ///< Terminal foreground
    glm::vec4 terminalBg{0.1f, 0.1f, 0.12f, 1.0f};      ///< Terminal background
    glm::vec4 terminalCursor{0.9f, 0.9f, 0.9f, 0.7f};   ///< Terminal cursor
    glm::vec4 terminalSelection{0.3f, 0.4f, 0.6f, 0.5f}; ///< Terminal selection

    // -------------------------------------------------------------------------
    // Editor colors
    // -------------------------------------------------------------------------
    glm::vec4 editorGutter{0.15f, 0.15f, 0.17f, 1.0f};  ///< Line number gutter
    glm::vec4 editorLineNum{0.5f, 0.5f, 0.5f, 1.0f};    ///< Line numbers
    glm::vec4 editorSelection{0.3f, 0.4f, 0.6f, 0.5f};  ///< Text selection
    glm::vec4 editorCursorLine{0.2f, 0.2f, 0.25f, 1.0f}; ///< Current line highlight
    glm::vec4 editorErrorLine{0.5f, 0.2f, 0.2f, 0.5f};  ///< Line with error
    glm::vec4 editorMatchBracket{0.4f, 0.6f, 0.8f, 0.3f}; ///< Matching bracket highlight

    // -------------------------------------------------------------------------
    // Connection/link colors (for node graphs)
    // -------------------------------------------------------------------------
    glm::vec4 connectionValue{1.0f, 0.7f, 0.3f, 0.9f};    ///< Value links (orange)
    glm::vec4 connectionTrigger{0.4f, 0.8f, 1.0f, 0.9f};  ///< Trigger links (cyan)
    glm::vec4 connectionEvent{0.4f, 1.0f, 0.6f, 0.9f};    ///< Event links (green)
    glm::vec4 connectionAudio{0.9f, 0.5f, 0.9f, 0.9f};    ///< Audio links (magenta)

    // -------------------------------------------------------------------------
    // Node graph colors
    // -------------------------------------------------------------------------
    glm::vec4 nodeSelected{0.8f, 0.6f, 0.2f, 1.0f};     ///< Selected node border (gold)
    glm::vec4 nodeFocused{0.4f, 0.7f, 0.9f, 1.0f};      ///< Focused node border (blue)
    glm::vec4 nodeHovered{0.5f, 0.5f, 0.55f, 1.0f};     ///< Hovered node border
    glm::vec4 pinInput{0.3f, 0.6f, 0.3f, 1.0f};         ///< Input pin (green)
    glm::vec4 pinOutput{0.6f, 0.3f, 0.3f, 1.0f};        ///< Output pin (red)
    glm::vec4 gridLine{0.2f, 0.2f, 0.22f, 1.0f};        ///< Grid lines
    glm::vec4 gridLineMajor{0.25f, 0.25f, 0.28f, 1.0f}; ///< Major grid lines

    // -------------------------------------------------------------------------
    // Scaled layout helpers
    // All return values scaled by the HiDPI factor
    // -------------------------------------------------------------------------

    float padding() const { return 8.0f * scale; }
    float smallPadding() const { return 4.0f * scale; }
    float largePadding() const { return 12.0f * scale; }

    float cornerRadius() const { return 4.0f * scale; }
    float largeCornerRadius() const { return 6.0f * scale; }

    float strokeWidth() const { return 1.0f * scale; }
    float thickStrokeWidth() const { return 2.0f * scale; }

    float sliderHeight() const { return 20.0f * scale; }
    float buttonPaddingX() const { return 8.0f * scale; }
    float buttonPaddingY() const { return 4.0f * scale; }

    float scrollSpeed() const { return 30.0f * scale; }

    // -------------------------------------------------------------------------
    // DevTools-specific layout helpers
    // -------------------------------------------------------------------------

    float titleBarHeight() const { return 28.0f * scale; }
    float tabBarHeight() const { return 32.0f * scale; }
    float statusBarHeight() const { return 40.0f * scale; }
    float inspectorWidth() const { return 280.0f * scale; }
    float nodeWidth() const { return 180.0f * scale; }
    float nodeHeaderHeight() const { return 24.0f * scale; }
    float pinRadius() const { return 6.0f * scale; }
    float connectionThickness() const { return 2.0f * scale; }
};

// =============================================================================
// Theme presets
// =============================================================================

/// Create the default dark theme (matches current Vivid styling)
inline UIStyle createDarkTheme() {
    UIStyle style;
    // All defaults are already set to dark theme values
    return style;
}

/// Create a light theme for daylight/high-brightness environments
inline UIStyle createLightTheme() {
    UIStyle style;

    // Panel colors (inverted)
    style.panelBg = {0.95f, 0.95f, 0.96f, 0.98f};
    style.panelBorder = {0.75f, 0.75f, 0.78f, 1.0f};
    style.headerBg = {0.9f, 0.9f, 0.92f, 1.0f};

    // Button colors
    style.buttonBg = {0.85f, 0.85f, 0.88f, 1.0f};
    style.buttonHover = {0.8f, 0.8f, 0.83f, 1.0f};
    style.buttonBorder = {0.7f, 0.7f, 0.73f, 1.0f};

    // Slider colors
    style.sliderBg = {0.85f, 0.85f, 0.88f, 1.0f};
    style.sliderFill = {0.3f, 0.5f, 0.8f, 1.0f};
    style.sliderActive = {0.4f, 0.6f, 0.9f, 1.0f};

    // Text colors
    style.textPrimary = {0.15f, 0.15f, 0.18f, 1.0f};
    style.textDim = {0.5f, 0.5f, 0.52f, 1.0f};
    style.textTitle = {0.2f, 0.4f, 0.7f, 1.0f};

    // Syntax highlighting (darker for light bg)
    style.syntaxKeyword = {0.2f, 0.4f, 0.8f, 1.0f};
    style.syntaxComment = {0.35f, 0.5f, 0.35f, 1.0f};
    style.syntaxString = {0.7f, 0.3f, 0.2f, 1.0f};
    style.syntaxNumber = {0.6f, 0.5f, 0.1f, 1.0f};
    style.syntaxFunction = {0.6f, 0.4f, 0.1f, 1.0f};

    // Terminal colors
    style.terminalFg = {0.1f, 0.1f, 0.12f, 1.0f};
    style.terminalBg = {0.98f, 0.98f, 0.99f, 1.0f};
    style.terminalCursor = {0.1f, 0.1f, 0.12f, 0.8f};

    // Editor colors
    style.editorGutter = {0.92f, 0.92f, 0.94f, 1.0f};
    style.editorLineNum = {0.6f, 0.6f, 0.62f, 1.0f};
    style.editorSelection = {0.3f, 0.5f, 0.8f, 0.3f};
    style.editorCursorLine = {0.9f, 0.92f, 0.95f, 1.0f};

    // Grid colors
    style.gridLine = {0.88f, 0.88f, 0.9f, 1.0f};
    style.gridLineMajor = {0.82f, 0.82f, 0.85f, 1.0f};

    return style;
}

/// Create a high-contrast theme for accessibility
inline UIStyle createHighContrastTheme() {
    UIStyle style;

    // Strong contrast
    style.panelBg = {0.0f, 0.0f, 0.0f, 1.0f};
    style.panelBorder = {1.0f, 1.0f, 1.0f, 1.0f};
    style.headerBg = {0.1f, 0.1f, 0.1f, 1.0f};

    // Text
    style.textPrimary = {1.0f, 1.0f, 1.0f, 1.0f};
    style.textDim = {0.8f, 0.8f, 0.8f, 1.0f};
    style.textTitle = {0.0f, 1.0f, 1.0f, 1.0f};

    // Strong accent colors
    style.accent = {0.0f, 1.0f, 1.0f, 1.0f};
    style.warning = {1.0f, 1.0f, 0.0f, 1.0f};
    style.error = {1.0f, 0.0f, 0.0f, 1.0f};
    style.success = {0.0f, 1.0f, 0.0f, 1.0f};

    // Bright syntax colors
    style.syntaxKeyword = {0.5f, 0.8f, 1.0f, 1.0f};
    style.syntaxComment = {0.5f, 0.8f, 0.5f, 1.0f};
    style.syntaxString = {1.0f, 0.8f, 0.5f, 1.0f};
    style.syntaxNumber = {1.0f, 1.0f, 0.5f, 1.0f};

    // Strong selection
    style.editorSelection = {0.3f, 0.6f, 1.0f, 0.6f};
    style.terminalSelection = {0.3f, 0.6f, 1.0f, 0.6f};

    // Node graph
    style.nodeSelected = {1.0f, 1.0f, 0.0f, 1.0f};
    style.nodeFocused = {0.0f, 1.0f, 1.0f, 1.0f};

    return style;
}

// =============================================================================
// TODO: Future widget system - immediate-mode API like Dear ImGui
//
// The goal is to provide simple, reusable widgets that handle:
// - Hit testing (is mouse over this widget?)
// - State tracking (hover, active, pressed)
// - Rendering (using OverlayCanvas + UIStyle)
//
// Example future API:
//
// struct UIContext {
//     OverlayCanvas* canvas;
//     UIStyle* style;
//     glm::vec2 mousePos;
//     bool mouseDown;
//     bool mouseClicked;
//     bool mouseReleased;
//
//     // Returns true if button was clicked
//     bool button(const char* label, float x, float y, float w, float h);
//
//     // Returns true if value changed, updates *value in place
//     bool slider(const char* label, float x, float y, float w,
//                 float* value, float minVal, float maxVal);
//
//     // Returns true if toggled, updates *checked in place
//     bool checkbox(const char* label, float x, float y, bool* checked);
//
//     // Layout helpers
//     void beginPanel(const char* title, float x, float y, float w, float h);
//     void endPanel();
//
//     // Automatic layout (like ImGui)
//     void sameLine();
//     void indent();
//     void unindent();
// };
//
// Usage would be:
//   UIContext ui(canvas, style, input);
//   ui.beginPanel("Inspector", panelX, panelY, panelW, panelH);
//   if (ui.slider("Scale", &scaleValue, 0.0f, 10.0f)) {
//       // value changed
//   }
//   if (ui.checkbox("Enable", &enableFlag)) {
//       // toggled
//   }
//   if (ui.button("Reset")) {
//       // clicked
//   }
//   ui.endPanel();
// =============================================================================

} // namespace vivid
