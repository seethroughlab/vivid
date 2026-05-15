#pragma once
//
// Stateless editor drawing helpers that sit above VividDrawAPI but below
// interactive widgets. These helpers deliberately do not read input.

#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_ui/geometry.h"

#include <algorithm>

namespace vivid::ui {

inline void draw_selection_rect(VividDrawAPI& d, void* o, Rect r,
                                VividColor color,
                                float fill_alpha = 0.12f,
                                float edge_alpha = 0.55f) {
    VividColor fill = color;
    fill.a *= fill_alpha;
    if (d.draw_rect) d.draw_rect(o, r.x, r.y, r.w, r.h, fill);
    VividColor edge = color;
    edge.a *= edge_alpha;
    if (!d.draw_rect) return;
    d.draw_rect(o, r.x, r.y, r.w, 1.0f, edge);
    d.draw_rect(o, r.x, r.y + r.h, r.w, 1.0f, edge);
    d.draw_rect(o, r.x, r.y, 1.0f, r.h, edge);
    d.draw_rect(o, r.x + r.w, r.y, 1.0f, r.h, edge);
}

inline void draw_scrollbar_track(VividDrawAPI& d, void* o, Rect track,
                                 VividColor track_color) {
    if (d.draw_rect) d.draw_rect(o, track.x, track.y, track.w, track.h, track_color);
}

inline void draw_scrollbar_thumb(VividDrawAPI& d, void* o, Rect thumb,
                                 bool hovered,
                                 VividColor thumb_color = {0.4f, 0.4f, 0.45f, 0.7f}) {
    thumb_color.a = hovered ? 1.0f : thumb_color.a;
    if (d.draw_rounded_rect) {
        const float radius = std::min(3.0f, std::min(thumb.w, thumb.h) * 0.5f);
        d.draw_rounded_rect(o, thumb.x, thumb.y, thumb.w, thumb.h, radius, thumb_color);
    } else if (d.draw_rect) {
        d.draw_rect(o, thumb.x, thumb.y, thumb.w, thumb.h, thumb_color);
    }
}

inline void draw_compact_strip_background(VividDrawAPI& d, void* o,
                                          Rect strip, const char* label,
                                          VividColor separator,
                                          VividColor fill,
                                          VividColor label_color,
                                          float separator_h = 1.0f) {
    if (d.draw_rect) {
        if (separator_h > 0.0f)
            d.draw_rect(o, strip.x, strip.y - separator_h, strip.w, separator_h, separator);
        d.draw_rect(o, strip.x, strip.y, strip.w, strip.h, fill);
    }
    if (d.draw_text && label && *label) {
        d.draw_text(o, strip.x + 4.0f, strip.y + 2.0f, label, label_color, 0.75f);
    }
}

inline void draw_playhead_line(VividDrawAPI& d, void* o, float x, float y,
                               float h, VividColor color,
                               float w = 2.0f) {
    if (d.draw_rect) d.draw_rect(o, x, y, w, h, color);
}

inline void draw_range_brace(VividDrawAPI& d, void* o, Rect bounds,
                             float x0, float x1,
                             VividColor band,
                             VividColor handle,
                             float handle_w = 4.0f) {
    const float lx = std::clamp(std::min(x0, x1), bounds.x, bounds.x + bounds.w);
    const float rx = std::clamp(std::max(x0, x1), bounds.x, bounds.x + bounds.w);
    if (d.draw_rect && rx > lx) {
        d.draw_rect(o, lx, bounds.y, rx - lx, bounds.h, band);
    }
    if (d.draw_rect) {
        d.draw_rect(o, lx - handle_w * 0.5f, bounds.y, handle_w, bounds.h, handle);
        d.draw_rect(o, rx - handle_w * 0.5f, bounds.y, handle_w, bounds.h, handle);
    }
}

} // namespace vivid::ui
