#pragma once
//
// World/screen viewport helpers for timeline, piano-roll, curve, and tracker
// editors. These are pure math; callers own persistence and commands.

#include "operator_api/editor_ui/geometry.h"

#include <algorithm>
#include <cmath>

namespace vivid::ui {

struct VisibleRange {
    double start = 0.0;
    double end = 0.0;
};

struct Viewport1D {
    double content_min = 0.0;
    double content_max = 1.0;
    float screen_min = 0.0f;
    float screen_size = 1.0f;
    double view_start = 0.0;
    double view_size = 1.0;

    double content_size() const {
        return std::max(0.0, content_max - content_min);
    }
    double max_view_start() const {
        return content_min + std::max(0.0, content_size() - view_size);
    }
    double pixels_per_unit() const {
        return (view_size > 1e-12) ? static_cast<double>(screen_size) / view_size : 0.0;
    }
    double units_per_pixel() const {
        return (screen_size > 1e-6f) ? view_size / static_cast<double>(screen_size) : 0.0;
    }
    VisibleRange visible_range() const {
        return {view_start, view_start + view_size};
    }
    float world_to_screen(double value) const {
        return screen_min + static_cast<float>((value - view_start) * pixels_per_unit());
    }
    double screen_to_world(float px) const {
        return view_start + static_cast<double>(px - screen_min) * units_per_pixel();
    }
};

inline void clamp_viewport(Viewport1D* v, double min_view_size = 1e-6) {
    if (!v) return;
    if (v->content_max < v->content_min) std::swap(v->content_min, v->content_max);
    const double size = v->content_size();
    v->view_size = std::clamp(v->view_size, min_view_size, std::max(min_view_size, size));
    v->view_start = std::clamp(v->view_start, v->content_min, v->max_view_start());
}

inline void pan_viewport(Viewport1D* v, double delta_units) {
    if (!v) return;
    v->view_start += delta_units;
    clamp_viewport(v);
}

inline void zoom_viewport_at(Viewport1D* v, float anchor_screen,
                             double factor,
                             double min_view_size) {
    if (!v || factor <= 0.0) return;
    clamp_viewport(v, min_view_size);
    const double anchor_world = v->screen_to_world(anchor_screen);
    const double old_size = v->view_size;
    const double new_size = std::clamp(old_size / factor,
                                       min_view_size,
                                       std::max(min_view_size, v->content_size()));
    const double anchor_frac = (old_size > 1e-12)
        ? (anchor_world - v->view_start) / old_size : 0.0;
    v->view_size = new_size;
    v->view_start = anchor_world - anchor_frac * new_size;
    clamp_viewport(v, min_view_size);
}

struct Viewport2D {
    Viewport1D x;
    Viewport1D y;
};

inline void clamp_viewport(Viewport2D* v, double min_x_size = 1e-6,
                           double min_y_size = 1e-6) {
    if (!v) return;
    clamp_viewport(&v->x, min_x_size);
    clamp_viewport(&v->y, min_y_size);
}

struct SpanItem {
    double start = 0.0;
    double end = 0.0;
    int payload = -1;
};

inline bool span_intersects_visible(double start, double end,
                                    VisibleRange visible) {
    return end >= visible.start && start <= visible.end;
}

} // namespace vivid::ui
