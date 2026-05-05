#include "mseg.h"
#include "mseg_editor_shared.h"
#include "operator_api/draw_ui_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace mseg_ed {

// GLFW codes — operators don't link GLFW directly (mirrors the Tracker and
// DrumSequencer patterns).
constexpr int kGlfwPress = 1;
constexpr int kGlfwModShift = 0x0001;

constexpr int kKeySpace        = 32;
constexpr int kKeyLeftBracket  = 91;
constexpr int kKeyRightBracket = 93;
constexpr int kKeyBackspace    = 259;
constexpr int kKeyDelete       = 261;
constexpr int kKeyRight        = 262;
constexpr int kKeyLeft         = 263;
constexpr int kKeyDown         = 264;
constexpr int kKeyUp           = 265;

constexpr float kPointHandleSize  = 10.0f;
constexpr float kCurveHandleSize  = 6.0f;
constexpr float kPointHitRadius   = 10.0f;
constexpr float kCurveHitRadius   = 8.0f;
constexpr float kDoubleClickMaxDt = 0.3;
constexpr float kDoubleClickMaxDist = 4.0f;

constexpr int kSubSegments = 40;

} // namespace mseg_ed

VividEditorMetadata MSEG::editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 900;
    m.default_height = 560;
    m.min_width      = 640;
    m.min_height     = 360;
    m.title_suffix   = "Envelope";
    return m;
}

void MSEG::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;
    namespace ed = ::mseg_ed;
    namespace me = ::vivid_mseg_editor;

    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;
    const auto& mouse = ctx->mouse;

    // --- Read live point data ---
    me::PointArrays pts;
    int np = me::read_points(ctx->param_values, ctx->param_count, &pts);
    const bool do_loop = (ctx->param_count > 2) && (ctx->param_values[2] > 0.5f);
    const int ls = (ctx->param_count > 3)
        ? std::max(0, std::min(np - 2, static_cast<int>(ctx->param_values[3])))
        : 0;
    const int le = (ctx->param_count > 4)
        ? std::max(ls + 1, std::min(np - 1, static_cast<int>(ctx->param_values[4])))
        : std::min(np - 1, 3);

    // --- Drain key events (Phase 2 ABI: ctx->events) ---
    ctx->wants_keyboard = 1;
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
        if (e.action != ed::kGlfwPress) continue;

        const bool shift = (e.modifiers & ed::kGlfwModShift) != 0;
        const float time_step  = shift ? 0.10f : 0.01f;
        const float value_step = shift ? 0.10f : 0.01f;
        const int sel = std::max(0, std::min(editor_selected_point_, np - 1));

        if (e.key == ed::kKeyLeft || e.key == ed::kKeyRight) {
            const float delta = (e.key == ed::kKeyLeft) ? -time_step : time_step;
            const float new_t = me::clamp_new_time(
                sel, np, pts.times[sel] + delta, pts.times);
            const std::string n = me::param_name_for(me::PointField::Time, sel);
            if (ctx->commands.set_param)
                ctx->commands.set_param(ctx->commands.opaque, n.c_str(), new_t);
        } else if (e.key == ed::kKeyDown || e.key == ed::kKeyUp) {
            const float delta = (e.key == ed::kKeyDown) ? -value_step : value_step;
            const float new_v = std::max(0.0f, std::min(1.0f,
                pts.values[sel] + delta));
            const std::string n = me::param_name_for(me::PointField::Value, sel);
            if (ctx->commands.set_param)
                ctx->commands.set_param(ctx->commands.opaque, n.c_str(), new_v);
        } else if (e.key == ed::kKeyLeftBracket || e.key == ed::kKeyRightBracket) {
            if (sel < np - 1 && sel < me::kMaxCurves) {
                const float delta = (e.key == ed::kKeyLeftBracket) ? -0.05f : 0.05f;
                const float new_c = std::max(-1.0f, std::min(1.0f,
                    pts.curves[sel] + delta));
                const std::string n = me::param_name_for(me::PointField::Curve, sel);
                if (ctx->commands.set_param)
                    ctx->commands.set_param(ctx->commands.opaque, n.c_str(), new_c);
            }
        } else if (e.key == ed::kKeyDelete || e.key == ed::kKeyBackspace) {
            if (me::remove_point(ctx->commands, pts, np, sel)) {
                editor_selected_point_ = std::max(0, std::min(sel, np - 2));
            }
        }
    }

    // --- Layout (Phase C of the editor-UI platform plan) ---
    //
    // The editor carves the surface into a top-bar row + plot area. Uses
    // ui_layout + ui_row so the split stays in one place and future
    // widgets (e.g. a side panel or transport row) can drop in without
    // touching the pixel math.
    constexpr float kPad = 8.0f;
    constexpr float kTopBarH = 20.0f;
    auto ed_cursor = vivid::ui::ui_layout(
        vivid::ui::Rect{0.0f, 0.0f, ctx->surface_width, ctx->surface_height},
        kPad, /*gap=*/4.0f);
    const vivid::ui::Rect top_bar_rect = vivid::ui::ui_row(ed_cursor, kTopBarH);
    const vivid::ui::Rect plot_rect    = vivid::ui::ui_row(
        ed_cursor, std::max(1.0f, ed_cursor.remaining_h));
    const float plot_x = plot_rect.x;
    const float plot_y = plot_rect.y;
    const float plot_w = std::max(1.0f, plot_rect.w);
    const float plot_h = std::max(1.0f, plot_rect.h);

    // Top readout
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "Points: %d / 16   |   drag: points/curve handles   arrows: nudge   [ ]: curve   Del: remove   dbl-click: add",
                      np);
        if (d.draw_text) {
            d.draw_text(o, top_bar_rect.x, top_bar_rect.y + 2.0f, buf,
                        {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.8f}, 1.0f);
        }
    }

    auto time_to_x  = [&](float t) { return plot_x + t * plot_w; };
    auto value_to_y = [&](float v) { return plot_y + (1.0f - v) * plot_h; };
    auto x_to_time  = [&](float x) { return (x - plot_x) / plot_w; };
    auto y_to_value = [&](float y) { return 1.0f - (y - plot_y) / plot_h; };

    // --- Background ---
    d.draw_rect(o, plot_x, plot_y, plot_w, plot_h,
                {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

    // --- Loop region markers (dashed verticals) ---
    if (do_loop && ls < np && le < np) {
        const float lsx = time_to_x(pts.times[ls]);
        const float lex = time_to_x(pts.times[le]);
        const VividColor marker_col{th.accent.r, th.accent.g, th.accent.b, 0.4f};
        if (d.draw_dashed_line) {
            d.draw_dashed_line(o, lsx, plot_y, lsx, plot_y + plot_h, 1.0f, 4.0f, 4.0f,
                               marker_col);
            d.draw_dashed_line(o, lex, plot_y, lex, plot_y + plot_h, 1.0f, 4.0f, 4.0f,
                               marker_col);
        } else {
            for (float dy = plot_y; dy < plot_y + plot_h; dy += 8.0f) {
                const float dash_end = std::min(dy + 4.0f, plot_y + plot_h);
                d.draw_line(o, lsx, dy, lsx, dash_end, 1.0f, marker_col);
                d.draw_line(o, lex, dy, lex, dash_end, 1.0f, marker_col);
            }
        }
    }

    // --- Column fill under curve (same algorithm as inspector, denser) ---
    const float bottom_y = value_to_y(0.0f);
    const int cols = static_cast<int>(plot_w / 2.0f);
    if (cols > 0) {
        const float col_w = plot_w / static_cast<float>(cols);
        for (int c = 0; c < cols; ++c) {
            const float t_norm = static_cast<float>(c) / static_cast<float>(cols);
            const float val = me::evaluate_at(t_norm, pts, np);
            const float fx = plot_x + c * col_w;
            const float ey = value_to_y(val);
            const float fill_h = bottom_y - ey;
            if (fill_h > 0.0f) {
                d.draw_rect(o, fx, ey, col_w, fill_h,
                            {th.accent.r, th.accent.g, th.accent.b, 0.15f});
            }
        }
    }

    // --- Curve polyline ---
    if (np > 1) {
        std::vector<float> px, py;
        px.reserve(1 + (np - 1) * ed::kSubSegments);
        py.reserve(1 + (np - 1) * ed::kSubSegments);
        for (int i = 0; i < np - 1; ++i) {
            const float t0 = pts.times[i];
            const float t1 = pts.times[i + 1];
            const float v0 = pts.values[i];
            const float v1 = pts.values[i + 1];
            const float crv = pts.curves[i];
            if (i == 0) { px.push_back(time_to_x(t0)); py.push_back(value_to_y(v0)); }
            for (int s = 1; s <= ed::kSubSegments; ++s) {
                const float frac = static_cast<float>(s) / static_cast<float>(ed::kSubSegments);
                float shaped;
                if (std::abs(crv) < 0.001f) shaped = frac;
                else {
                    const float k = crv * 4.0f;
                    shaped = (std::exp(k * frac) - 1.0f) / (std::exp(k) - 1.0f);
                }
                px.push_back(time_to_x(t0 + (t1 - t0) * frac));
                py.push_back(value_to_y(v0 + (v1 - v0) * shaped));
            }
        }
        const VividColor curve_col{th.accent.r, th.accent.g, th.accent.b, 0.9f};
        const auto n = static_cast<uint32_t>(px.size());
        if (d.draw_polyline) {
            d.draw_polyline(o, px.data(), py.data(), n, 1.5f, curve_col);
        } else {
            for (uint32_t j = 1; j < n; ++j)
                d.draw_line(o, px[j-1], py[j-1], px[j], py[j], 1.5f, curve_col);
        }
    }

    // Identify the currently-dragging handle indices (if any) so the
    // render path can highlight them. Widget-owned DragHandleState is
    // the source of truth; an index of -1 means "not dragging".
    auto active_drag_curve = [&]() -> int {
        for (int i = 0; i < np - 1 && i < MSEG::kMaxCurves; ++i) {
            if (curve_drag_[i].dragging) return i;
        }
        return -1;
    };
    auto active_drag_point = [&]() -> int {
        for (int i = 0; i < np && i < MSEG::kMaxPoints; ++i) {
            if (point_drag_[i].dragging) return i;
        }
        return -1;
    };
    const int cur_drag_curve = active_drag_curve();
    const int cur_drag_point = active_drag_point();

    // --- Curve handles (segment midpoints) ---
    for (int i = 0; i < np - 1; ++i) {
        const float mid_t = 0.5f * (pts.times[i] + pts.times[i + 1]);
        const float mid_v = me::evaluate_at(mid_t, pts, np);
        const float hx = time_to_x(mid_t);
        const float hy = value_to_y(mid_v);
        const float s = ed::kCurveHandleSize;
        const float a = (i == cur_drag_curve) ? 1.0f
                      : (std::abs(pts.curves[i]) < 0.01f ? 0.45f : 0.8f);
        const bool active = (i == cur_drag_curve);
        const VividColor c = active
            ? VividColor{th.bright_text.r, th.bright_text.g, th.bright_text.b, 1.0f}
            : VividColor{th.bright_text.r, th.bright_text.g, th.bright_text.b, a};
        d.draw_rect(o, hx - s * 0.5f, hy - s * 0.5f, s, s, c);
    }

    // --- Point handles ---
    const float h_size = ed::kPointHandleSize;
    const float half_h = h_size * 0.5f;
    for (int i = 0; i < np; ++i) {
        const float hx = time_to_x(pts.times[i]) - half_h;
        const float hy = value_to_y(pts.values[i]) - half_h;
        const bool is_active = (i == cur_drag_point) ||
                               (i == editor_selected_point_);
        const VividColor c = is_active
            ? VividColor{th.bright_text.r, th.bright_text.g, th.bright_text.b, 1.0f}
            : VividColor{th.accent.r, th.accent.g, th.accent.b, 1.0f};
        d.draw_rect(o, hx, hy, h_size, h_size, c);
    }

    // --- Playhead (vertical accent line at current envelope time, plus a
    // dot where the playhead intersects the curve). draw_editor is a member
    // of MSEG, so elapsed_ and stage_ are readable directly. LOOPING wraps
    // inside the loop region — mirror the math in compute().
    if (stage_ != IDLE) {
        const float tt = std::max(0.01f, total_time.value);
        float norm_pos = elapsed_ / tt;
        if (do_loop && stage_ == LOOPING && ls < np && le < np) {
            const float lst = pts.times[ls];
            const float let = pts.times[le];
            const float llen = let - lst;
            if (llen > 0.0001f) {
                const float overshoot = norm_pos - lst;
                norm_pos = lst + std::fmod(overshoot, llen);
            }
        }
        norm_pos = std::max(0.0f, std::min(1.0f, norm_pos));

        const float ph_x = time_to_x(norm_pos);
        d.draw_line(o, ph_x, plot_y, ph_x, plot_y + plot_h, 1.5f,
                    {th.accent.r, th.accent.g, th.accent.b, 0.9f});

        // Dot on the curve at the playhead's time position.
        const float val_at_ph = me::evaluate_at(norm_pos, pts, np);
        const float dot_r = 4.0f;
        d.draw_rect(o, ph_x - dot_r, value_to_y(val_at_ph) - dot_r,
                    dot_r * 2.0f, dot_r * 2.0f,
                    {th.bright_text.r, th.bright_text.g, th.bright_text.b, 1.0f});
    }

    // --- Selection ring ---
    if (editor_selected_point_ >= 0 && editor_selected_point_ < np) {
        const float hx = time_to_x(pts.times[editor_selected_point_]) - half_h;
        const float hy = value_to_y(pts.values[editor_selected_point_]) - half_h;
        vivid::draw_ui::draw_panel(d, o, hx - 2.0f, hy - 2.0f,
                                    h_size + 4.0f, h_size + 4.0f,
                                    {0, 0, 0, 0},
                                    {th.accent.r, th.accent.g, th.accent.b, 1.0f},
                                    0.0f, 1.5f);
    }

    // --- Host-service hints (Phase D): cursor + status on handle hover.
    //
    // Identify which handle (point or curve) the mouse is currently
    // hovering; emit CROSSHAIR over point handles, RESIZE_V over curve
    // handles; set_status_text with the hovered point's (t, v, curve).
    {
        const int hover_pi = me::pick_point(mouse.x, mouse.y, pts, np,
                                            plot_x, plot_y, plot_w, plot_h,
                                            ed::kPointHitRadius);
        const int hover_ci = (hover_pi < 0)
            ? me::pick_curve_handle(mouse.x, mouse.y, pts, np,
                                    plot_x, plot_y, plot_w, plot_h,
                                    ed::kCurveHitRadius)
            : -1;
        if (ctx->host.set_cursor) {
            if (hover_pi >= 0)
                ctx->host.set_cursor(ctx->host.opaque, VIVID_CURSOR_CROSSHAIR);
            else if (hover_ci >= 0)
                ctx->host.set_cursor(ctx->host.opaque, VIVID_CURSOR_RESIZE_V);
        }
        if (ctx->host.set_status_text && hover_pi >= 0 && hover_pi < np) {
            const float crv = (hover_pi < np - 1 && hover_pi < me::kMaxCurves)
                ? pts.curves[hover_pi] : 0.0f;
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                "point %d: t=%.3f  v=%.2f  curve=%.2f",
                hover_pi, pts.times[hover_pi], pts.values[hover_pi], crv);
            ctx->host.set_status_text(ctx->host.opaque, buf);
        }
    }

    // --- Mouse: click picks point → curve handle → double-click adds ---
    //
    // Phase C of the editor-UI platform plan: nearest-hit picking stays in
    // MSEG's shared helpers (pick_point / pick_curve_handle), while per-
    // handle drag bookkeeping (origin_mx/my + dragging flag) moves into
    // vivid::ui::DragHandleState arrays on the core. The picker chooses
    // the winner on click; ui_drag_handle_begin records the origin;
    // ui_drag_handle_update drives the subsequent drag frames and fires
    // `released` on the mouse-up frame.
    if (mouse.left_clicked) {
        const int pi = me::pick_point(mouse.x, mouse.y, pts, np,
                                      plot_x, plot_y, plot_w, plot_h,
                                      ed::kPointHitRadius);
        if (pi >= 0 && pi < MSEG::kMaxPoints) {
            vivid::ui::ui_drag_handle_begin(*ctx, &point_drag_[pi]);
            editor_selected_point_ = pi;
        } else {
            const int ci = me::pick_curve_handle(mouse.x, mouse.y, pts, np,
                                                 plot_x, plot_y, plot_w, plot_h,
                                                 ed::kCurveHitRadius);
            if (ci >= 0 && ci < MSEG::kMaxCurves) {
                vivid::ui::ui_drag_handle_begin(*ctx, &curve_drag_[ci]);
            } else {
                // Double-click-add: within plot area only. This is
                // operator-specific; no widget yet.
                const bool in_plot = mouse.x >= plot_x && mouse.x < plot_x + plot_w &&
                                     mouse.y >= plot_y && mouse.y < plot_y + plot_h;
                const double now = ctx->time;
                const float dx = mouse.x - editor_last_click_x_;
                const float dy = mouse.y - editor_last_click_y_;
                const bool close_in_time =
                    (editor_last_click_t_ > 0.0) &&
                    ((now - editor_last_click_t_) < ed::kDoubleClickMaxDt);
                const bool close_in_space =
                    (dx * dx + dy * dy) <
                    (ed::kDoubleClickMaxDist * ed::kDoubleClickMaxDist);

                if (in_plot && close_in_time && close_in_space && np < me::kMaxPoints) {
                    const float t = std::max(0.001f, std::min(0.999f, x_to_time(mouse.x)));
                    const float v = std::max(0.0f, std::min(1.0f, y_to_value(mouse.y)));
                    int seg = 0;
                    for (int i = 0; i < np - 1; ++i) {
                        if (t <= pts.times[i + 1]) { seg = i; break; }
                        seg = i;
                    }
                    const int insert_at = seg + 1;
                    const float curve_inherit =
                        (seg < me::kMaxCurves) ? pts.curves[seg] : 0.0f;
                    if (me::add_point(ctx->commands, pts, np, insert_at,
                                       t, v, curve_inherit)) {
                        editor_selected_point_ = insert_at;
                    }
                    editor_last_click_t_ = -1.0;
                } else {
                    editor_last_click_t_ = now;
                    editor_last_click_x_ = mouse.x;
                    editor_last_click_y_ = mouse.y;
                }
            }
        }
    }

    // Per-frame drag updates. Scan each handle's DragHandleState; if it
    // reports dragging, translate current mouse.x/y → time/value.
    for (int i = 0; i < np && i < MSEG::kMaxPoints; ++i) {
        const auto r = vivid::ui::ui_drag_handle_update(*ctx, &point_drag_[i]);
        if (!r.dragging && !r.released) continue;
        if (!r.dragging && r.released) continue;  // release-only frame: no write
        const float new_t = me::clamp_new_time(i, np,
                                               x_to_time(mouse.x), pts.times);
        const float new_v = std::max(0.0f, std::min(1.0f, y_to_value(mouse.y)));
        if (ctx->commands.set_param) {
            const std::string nt = me::param_name_for(me::PointField::Time, i);
            ctx->commands.set_param(ctx->commands.opaque, nt.c_str(), new_t);
            const std::string nv = me::param_name_for(me::PointField::Value, i);
            ctx->commands.set_param(ctx->commands.opaque, nv.c_str(), new_v);
        }
    }

    for (int i = 0; i < np - 1 && i < MSEG::kMaxCurves; ++i) {
        const auto r = vivid::ui::ui_drag_handle_update(*ctx, &curve_drag_[i]);
        if (!r.dragging) continue;
        // Map mouse-y delta from the linear-midpoint to a curvature value.
        // Dragging above the linear midpoint bulges the curve up (+),
        // below sags it down (−). Full range ±1 spans ~1/3 of plot_h.
        const float lin_v = 0.5f * (pts.values[i] + pts.values[i + 1]);
        const float lin_y = value_to_y(lin_v);
        const float span = std::max(1.0f, plot_h * 0.33f);
        float new_c = (lin_y - mouse.y) / span;
        new_c = std::max(-1.0f, std::min(1.0f, new_c));
        if (ctx->commands.set_param) {
            const std::string n = me::param_name_for(me::PointField::Curve, i);
            ctx->commands.set_param(ctx->commands.opaque, n.c_str(), new_c);
        }
    }

    // --- Keep selection in-bounds ---
    if (editor_selected_point_ < 0) editor_selected_point_ = 0;
    if (editor_selected_point_ >= np) editor_selected_point_ = np - 1;
}
