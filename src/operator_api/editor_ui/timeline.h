#pragma once
//
// Timeline and span-editor helpers extracted from the MidiClip editor.

#include "operator_api/editor_ui/drawing.h"
#include "operator_api/editor_ui/viewport.h"
#include "operator_api/types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vivid::ui {

enum class Orientation { Horizontal, Vertical };

struct ScrollbarState {
    bool dragging_thumb = false;
    double drag_start_view = 0.0;
    float drag_start_mouse = 0.0f;
};

struct ScrollbarResult {
    bool changed = false;
    bool hovered = false;
    bool dragging = false;
    Rect thumb{};
};

inline Rect scrollbar_thumb_rect(Rect track, Orientation orientation,
                                 double content_size, double view_size,
                                 double view_start, double content_min = 0.0,
                                 float min_thumb = 20.0f) {
    const float track_len = (orientation == Orientation::Horizontal) ? track.w : track.h;
    if (content_size <= 0.0 || view_size >= content_size || track_len <= 0.0f) return {};
    const float thumb_len = std::max(min_thumb,
        track_len * static_cast<float>(view_size / content_size));
    const float travel = std::max(0.0f, track_len - thumb_len);
    const double scroll_span = std::max(0.0, content_size - view_size);
    const float frac = (scroll_span > 1e-12)
        ? static_cast<float>((view_start - content_min) / scroll_span) : 0.0f;
    if (orientation == Orientation::Horizontal) {
        return Rect{track.x + travel * std::clamp(frac, 0.0f, 1.0f),
                    track.y, thumb_len, track.h};
    }
    return Rect{track.x, track.y + travel * std::clamp(frac, 0.0f, 1.0f),
                track.w, thumb_len};
}

inline ScrollbarResult ui_scrollbar(VividEditorContext& ctx, Rect track,
                                    Orientation orientation,
                                    Viewport1D* viewport,
                                    ScrollbarState* state,
                                    VividColor track_color = {0.10f, 0.10f, 0.12f, 1.0f},
                                    VividColor thumb_color = {0.4f, 0.4f, 0.45f, 0.7f}) {
    ScrollbarResult out;
    if (!viewport || !state) return out;
    clamp_viewport(viewport);
    draw_scrollbar_track(ctx.draw, ctx.draw.opaque, track, track_color);
    out.thumb = scrollbar_thumb_rect(track, orientation,
                                     viewport->content_size(),
                                     viewport->view_size,
                                     viewport->view_start,
                                     viewport->content_min);
    if (out.thumb.w <= 0.0f || out.thumb.h <= 0.0f) return out;

    const auto& mouse = ctx.mouse;
    out.hovered = out.thumb.contains(mouse.x, mouse.y);
    if (out.hovered && mouse.left_clicked) {
        state->dragging_thumb = true;
        state->drag_start_view = viewport->view_start;
        state->drag_start_mouse = (orientation == Orientation::Horizontal) ? mouse.x : mouse.y;
    }
    if (state->dragging_thumb && mouse.left_down) {
        const float track_len = (orientation == Orientation::Horizontal) ? track.w : track.h;
        const float thumb_len = (orientation == Orientation::Horizontal) ? out.thumb.w : out.thumb.h;
        const float travel = std::max(1.0f, track_len - thumb_len);
        const float now = (orientation == Orientation::Horizontal) ? mouse.x : mouse.y;
        const double scroll_span = std::max(0.0, viewport->content_size() - viewport->view_size);
        viewport->view_start = state->drag_start_view
            + static_cast<double>(now - state->drag_start_mouse) * (scroll_span / travel);
        clamp_viewport(viewport);
        out.changed = true;
        out.dragging = true;
        out.thumb = scrollbar_thumb_rect(track, orientation,
                                         viewport->content_size(),
                                         viewport->view_size,
                                         viewport->view_start,
                                         viewport->content_min);
    } else if (state->dragging_thumb) {
        state->dragging_thumb = false;
    }
    draw_scrollbar_thumb(ctx.draw, ctx.draw.opaque, out.thumb,
                         out.hovered || out.dragging, thumb_color);
    return out;
}

struct TimelineTick {
    double value = 0.0;
    bool major = false;
};

template <typename Fn>
inline void for_each_timeline_tick(VisibleRange range, double step,
                                   double major_every, Fn&& fn) {
    if (step <= 0.0 || range.end < range.start) return;
    const int first = static_cast<int>(std::floor(range.start / step)) - 1;
    const int last = static_cast<int>(std::ceil(range.end / step)) + 1;
    for (int i = first; i <= last; ++i) {
        const double v = static_cast<double>(i) * step;
        if (v < range.start - step || v > range.end + step) continue;
        bool major = false;
        if (major_every > 0.0) {
            const double q = v / major_every;
            major = std::fabs(q - std::round(q)) < 1e-6;
        }
        fn(TimelineTick{v, major});
    }
}

inline double timeline_grid_step_for_pixels(const Viewport1D& viewport,
                                            double step,
                                            double major_every,
                                            float min_pixel_spacing = 3.0f) {
    if (step <= 0.0) return step;
    const double pixels_per_unit = std::fabs(viewport.pixels_per_unit());
    if (pixels_per_unit <= 1e-12) return step;

    const double requested_px = step * pixels_per_unit;
    if (requested_px >= static_cast<double>(min_pixel_spacing)) return step;

    if (major_every > step
        && major_every * pixels_per_unit >= static_cast<double>(min_pixel_spacing)) {
        return major_every;
    }

    const double base = (major_every > step) ? major_every : step;
    const double base_px = base * pixels_per_unit;
    if (base_px <= 1e-12) return step;
    const double multiplier = std::ceil(static_cast<double>(min_pixel_spacing) / base_px);
    return base * std::max(1.0, multiplier);
}

inline void draw_timeline_grid(VividDrawAPI& d, void* o, Rect bounds,
                               const Viewport1D& viewport,
                               double step, double major_every,
                               VividColor color,
                               float minor_alpha = 0.15f,
                               float major_alpha = 0.4f,
                               float min_pixel_spacing = 3.0f) {
    const double effective_step = timeline_grid_step_for_pixels(
        viewport, step, major_every, min_pixel_spacing);
    for_each_timeline_tick(viewport.visible_range(), effective_step, major_every,
        [&](TimelineTick tick) {
            const float x = viewport.world_to_screen(tick.value);
            if (x < bounds.x - 1.0f || x > bounds.x + bounds.w + 1.0f) return;
            VividColor c = color;
            c.a *= tick.major ? major_alpha : minor_alpha;
            if (d.draw_rect) d.draw_rect(o, x, bounds.y, 1.0f, bounds.h, c);
        });
}

enum class RangeDragMode { None, Sweep, Left, Right, Body };

struct RangeDragState {
    RangeDragMode mode = RangeDragMode::None;
    float drag_start_x = 0.0f;
    double origin_start = 0.0;
    double origin_end = 0.0;
    bool active() const { return mode != RangeDragMode::None; }
};

struct RangeHit {
    RangeDragMode zone = RangeDragMode::None;
};

inline RangeHit hit_test_range(Rect bounds, const Viewport1D& viewport,
                               double start, double end, float handle_px = 8.0f,
                               float x = 0.0f, float y = 0.0f) {
    if (!bounds.contains(x, y) || end <= start) return {};
    const float lx = viewport.world_to_screen(start);
    const float rx = viewport.world_to_screen(end);
    if (std::fabs(x - lx) < handle_px) return {RangeDragMode::Left};
    if (std::fabs(x - rx) < handle_px) return {RangeDragMode::Right};
    if (x > lx && x < rx) return {RangeDragMode::Body};
    return {};
}

inline void begin_range_drag(RangeDragState* state, RangeDragMode mode,
                             float mouse_x, double start, double end,
                             double anchor = 0.0) {
    if (!state) return;
    state->mode = mode;
    state->drag_start_x = mouse_x;
    state->origin_start = (mode == RangeDragMode::Sweep) ? anchor : start;
    state->origin_end = end;
}

inline bool update_range_drag(RangeDragState* state, const Viewport1D& viewport,
                              float mouse_x, double content_min,
                              double content_max, double min_len,
                              double* start, double* end) {
    if (!state || !state->active() || !start || !end) return false;
    const double mouse_world = viewport.screen_to_world(mouse_x);
    double ns = *start;
    double ne = *end;
    if (state->mode == RangeDragMode::Sweep) {
        ns = std::min(state->origin_start, mouse_world);
        ne = std::max(state->origin_start, mouse_world);
    } else if (state->mode == RangeDragMode::Left) {
        ns = std::clamp(mouse_world, content_min, state->origin_end - min_len);
        ne = state->origin_end;
    } else if (state->mode == RangeDragMode::Right) {
        ns = state->origin_start;
        ne = std::clamp(mouse_world, state->origin_start + min_len, content_max);
    } else if (state->mode == RangeDragMode::Body) {
        const double delta = static_cast<double>(mouse_x - state->drag_start_x)
            * viewport.units_per_pixel();
        const double len = state->origin_end - state->origin_start;
        ns = std::clamp(state->origin_start + delta, content_min, content_max - len);
        ne = ns + len;
    }
    ns = std::clamp(ns, content_min, content_max);
    ne = std::clamp(ne, content_min, content_max);
    if (ne - ns < min_len) return false;
    const bool changed = (std::fabs(*start - ns) > 1e-9)
        || (std::fabs(*end - ne) > 1e-9);
    *start = ns;
    *end = ne;
    return changed;
}

struct BoxSelectState {
    bool active = false;
    bool additive = false;
    double start_x = 0.0;
    double start_y = 0.0;
};

struct BoxSelectRect {
    bool active = false;
    bool additive = false;
    double x0 = 0.0, x1 = 0.0;
    double y0 = 0.0, y1 = 0.0;
};

inline void begin_box_select(BoxSelectState* state, double x, double y, bool additive) {
    if (!state) return;
    state->active = true;
    state->additive = additive;
    state->start_x = x;
    state->start_y = y;
}

inline BoxSelectRect update_box_select(const BoxSelectState& state,
                                       double x, double y) {
    if (!state.active) return {};
    return BoxSelectRect{
        true, state.additive,
        std::min(state.start_x, x), std::max(state.start_x, x),
        std::min(state.start_y, y), std::max(state.start_y, y)
    };
}

inline void end_box_select(BoxSelectState* state) {
    if (state) state->active = false;
}

enum class SpanHitZone { None, Body, ResizeRight };

struct SpanHitResult {
    int index = -1;
    SpanHitZone zone = SpanHitZone::None;
};

template <typename Items, typename StartFn, typename EndFn, typename MatchFn>
inline SpanHitResult hit_test_spans(const Items& indices,
                                    StartFn&& start_fn,
                                    EndFn&& end_fn,
                                    MatchFn&& matches_fn,
                                    const Viewport1D& x_view,
                                    double mouse_world,
                                    float mouse_x,
                                    float resize_px = 8.0f) {
    for (int i : indices) {
        if (!matches_fn(i)) continue;
        const double start = start_fn(i);
        const double end = end_fn(i);
        if (mouse_world < start || mouse_world >= end) continue;
        const float right_px = x_view.world_to_screen(end);
        if (!std::isfinite(right_px)) continue;
        const SpanHitZone zone = (mouse_x >= right_px - resize_px)
            ? SpanHitZone::ResizeRight : SpanHitZone::Body;
        return {i, zone};
    }
    return {};
}

} // namespace vivid::ui
