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
    constexpr int FloatingPanels = 350; ///< Floating panels (inspector, performance)
    constexpr int Menus = 400;         ///< Dropdown menus, context menus
    constexpr int ModalOverlay = 450;  ///< Modal dialog darkened overlay
    constexpr int ModalDialog = 460;   ///< Modal dialog content
    constexpr int Tooltips = 500;      ///< Tooltips (highest priority)
}

/// Centralized UI style - colors and layout values.
///
/// Layout values are in **logical pixels** - the canvas handles HiDPI scaling
/// internally via its content scale factor. This means you should NOT manually
/// scale values from this style.
///
/// Usage:
/// @code
/// UIStyle style;
///
/// // Use colors directly
/// canvas.fillRect(x, y, w, h, style.panelBg);
///
/// // Layout helpers return logical (unscaled) pixels
/// float p = style.padding();  // Returns 8.0f (canvas scales internally)
/// @endcode
///
/// @note The `scale` member is provided for cases where you need the HiDPI
/// factor outside the canvas system (e.g., text sizing or external rendering).
struct UIStyle {
    float scale = 1.0f;  ///< HiDPI scale factor for external use (1.0 = standard, 2.0 = Retina)

    // -------------------------------------------------------------------------
    // Panel colors
    // -------------------------------------------------------------------------
    glm::vec4 panelBg{0.12f, 0.12f, 0.15f, 0.95f};      ///< Panel background
    glm::vec4 panelBorder{0.3f, 0.3f, 0.35f, 1.0f};     ///< Panel border/outline
    glm::vec4 headerBg{0.16f, 0.16f, 0.2f, 1.0f};       ///< Panel header background

    // -------------------------------------------------------------------------
    // Corner radii (unscaled base values, use helper methods for scaled)
    // -------------------------------------------------------------------------
    float panelCornerRadiusBase = 6.0f;    ///< Panel corner radius (0 = square)
    float buttonCornerRadiusBase = 4.0f;   ///< Button corner radius
    float sliderCornerRadiusBase = 3.0f;   ///< Slider track corner radius

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
    // Layout helpers (return logical pixels - canvas handles HiDPI scaling)
    // -------------------------------------------------------------------------

    float padding() const { return 8.0f; }
    float smallPadding() const { return 4.0f; }
    float largePadding() const { return 12.0f; }

    float cornerRadius() const { return buttonCornerRadiusBase; }
    float largeCornerRadius() const { return panelCornerRadiusBase; }
    float panelCornerRadius() const { return panelCornerRadiusBase; }
    float buttonCornerRadius() const { return buttonCornerRadiusBase; }
    float sliderCornerRadius() const { return sliderCornerRadiusBase; }

    float strokeWidth() const { return 1.0f; }
    float thickStrokeWidth() const { return 2.0f; }

    float sliderHeight() const { return 20.0f; }
    float buttonPaddingX() const { return 8.0f; }
    float buttonPaddingY() const { return 4.0f; }

    float scrollSpeed() const { return 30.0f; }

    // -------------------------------------------------------------------------
    // DevTools-specific layout helpers (logical pixels)
    // -------------------------------------------------------------------------

    float titleBarHeight() const { return 48.0f; }
    float statusBarHeight() const { return 40.0f; }
    float inspectorWidth() const { return 280.0f; }
    float nodeWidth() const { return 180.0f; }
    float nodeHeaderHeight() const { return 24.0f; }
    float pinRadius() const { return 6.0f; }
    float connectionThickness() const { return 2.0f; }
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

    // Node graph
    style.nodeSelected = {1.0f, 1.0f, 0.0f, 1.0f};
    style.nodeFocused = {0.0f, 1.0f, 1.0f, 1.0f};

    return style;
}

} // namespace vivid
