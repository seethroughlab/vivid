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
    constexpr int Menus = 400;         ///< Dropdown menus, context menus
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
};

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
