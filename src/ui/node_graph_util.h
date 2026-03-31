#pragma once

#include "ui/renderer_2d.h"
#include "ui/ui_style.h"
#include "ui/text_edit.h"
#include "operator_api/types.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>

namespace vivid::ui {

// --- HSV <-> RGB conversion ---

inline void hsv_to_rgb(float h, float s, float v, float& r, float& g, float& b) {
    if (s <= 0.0f) { r = g = b = v; return; }
    float hh = std::fmod(h, 360.0f) / 60.0f;
    int i = static_cast<int>(hh);
    float f = hh - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

inline void rgb_to_hsv(float r, float g, float b, float& h, float& s, float& v) {
    float mx = std::max({r, g, b});
    float mn = std::min({r, g, b});
    v = mx;
    float d = mx - mn;
    s = (mx > 0.0f) ? d / mx : 0.0f;
    if (d < 0.00001f) { h = 0.0f; return; }
    if (mx == r)      h = 60.0f * std::fmod((g - b) / d, 6.0f);
    else if (mx == g) h = 60.0f * ((b - r) / d + 2.0f);
    else              h = 60.0f * ((r - g) / d + 4.0f);
    if (h < 0.0f) h += 360.0f;
}

// --- Hex color formatting ---

inline void rgb_to_hex(float r, float g, float b, char* buf, size_t buf_size) {
    int ri = static_cast<int>(std::round(std::max(0.0f, std::min(1.0f, r)) * 255.0f));
    int gi = static_cast<int>(std::round(std::max(0.0f, std::min(1.0f, g)) * 255.0f));
    int bi = static_cast<int>(std::round(std::max(0.0f, std::min(1.0f, b)) * 255.0f));
    std::snprintf(buf, buf_size, "#%02X%02X%02X", ri, gi, bi);
}

// --- Border rect (filled rect + 1px border lines) ---

inline void draw_rect_border(Renderer2D& tr, float x, float y, float w, float h,
                             float br, float bg, float bb, float ba = 0.6f,
                             float thickness = 1.0f) {
    tr.draw_rect(x, y, w, thickness, br, bg, bb, ba);                   // top
    tr.draw_rect(x, y + h - thickness, w, thickness, br, bg, bb, ba);   // bottom
    tr.draw_rect(x, y, thickness, h, br, bg, bb, ba);                   // left
    tr.draw_rect(x + w - thickness, y, thickness, h, br, bg, bb, ba);   // right
}

// --- Lo-fi drop shadow (offset dark rects, no blur) ---

inline void draw_shadow(Renderer2D& tr, float x, float y, float w, float h,
                        float corner_radius = 0.0f) {
    // 2-layer stepped shadow: further offset = more diffuse
    tr.draw_rounded_rect(x + 3.0f, y + 3.0f, w, h, corner_radius,
                         0.0f, 0.0f, 0.0f, 0.20f);
    tr.draw_rounded_rect(x + 6.0f, y + 6.0f, w, h, corner_radius,
                         0.0f, 0.0f, 0.0f, 0.10f);
}

// --- Popup background (shadow + rect + accent bar at top) ---

inline void draw_popup_bg(Renderer2D& tr, const UIStyle& style,
                          float x, float y, float w, float h) {
    draw_shadow(tr, x, y, w, h);
    tr.draw_rect(x, y, w, h,
                 style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    tr.draw_rect(x, y, w, 1,
                 style.accent[0], style.accent[1], style.accent[2]);
}

// --- Editing text field ---

// Draws the active editing state of a text field:
//   - 1px accent border (outset by 1px on all sides)
//   - input_field_bg filled rect
//   - selection highlight behind selected text
//   - cursor bar at insertion point
// Call this only in the is_editing branch. The caller handles non-editing display.
inline void draw_editing_text_field(
    Renderer2D& tr, const UIStyle& style,
    float x, float y, float w, float h,
    const std::string& buffer, const TextEditState& text_edit,
    bool blink_on,
    float pad_left = 4.0f, float pad_top = 2.0f)
{
    tr.draw_rect(x - 1, y - 1, w + 2, h + 2,
                 style.accent[0], style.accent[1], style.accent[2]);
    tr.draw_rect(x, y, w, h,
                 style.input_field_bg[0], style.input_field_bg[1], style.input_field_bg[2]);

    // Draw selection highlight
    if (text_edit.has_selection()) {
        int lo = text_edit.sel_min();
        int hi = text_edit.sel_max();
        float sel_x0 = x + pad_left + tr.text_width(buffer.substr(0, lo).c_str());
        float sel_x1 = x + pad_left + tr.text_width(buffer.substr(0, hi).c_str());
        tr.draw_rect(sel_x0, y + 1, sel_x1 - sel_x0, h - 2,
                     style.accent[0], style.accent[1], style.accent[2], 0.3f);
    }

    // Draw text
    tr.draw_text(x + pad_left, y + pad_top, buffer.c_str(),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);

    // Draw cursor bar
    if (blink_on) {
        int cpos = std::max(0, std::min(text_edit.cursor, static_cast<int>(buffer.size())));
        float cursor_x = x + pad_left + tr.text_width(buffer.substr(0, cpos).c_str());
        tr.draw_rect(cursor_x, y + 1, 1.0f, h - 2,
                     style.bright_text[0], style.bright_text[1], style.bright_text[2]);
    }
}

// --- Tab button ---

// Draws one tab button and returns its pixel width (text_width + 16).
// Caller is responsible for advancing tab_x by the returned width + gap.
inline float draw_tab_button(
    Renderer2D& tr, const UIStyle& style,
    float x, float y, float h,
    const char* label, bool selected, bool hovered)
{
    float tw = tr.text_width(label) + 16.0f;
    if (selected) {
        tr.draw_rect(x, y, tw, h, style.accent[0], style.accent[1], style.accent[2], 0.9f);
        tr.draw_text(x + 8, y + 3, label, 0.0f, 0.0f, 0.0f);
    } else {
        if (hovered)
            tr.draw_rect(x, y, tw, h, style.button_hover[0], style.button_hover[1], style.button_hover[2], 0.6f);
        else
            tr.draw_rect(x, y, tw, h, style.button_bg[0], style.button_bg[1], style.button_bg[2], 0.6f);
        tr.draw_text(x + 8, y + 3, label, style.dim_text[0], style.dim_text[1], style.dim_text[2]);
    }
    return tw;
}

// --- Checkbox ---

// Draws a checkbox: background track rect, filled accent rect if checked.
// alpha_if_checked lets callers dim the fill (e.g. 0.3f when param is connected).
inline void draw_checkbox(
    Renderer2D& tr, const UIStyle& style,
    float x, float y, float size,
    bool checked, float alpha_if_checked = 1.0f)
{
    tr.draw_rect(x, y, size, size, style.slider_track[0], style.slider_track[1], style.slider_track[2]);
    if (checked) {
        tr.draw_rect(x + 2, y + 2, size - 4, size - 4,
                     style.accent[0], style.accent[1], style.accent[2], alpha_if_checked);
    }
}

// --- Port type compatibility ---

inline bool is_control_type(VividPortType t) {
    return vivid_is_control_type(t) != 0;
}

inline bool is_numeric_type(VividPortType t) {
    return t == VIVID_PORT_SIGNAL || t == VIVID_PORT_LANE_ARRAY || t == VIVID_PORT_AUDIO;
}

inline bool port_type_compatible(VividPortType a, VividPortType b) {
    return vivid_port_type_compatible(a, b) != 0;
}

// --- Create operator modal helpers ---

struct PortTypeEntry {
    const char*   label;
    VividPortType type;
};

inline const std::vector<PortTypeEntry>& port_types_for_env(int env_sel) {
    static const std::vector<PortTypeEntry> control_types = {
        {"float",         VIVID_PORT_SIGNAL},
        {"lane_array",    VIVID_PORT_LANE_ARRAY},
        {"string",        VIVID_PORT_STRING},
        {"string_spread", VIVID_PORT_STRING_SPREAD},
    };
    static const std::vector<PortTypeEntry> audio_types = {
        {"audio", VIVID_PORT_AUDIO},
    };
    static const std::vector<PortTypeEntry> gpu_types = {
        {"texture", VIVID_PORT_TEXTURE},
    };
    switch (env_sel) {
        case 1:  return audio_types;
        case 2:  return gpu_types;
        default: return control_types;
    }
}

inline const char* param_type_labels[] = { "float", "int", "bool", "file", "text" };
inline constexpr int kParamTypeCount = 5;

// --- Preset menu tree (for hierarchical submenus) ---

struct PresetMenuNode {
    std::string label;       // segment name, e.g. "Bass" or "Deep Sub"
    std::string full_path;   // full slash-delimited name (empty for folders)
    bool is_folder = false;
    bool is_factory = false;
    std::vector<PresetMenuNode> children;
};

// Build a hierarchical menu tree from flat preset name lists.
// Names containing "/" are split into folder levels (e.g. "Bass/Deep Sub" →
// folder "Bass" → leaf "Deep Sub"). Factory and user presets merge into the
// same tree, distinguished by is_factory on leaf nodes.
inline std::vector<PresetMenuNode> build_preset_menu_tree(
    const std::vector<std::string>& factory_names,
    const std::vector<std::string>& user_names)
{
    std::vector<PresetMenuNode> root;

    auto insert = [&](const std::string& full_name, bool is_factory) {
        std::vector<PresetMenuNode>* level = &root;
        size_t pos = 0;
        // Walk/create intermediate folder nodes for each segment before the last
        while (true) {
            size_t slash = full_name.find('/', pos);
            if (slash == std::string::npos) break;  // remaining part is the leaf
            std::string seg = full_name.substr(pos, slash - pos);
            if (seg.empty()) { pos = slash + 1; continue; }
            // Find or create folder
            PresetMenuNode* folder = nullptr;
            for (auto& child : *level) {
                if (child.is_folder && child.label == seg) { folder = &child; break; }
            }
            if (!folder) {
                level->push_back({seg, "", true, false, {}});
                folder = &level->back();
            }
            level = &folder->children;
            pos = slash + 1;
        }
        // Leaf node
        std::string leaf_label = full_name.substr(pos);
        if (leaf_label.empty()) return;
        level->push_back({leaf_label, full_name, false, is_factory, {}});
    };

    for (const auto& n : factory_names) insert(n, true);
    for (const auto& n : user_names)    insert(n, false);

    // Sort each level: folders first (alpha), then leaves (alpha)
    std::function<void(std::vector<PresetMenuNode>&)> sort_level;
    sort_level = [&](std::vector<PresetMenuNode>& nodes) {
        std::sort(nodes.begin(), nodes.end(), [](const PresetMenuNode& a, const PresetMenuNode& b) {
            if (a.is_folder != b.is_folder) return a.is_folder > b.is_folder;  // folders first
            return a.label < b.label;
        });
        for (auto& node : nodes) {
            if (node.is_folder) sort_level(node.children);
        }
    };
    sort_level(root);

    return root;
}

} // namespace vivid::ui
