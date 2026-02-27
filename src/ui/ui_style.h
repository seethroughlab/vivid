#ifndef VIVID_UI_STYLE_H
#define VIVID_UI_STYLE_H

#include <array>
#include <string>
#include <vector>

namespace vivid::ui {

struct UIStyle {
    std::string name;
    std::string id;

    // Corner radius (0 = sharp, 4-6 = rounded)
    float corner_radius = 0.0f;

    // Core palette
    std::array<float,3> node_bg;
    std::array<float,3> node_sel_bg;
    std::array<float,3> accent;
    std::array<float,3> slider_fill;
    std::array<float,3> inspector_bg;
    std::array<float,3> dim_text;
    std::array<float,3> bright_text;

    // Surfaces
    std::array<float,4> popup_bg;
    std::array<float,3> input_field_bg;
    std::array<float,3> separator;
    std::array<float,3> scrollbar_track;
    std::array<float,3> scrollbar_thumb;
    std::array<float,3> button_bg;
    std::array<float,3> button_hover;
    std::array<float,4> scrim;

    // Wires
    std::array<float,4> wire_color;
    std::array<float,4> wire_sel_color;

    // Slider
    std::array<float,3> slider_track;
    std::array<float,3> dark_bg;
};

// Returns the 3 built-in style presets
std::vector<UIStyle> builtin_styles();

// Find a style by id, returns nullptr if not found
const UIStyle* find_style(const std::vector<UIStyle>& styles, const std::string& id);

} // namespace vivid::ui

#endif // VIVID_UI_STYLE_H
