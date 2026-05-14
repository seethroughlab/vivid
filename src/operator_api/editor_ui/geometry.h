#pragma once
//
// Basic editor-window geometry and immediate-mode layout helpers.
// Header-only so operator editors can include this without link deps.

#include <algorithm>
#include <utility>

namespace vivid::ui {

// Half-open axis-aligned rectangle in editor-window pixel space.
struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

struct LayoutCursor {
    Rect bounds;
    float pad = 0.0f;
    float gap = 4.0f;

    float cursor_x = 0.0f;
    float cursor_y = 0.0f;
    float remaining_w = 0.0f;
    float remaining_h = 0.0f;
};

inline LayoutCursor ui_layout(Rect outer, float pad = 0.0f, float gap = 4.0f) {
    LayoutCursor c{};
    c.bounds = outer;
    c.pad = pad;
    c.gap = gap;
    c.cursor_x = outer.x + pad;
    c.cursor_y = outer.y + pad;
    c.remaining_w = std::max(0.0f, outer.w - 2.0f * pad);
    c.remaining_h = std::max(0.0f, outer.h - 2.0f * pad);
    return c;
}

inline Rect ui_row(LayoutCursor& c, float height, float override_gap = -1.0f) {
    const float gap = (override_gap >= 0.0f) ? override_gap : c.gap;
    const float actual_h = std::min(height, c.remaining_h);
    Rect r{c.cursor_x, c.cursor_y, c.remaining_w, actual_h};
    const float consumed = actual_h + gap;
    c.cursor_y += consumed;
    c.remaining_h = std::max(0.0f, c.remaining_h - consumed);
    return r;
}

inline Rect ui_column(LayoutCursor& c, float width, float override_gap = -1.0f) {
    const float gap = (override_gap >= 0.0f) ? override_gap : c.gap;
    const float actual_w = std::min(width, c.remaining_w);
    Rect r{c.cursor_x, c.cursor_y, actual_w, c.remaining_h};
    const float consumed = actual_w + gap;
    c.cursor_x += consumed;
    c.remaining_w = std::max(0.0f, c.remaining_w - consumed);
    return r;
}

inline Rect ui_pad(Rect r, float inset) {
    return Rect{
        r.x + inset, r.y + inset,
        std::max(0.0f, r.w - 2.0f * inset),
        std::max(0.0f, r.h - 2.0f * inset)
    };
}

inline std::pair<Rect, Rect> ui_split_h(Rect r, float fraction, float gap = 0.0f) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    const float available = std::max(0.0f, r.w - gap);
    const float lw = available * fraction;
    const float rw = available - lw;
    return {
        Rect{r.x, r.y, lw, r.h},
        Rect{r.x + lw + gap, r.y, rw, r.h}
    };
}

inline std::pair<Rect, Rect> ui_split_v(Rect r, float fraction, float gap = 0.0f) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    const float available = std::max(0.0f, r.h - gap);
    const float th = available * fraction;
    const float bh = available - th;
    return {
        Rect{r.x, r.y, r.w, th},
        Rect{r.x, r.y + th + gap, r.w, bh}
    };
}

} // namespace vivid::ui
