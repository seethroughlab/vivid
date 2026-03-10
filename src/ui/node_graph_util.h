#pragma once

#include "ui/renderer_2d.h"
#include "ui/ui_style.h"
#include "operator_api/types.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

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
                             float br, float bg, float bb, float ba = 0.6f) {
    tr.draw_rect(x, y, w, 1, br, bg, bb, ba);           // top
    tr.draw_rect(x, y + h - 1, w, 1, br, bg, bb, ba);   // bottom
    tr.draw_rect(x, y, 1, h, br, bg, bb, ba);            // left
    tr.draw_rect(x + w - 1, y, 1, h, br, bg, bb, ba);   // right
}

// --- Popup background (rect + accent bar at top) ---

inline void draw_popup_bg(Renderer2D& tr, const UIStyle& style,
                          float x, float y, float w, float h) {
    tr.draw_rect(x, y, w, h,
                 style.popup_bg[0], style.popup_bg[1], style.popup_bg[2], style.popup_bg[3]);
    tr.draw_rect(x, y, w, 1,
                 style.accent[0], style.accent[1], style.accent[2]);
}

// --- Editing text field ---

// Draws the active editing state of a text field:
//   - 1px accent border (outset by 1px on all sides)
//   - input_field_bg filled rect
//   - buffer text with blinking cursor
// Call this only in the is_editing branch. The caller handles non-editing display.
inline void draw_editing_text_field(
    Renderer2D& tr, const UIStyle& style,
    float x, float y, float w, float h,
    const std::string& buffer, bool blink_on,
    float pad_left = 4.0f, float pad_top = 2.0f)
{
    tr.draw_rect(x - 1, y - 1, w + 2, h + 2,
                 style.accent[0], style.accent[1], style.accent[2]);
    tr.draw_rect(x, y, w, h,
                 style.input_field_bg[0], style.input_field_bg[1], style.input_field_bg[2]);
    std::string display = buffer + (blink_on ? "_" : " ");
    tr.draw_text(x + pad_left, y + pad_top, display.c_str(),
                 style.bright_text[0], style.bright_text[1], style.bright_text[2]);
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
    return t == VIVID_PORT_CONTROL_FLOAT || t == VIVID_PORT_CONTROL_INT ||
           t == VIVID_PORT_CONTROL_BOOL  || t == VIVID_PORT_CONTROL_SPREAD ||
           t == VIVID_PORT_CONTROL_STRING || t == VIVID_PORT_CONTROL_STRING_SPREAD;
}

inline bool is_numeric_type(VividPortType t) {
    return t == VIVID_PORT_CONTROL_FLOAT || t == VIVID_PORT_CONTROL_INT ||
           t == VIVID_PORT_CONTROL_SPREAD || t == VIVID_PORT_AUDIO_FLOAT;
}

inline bool port_type_compatible(VividPortType a, VividPortType b) {
    if (a == VIVID_PORT_GPU_TEXTURE)          return b == VIVID_PORT_GPU_TEXTURE;
    if (a == VIVID_PORT_DATA)                 return b == VIVID_PORT_DATA;
    if (a == VIVID_PORT_MEDIA_STREAM)         return b == VIVID_PORT_MEDIA_STREAM;
    if (a == VIVID_PORT_MEDIA_CLOCK)          return b == VIVID_PORT_MEDIA_CLOCK;
    if (a == VIVID_PORT_MIDI)                 return b == VIVID_PORT_MIDI;
    if (a == VIVID_PORT_CONTROL_STRING)       return b == VIVID_PORT_CONTROL_STRING;
    if (a == VIVID_PORT_CONTROL_STRING_SPREAD) return b == VIVID_PORT_CONTROL_STRING_SPREAD;
    if (a == VIVID_PORT_AUDIO_FLOAT)          return b == VIVID_PORT_AUDIO_FLOAT;
    if (a == VIVID_PORT_GPU_BUFFER)           return b == VIVID_PORT_GPU_BUFFER;
    if (a == VIVID_PORT_GPU_MESH)             return b == VIVID_PORT_GPU_MESH;
    if (a == VIVID_PORT_GPU_COMPUTE)          return b == VIVID_PORT_GPU_COMPUTE;
    return is_control_type(a) && is_control_type(b);
}

} // namespace vivid::ui
