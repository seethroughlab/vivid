#pragma once

#include "operator_api/types.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vivid::draw_ui {

inline VividColor with_alpha(VividColor c, float a) {
    c.a = a;
    return c;
}

inline VividColor scale_rgb(VividColor c, float s) {
    c.r *= s;
    c.g *= s;
    c.b *= s;
    return c;
}

inline float text_width_or(VividDrawAPI& d, void* o, const char* text, float scale, float fallback) {
    return (d.text_width && text) ? d.text_width(o, text, scale) : fallback;
}

inline float line_height_or(VividDrawAPI& d, void* o, float fallback) {
    return d.line_height ? d.line_height(o) : fallback;
}

inline void draw_text_aligned(VividDrawAPI& d, void* o,
                              float x, float y, float w,
                              const char* text, VividColor color,
                              float scale = 1.0f,
                              float align = 0.0f) {
    if (!d.draw_text || !text) return;
    float tw = text_width_or(d, o, text, scale, std::strlen(text) * 6.0f * scale);
    float tx = x + std::max(0.0f, w - tw) * std::clamp(align, 0.0f, 1.0f);
    d.draw_text(o, tx, y, text, color, scale);
}

inline void draw_panel(VividDrawAPI& d, void* o,
                       float x, float y, float w, float h,
                       VividColor fill,
                       VividColor border = {0.0f, 0.0f, 0.0f, 0.0f},
                       float radius = 0.0f,
                       float border_thickness = 1.0f) {
    if (radius > 0.0f && d.draw_rounded_rect) {
        d.draw_rounded_rect(o, x, y, w, h, radius, fill);
    } else if (d.draw_rect) {
        d.draw_rect(o, x, y, w, h, fill);
    }

    if (border.a <= 0.0f || !d.draw_rect) return;
    d.draw_rect(o, x, y, w, border_thickness, border);
    d.draw_rect(o, x, y + h - border_thickness, w, border_thickness, border);
    d.draw_rect(o, x, y, border_thickness, h, border);
    d.draw_rect(o, x + w - border_thickness, y, border_thickness, h, border);
}

inline void draw_section_header(VividDrawAPI& d, void* o,
                                float x, float y, float w,
                                const char* label,
                                VividColor text,
                                VividColor divider = {0.0f, 0.0f, 0.0f, 0.0f},
                                float scale = 0.85f) {
    draw_text_aligned(d, o, x, y, w, label, text, scale, 0.0f);
    if (divider.a > 0.0f && d.draw_rect) {
        float lh = line_height_or(d, o, 12.0f) * scale;
        d.draw_rect(o, x, y + lh + 2.0f, w, 1.0f, divider);
    }
}

inline void draw_value_badge(VividDrawAPI& d, void* o,
                             float x, float y, float w, float h,
                             const char* text,
                             VividColor fill,
                             VividColor color,
                             float radius = 3.0f,
                             float scale = 0.85f) {
    draw_panel(d, o, x, y, w, h, fill, {0, 0, 0, 0}, radius);
    float lh = line_height_or(d, o, 12.0f) * scale;
    draw_text_aligned(d, o, x, y + std::max(0.0f, (h - lh) * 0.5f - 1.0f), w, text, color, scale, 0.5f);
}

inline void draw_button(VividDrawAPI& d, void* o,
                        float x, float y, float w, float h,
                        const char* text,
                        bool active,
                        VividColor fill,
                        VividColor active_fill,
                        VividColor text_color,
                        float radius = 3.0f,
                        float scale = 1.0f) {
    draw_panel(d, o, x, y, w, h, active ? active_fill : fill, {0, 0, 0, 0}, radius);
    float lh = line_height_or(d, o, 12.0f) * scale;
    draw_text_aligned(d, o, x, y + std::max(0.0f, (h - lh) * 0.5f - 1.0f), w, text, text_color, scale, 0.5f);
}

inline void draw_tab_strip(VividDrawAPI& d, void* o,
                           float x, float y,
                           float tab_w, float tab_h,
                           const char* const* labels,
                           int count,
                           int active_idx,
                           VividColor inactive_text,
                           VividColor active_text,
                           VividColor active_bg,
                           VividColor accent) {
    for (int i = 0; i < count; ++i) {
        float tx = x + static_cast<float>(i) * tab_w;
        bool active = (i == active_idx);
        if (active) {
            if (d.draw_rect) d.draw_rect(o, tx, y, tab_w, tab_h, active_bg);
            if (d.draw_rect) d.draw_rect(o, tx, y + tab_h - 2.0f, tab_w, 2.0f, accent);
        }
        draw_text_aligned(d, o, tx + 8.0f, y + 3.0f, tab_w - 16.0f, labels[i],
                          active ? active_text : inactive_text, 1.0f, 0.0f);
    }
}

inline void draw_text_row(VividDrawAPI& d, void* o,
                          float x, float y, float w,
                          const char* label, const char* value,
                          VividColor label_color,
                          VividColor value_color,
                          float scale = 0.9f) {
    draw_text_aligned(d, o, x, y, w * 0.45f, label, label_color, scale, 0.0f);
    draw_text_aligned(d, o, x + w * 0.45f, y, w * 0.55f, value, value_color, scale, 1.0f);
}

inline void draw_grid_cell(VividDrawAPI& d, void* o,
                           float x, float y, float w, float h,
                           const char* text,
                           VividColor fill,
                           VividColor text_color,
                           float radius = 2.0f,
                           float scale = 1.0f) {
    draw_panel(d, o, x, y, w, h, fill, {0, 0, 0, 0}, radius);
    float lh = line_height_or(d, o, 12.0f) * scale;
    draw_text_aligned(d, o, x, y + std::max(0.0f, (h - lh) * 0.5f - 1.0f), w, text, text_color, scale, 0.5f);
}

inline void draw_selection_highlight(VividDrawAPI& d, void* o,
                                     float x, float y, float w, float h,
                                     VividColor color,
                                     float alpha = 0.15f) {
    color.a *= alpha;
    if (d.draw_rect) d.draw_rect(o, x, y, w, h, color);
}

inline void draw_clipped_text_box(VividDrawAPI& d, void* o,
                                  float x, float y, float w, float h,
                                  const char* text,
                                  VividColor fill,
                                  VividColor text_color,
                                  float radius = 2.0f,
                                  float scale = 1.0f,
                                  float align = 0.0f,
                                  float pad = 4.0f) {
    draw_panel(d, o, x, y, w, h, fill, {0, 0, 0, 0}, radius);
    if (d.push_clip_rect) d.push_clip_rect(o, x, y, w, h);
    float lh = line_height_or(d, o, 12.0f) * scale;
    draw_text_aligned(d, o, x + pad, y + std::max(0.0f, (h - lh) * 0.5f - 1.0f),
                      std::max(0.0f, w - pad * 2.0f), text, text_color, scale, align);
    if (d.pop_clip_rect) d.pop_clip_rect(o);
}

inline void draw_scroll_panel_begin(VividDrawAPI& d, void* o,
                                    float x, float y, float w, float h,
                                    VividColor fill,
                                    float radius = 0.0f) {
    draw_panel(d, o, x, y, w, h, fill, {0, 0, 0, 0}, radius);
    if (d.push_clip_rect) d.push_clip_rect(o, x, y, w, h);
}

inline void draw_scroll_panel_end(VividDrawAPI& d, void* o) {
    (void)o;
    if (d.pop_clip_rect) d.pop_clip_rect(o);
}

// --- Meter (fill bar) ---

enum class MeterOrientation { Horizontal, Vertical };

// Horizontal: fill grows left→right (0 = empty at the left edge, 1 = full).
// Vertical:   fill grows bottom→top  (0 = empty at the bottom, 1 = full).
inline void draw_meter(VividDrawAPI& d, void* o,
                       float x, float y, float w, float h,
                       float fill_frac,
                       VividColor fill_color,
                       VividColor track_color,
                       MeterOrientation orientation = MeterOrientation::Horizontal,
                       float radius = 2.0f) {
    const float frac = std::clamp(fill_frac, 0.0f, 1.0f);
    draw_panel(d, o, x, y, w, h, track_color, {0, 0, 0, 0}, radius);
    if (frac <= 0.0f || w <= 0.0f || h <= 0.0f) return;
    if (orientation == MeterOrientation::Horizontal) {
        const float fw = w * frac;
        if (fw <= 0.0f) return;
        if (radius > 0.0f && d.draw_rounded_rect) {
            d.draw_rounded_rect(o, x, y, fw, h, radius, fill_color);
        } else if (d.draw_rect) {
            d.draw_rect(o, x, y, fw, h, fill_color);
        }
    } else {
        const float fh = h * frac;
        if (fh <= 0.0f) return;
        if (radius > 0.0f && d.draw_rounded_rect) {
            d.draw_rounded_rect(o, x, y + (h - fh), w, fh, radius, fill_color);
        } else if (d.draw_rect) {
            d.draw_rect(o, x, y + (h - fh), w, fh, fill_color);
        }
    }
}

// Side-panel-style slider readout: `label` on the left (≈35% of w), a
// track-with-fill meter in the middle (≈50%), and a numeric `value` string
// on the right (≈15%). Caller renders — this helper does NOT handle clicks.
// Used by editor side panels where the operator owns the hit-testing.
inline void draw_labeled_slider_readonly(VividDrawAPI& d, void* o,
                                         float x, float y, float w, float h,
                                         const char* label,
                                         float value, float min_v, float max_v,
                                         VividColor label_color,
                                         VividColor value_color,
                                         VividColor fill_color,
                                         VividColor track_color,
                                         float scale = 0.9f) {
    const float span = max_v - min_v;
    const float frac = (span > 1e-6f)
        ? std::clamp((value - min_v) / span, 0.0f, 1.0f) : 0.0f;

    const float label_w = w * 0.35f;
    const float value_w = w * 0.15f;
    const float meter_x = x + label_w + 4.0f;
    const float meter_w = std::max(0.0f, w - label_w - value_w - 8.0f);

    draw_text_aligned(d, o, x, y, label_w, label, label_color, scale, 0.0f);
    if (meter_w > 0.0f)
        draw_meter(d, o, meter_x, y + (h - 8.0f) * 0.5f, meter_w, 8.0f,
                   frac, fill_color, track_color,
                   MeterOrientation::Horizontal, 2.0f);

    // Compact numeric readout (1 decimal for [0,1] range values, int otherwise).
    char buf[24];
    if (span <= 1.0f + 1e-6f) {
        std::snprintf(buf, sizeof(buf), "%.2f", value);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f", value);
    }
    draw_text_aligned(d, o, x + w - value_w, y, value_w, buf, value_color,
                      scale, 1.0f);
}

} // namespace vivid::draw_ui
