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

// --- Port type compatibility ---

inline bool is_control_type(VividPortType t) {
    return t == VIVID_PORT_CONTROL_FLOAT || t == VIVID_PORT_CONTROL_INT ||
           t == VIVID_PORT_CONTROL_BOOL  || t == VIVID_PORT_CONTROL_SPREAD;
}

inline bool is_numeric_type(VividPortType t) {
    return t == VIVID_PORT_CONTROL_FLOAT || t == VIVID_PORT_CONTROL_INT ||
           t == VIVID_PORT_CONTROL_SPREAD || t == VIVID_PORT_AUDIO_FLOAT;
}

inline bool port_type_compatible(VividPortType a, VividPortType b) {
    if (a == VIVID_PORT_GPU_TEXTURE)   return b == VIVID_PORT_GPU_TEXTURE;
    if (a == VIVID_PORT_DATA)          return b == VIVID_PORT_DATA;
    if (a == VIVID_PORT_AUDIO_FLOAT)   return b == VIVID_PORT_AUDIO_FLOAT;
    return is_control_type(a) && is_control_type(b);
}

} // namespace vivid::ui
