// Dedicated editor window for ParametricEQ. Frequency-response curve
// on a log-freq × linear-dB plane: each active band is a draggable
// node (x=freq, y=gain), scroll adjusts Q, double-click cycles type.
// Side panel shows the selected band's numeric values + type cycle.

#include "parametric_eq.h"
#include "parametric_eq_editor_shared.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_keys.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

namespace pe  = ::vivid::parametric_eq_editor;
namespace ek  = ::vivid::editor_keys;

constexpr float kInset        = 8.0f;
constexpr float kTopBarH      = 32.0f;
constexpr float kSidePanelW   = 260.0f;
constexpr float kPlaneLeftPad = 48.0f;   // y-axis label gutter
constexpr float kPlaneBotPad  = 24.0f;   // x-axis label strip
constexpr float kBandHitRadius = 14.0f;

constexpr int kCurveSamples = 192;  // horizontal response-curve points

// Simple, readable palette for the four band nodes.
constexpr float kBandColors[pe::kMaxBands][3] = {
    {0.92f, 0.48f, 0.34f},   // warm red
    {0.88f, 0.79f, 0.30f},   // yellow
    {0.34f, 0.76f, 0.58f},   // green
    {0.38f, 0.60f, 0.92f},   // blue
};

} // namespace


VividEditorMetadata ParametricEQ::editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 1000;
    m.default_height = 540;
    m.min_width      = 720;
    m.min_height     = 400;
    m.title_suffix   = "ParametricEQ Editor";
    return m;
}

void ParametricEQ::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;

    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;

    auto get_param = [&](int idx, float fallback) -> float {
        if (idx < 0) return fallback;
        if (static_cast<uint32_t>(idx) >= ctx->param_count) return fallback;
        return ctx->param_values[idx];
    };
    auto set_named = [&](const char* name, float v) {
        if (ctx->commands.set_param)
            ctx->commands.set_param(ctx->commands.opaque, name, v);
    };

    // --- Live state ----
    const int active_bands = std::clamp(
        static_cast<int>(std::lround(
            get_param(pe::kBandCountParamIndex, 4.0f))),
        1, pe::kMaxBands);

    float freqs[pe::kMaxBands];
    float gains[pe::kMaxBands];
    float Qs   [pe::kMaxBands];
    int   types[pe::kMaxBands];
    for (int b = 0; b < pe::kMaxBands; ++b) {
        freqs[b] = std::clamp(get_param(pe::freq_param_index(b), 1000.0f),
                              pe::kMinFreqHz, pe::kMaxFreqHz);
        gains[b] = std::clamp(get_param(pe::gain_param_index(b), 0.0f),
                              pe::kMinGainDb, pe::kMaxGainDb);
        Qs[b]    = std::max(0.1f, get_param(pe::q_param_index(b), 1.0f));
        types[b] = std::clamp(
            static_cast<int>(std::lround(get_param(pe::type_param_index(b), 0.0f))),
            0, pe::kBandTypeCount - 1);
    }

    // --- Layout ---
    const float surf_w = ctx->surface_width;
    const float surf_h = ctx->surface_height;

    const float top_y = kInset;
    const float top_h = kTopBarH;
    const float body_y = top_y + top_h + kInset;
    const float body_h = std::max(0.0f, surf_h - body_y - kInset);

    const float side_x = surf_w - kInset - kSidePanelW;
    const float side_w = kSidePanelW;

    const float plane_outer_x = kInset;
    const float plane_outer_y = body_y;
    const float plane_outer_w = std::max(0.0f, side_x - plane_outer_x - kInset);
    const float plane_outer_h = body_h;

    const float plane_x = plane_outer_x + kPlaneLeftPad;
    const float plane_y = plane_outer_y + 12.0f;
    const float plane_w = std::max(1.0f,
        plane_outer_w - kPlaneLeftPad - 12.0f);
    const float plane_h = std::max(1.0f,
        plane_outer_h - kPlaneBotPad - 24.0f);

    // Sample rate for response curve. We don't have the live audio SR
    // in the editor context; 48k matches the runtime default and the
    // curve is only for display.
    constexpr float kDisplaySampleRate = 48000.0f;

    // --- Keyboard ----
    ctx->wants_keyboard = 1;
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
        if (e.action != ek::kPress && e.action != ek::kRepeat) continue;

        // 1..4 select band
        if (e.key == ek::k1) { editor_selected_band_ = 0; continue; }
        if (e.key == ek::k2 && active_bands > 1) { editor_selected_band_ = 1; continue; }
        if (e.key == ek::k3 && active_bands > 2) { editor_selected_band_ = 2; continue; }
        if (e.key == ek::k4 && active_bands > 3) { editor_selected_band_ = 3; continue; }

        // T cycles type of the selected band
        if (e.key == ek::kT && editor_selected_band_ >= 0) {
            char name[8];
            std::snprintf(name, sizeof(name), "type_%d", editor_selected_band_ + 1);
            const int next = (types[editor_selected_band_] + 1) % pe::kBandTypeCount;
            set_named(name, static_cast<float>(next));
            continue;
        }

        // Arrow keys nudge the selected band's freq (x) / gain (y).
        if (editor_selected_band_ >= 0) {
            const bool shift = (e.modifiers & ek::kModShift) != 0;
            const int b = editor_selected_band_;

            if (e.key == ek::kLeft || e.key == ek::kRight) {
                const float octaves = shift ? 1.0f : (1.0f / 12.0f);
                const float mult = (e.key == ek::kRight)
                    ? std::pow(2.0f, octaves)
                    : std::pow(2.0f, -octaves);
                char name[8];
                std::snprintf(name, sizeof(name), "freq_%d", b + 1);
                const float new_f = std::clamp(freqs[b] * mult,
                                               pe::kMinFreqHz, pe::kMaxFreqHz);
                set_named(name, new_f);
                continue;
            }
            if (e.key == ek::kUp || e.key == ek::kDown) {
                const float step = shift ? 3.0f : 0.5f;
                const float sign = (e.key == ek::kUp) ? +1.0f : -1.0f;
                char name[8];
                std::snprintf(name, sizeof(name), "gain_%d", b + 1);
                const float new_g = std::clamp(gains[b] + sign * step,
                                               pe::kMinGainDb, pe::kMaxGainDb);
                set_named(name, new_g);
                continue;
            }
        }
    }

    // --- Mouse on the plane: click to select/drag, scroll to adjust Q ----
    const auto& mouse = ctx->mouse;
    const bool mouse_in_plane =
        (mouse.x >= plane_x && mouse.x < plane_x + plane_w &&
         mouse.y >= plane_y && mouse.y < plane_y + plane_h);

    if (mouse.left_clicked) {
        const int hit = pe::hit_test_band(plane_x, plane_y, plane_w, plane_h,
                                          mouse.x, mouse.y,
                                          freqs, gains,
                                          active_bands, kBandHitRadius);
        if (hit >= 0) {
            editor_selected_band_ = hit;
            editor_drag_band_     = hit;
        } else if (mouse_in_plane) {
            // Clicking empty plane selects the nearest band, no drag.
            const int near = pe::hit_test_band(plane_x, plane_y, plane_w, plane_h,
                                               mouse.x, mouse.y,
                                               freqs, gains,
                                               active_bands, 1e6f);
            if (near >= 0) editor_selected_band_ = near;
        }
    }
    if (!mouse.left_down) editor_drag_band_ = -1;

    if (editor_drag_band_ >= 0) {
        const int b = editor_drag_band_;
        const float fx = std::clamp((mouse.x - plane_x) / plane_w, 0.0f, 1.0f);
        const float fy = std::clamp(1.0f - (mouse.y - plane_y) / plane_h,
                                    0.0f, 1.0f);
        const float new_f = pe::fraction_to_freq(fx);
        const float new_g = pe::fraction_to_db(fy);
        char fn[8], gn[8];
        std::snprintf(fn, sizeof(fn), "freq_%d", b + 1);
        std::snprintf(gn, sizeof(gn), "gain_%d", b + 1);
        set_named(fn, new_f);
        set_named(gn, new_g);
    }

    // Scroll over a band node: adjust Q. Shift = coarse.
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_MOUSE_SCROLL) continue;
        const int hit = pe::hit_test_band(plane_x, plane_y, plane_w, plane_h,
                                          mouse.x, mouse.y,
                                          freqs, gains,
                                          active_bands, kBandHitRadius);
        if (hit < 0) continue;
        const float mult = (e.modifiers & ek::kModShift) ? 1.4f : 1.15f;
        const float dir  = (e.scroll_dy > 0) ? mult : (1.0f / mult);
        const float new_q = std::clamp(Qs[hit] * dir, 0.1f, 20.0f);
        char qn[8];
        std::snprintf(qn, sizeof(qn), "q_%d", hit + 1);
        set_named(qn, new_q);
        editor_selected_band_ = hit;
    }

    // --- Draw: backdrop ----
    vivid::draw_ui::draw_panel(d, o, 0.0f, 0.0f, surf_w, surf_h,
        {th.dark_bg.r * 0.9f, th.dark_bg.g * 0.9f, th.dark_bg.b * 0.9f, 1.0f},
        {0, 0, 0, 0}, 0.0f, 0.0f);

    // Top bar.
    if (d.draw_text) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
                      "ParametricEQ · %d band%s active",
                      active_bands, active_bands == 1 ? "" : "s");
        d.draw_text(o, kInset + 4.0f, top_y + 8.0f, buf,
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f},
            1.0f);
        const char* hints =
            "click band=select/drag  ·  scroll=Q  ·  T=type  ·  1–4 select  ·  "
            "arrows nudge  ·  shift=coarse";
        const float scale = 0.7f;
        const float hints_w = d.text_width
            ? d.text_width(o, hints, scale) : 600.0f;
        d.draw_text(o, surf_w - kInset - hints_w, top_y + 10.0f, hints,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.7f}, scale);
    }

    // --- Plane region ---
    vivid::draw_ui::draw_panel(d, o, plane_outer_x, plane_outer_y,
        plane_outer_w, plane_outer_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.92f},
        {th.separator.r, th.separator.g, th.separator.b, 0.6f}, 4.0f, 1.0f);

    // Horizontal gridlines at -18/-12/-6/0/+6/+12/+18 dB.
    const float gridline_dbs[] = {-18, -12, -6, 0, +6, +12, +18};
    if (d.draw_rect) {
        for (float db : gridline_dbs) {
            const float yf = 1.0f - pe::db_to_fraction(db);
            const float gy = plane_y + yf * plane_h;
            const bool zero = std::fabs(db) < 0.01f;
            d.draw_rect(o, plane_x, gy - 0.5f, plane_w, 1.0f,
                {th.separator.r, th.separator.g, th.separator.b,
                 zero ? 0.6f : 0.22f});
        }
    }
    if (d.draw_text) {
        for (float db : gridline_dbs) {
            char lb[8];
            std::snprintf(lb, sizeof(lb), "%+d", static_cast<int>(db));
            const float yf = 1.0f - pe::db_to_fraction(db);
            const float gy = plane_y + yf * plane_h - 5.0f;
            d.draw_text(o, plane_outer_x + 6.0f, gy, lb,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.7f}, 0.65f);
        }
    }

    // Vertical gridlines at 100 Hz, 1 kHz, 10 kHz, plus minor at octaves.
    const float major_hz[]  = {100.0f, 1000.0f, 10000.0f};
    const float major_decades[] = {20.0f, 200.0f, 2000.0f, 20000.0f};
    if (d.draw_rect) {
        for (float hz : major_decades) {
            const float xf = pe::freq_to_fraction(hz);
            const float gx = plane_x + xf * plane_w;
            d.draw_rect(o, gx - 0.5f, plane_y, 1.0f, plane_h,
                {th.separator.r, th.separator.g, th.separator.b, 0.18f});
        }
        for (float hz : major_hz) {
            const float xf = pe::freq_to_fraction(hz);
            const float gx = plane_x + xf * plane_w;
            d.draw_rect(o, gx - 0.5f, plane_y, 1.0f, plane_h,
                {th.separator.r, th.separator.g, th.separator.b, 0.35f});
        }
    }
    if (d.draw_text) {
        auto label_hz = [&](float hz, const char* s) {
            const float xf = pe::freq_to_fraction(hz);
            const float gx = plane_x + xf * plane_w - 10.0f;
            d.draw_text(o, gx, plane_y + plane_h + 6.0f, s,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.7f}, 0.65f);
        };
        label_hz(20.0f,    "20");
        label_hz(100.0f,   "100");
        label_hz(1000.0f,  "1k");
        label_hz(10000.0f, "10k");
        label_hz(20000.0f, "20k");
    }

    // Composite response curve.
    if (d.draw_line && plane_w > 0.0f) {
        float prev_x = plane_x;
        float prev_y = plane_y + plane_h * 0.5f;
        for (int i = 0; i < kCurveSamples; ++i) {
            const float t  = static_cast<float>(i) /
                             static_cast<float>(kCurveSamples - 1);
            const float cx = plane_x + t * plane_w;
            const float hz = pe::fraction_to_freq(t);
            const float db = pe::composite_magnitude_db(
                types, freqs, gains, Qs,
                active_bands, kDisplaySampleRate, hz);
            const float cy = plane_y +
                             (1.0f - pe::db_to_fraction(db)) * plane_h;
            if (i > 0) {
                d.draw_line(o, prev_x, prev_y, cx, cy, 1.6f,
                    {th.accent.r, th.accent.g, th.accent.b, 0.9f});
            }
            prev_x = cx; prev_y = cy;
        }
    }

    // Band nodes: dim inactive beyond band_count; highlight selected.
    for (int b = 0; b < pe::kMaxBands; ++b) {
        const bool active = (b < active_bands);
        const bool selected = (b == editor_selected_band_);
        const pe::NodePoint p = pe::band_node_position(
            plane_x, plane_y, plane_w, plane_h, freqs[b], gains[b]);

        // Node radius scales weakly with log(Q) so high-Q bands look
        // tighter. Clamp so dragging is easy at all Q.
        const float q_log = std::log2(std::max(0.1f, Qs[b]));
        const float radius = std::clamp(10.0f - q_log * 1.5f, 5.0f, 12.0f);

        const float r = kBandColors[b][0];
        const float g = kBandColors[b][1];
        const float bl = kBandColors[b][2];
        const float alpha = active ? (selected ? 1.0f : 0.8f) : 0.25f;

        if (d.draw_rounded_rect) {
            d.draw_rounded_rect(o, p.x - radius, p.y - radius,
                radius * 2.0f, radius * 2.0f, radius,
                {r, g, bl, alpha});
        } else if (d.draw_rect) {
            d.draw_rect(o, p.x - radius, p.y - radius,
                        radius * 2.0f, radius * 2.0f, {r, g, bl, alpha});
        }
        if (selected && d.draw_rounded_rect) {
            d.draw_rounded_rect(o, p.x - radius - 3.0f, p.y - radius - 3.0f,
                (radius + 3.0f) * 2.0f, (radius + 3.0f) * 2.0f, radius + 3.0f,
                {1.0f, 1.0f, 1.0f, 0.0f});  // halo handled by stroke below
        }
        // Band-number label inside the node.
        if (d.draw_text) {
            char lb[4];
            std::snprintf(lb, sizeof(lb), "%d", b + 1);
            d.draw_text(o, p.x - 3.0f, p.y - 5.0f, lb,
                {0.08f, 0.08f, 0.10f,
                 active ? 0.95f : 0.5f}, 0.7f);
        }
    }

    // --- Side panel ---
    vivid::draw_ui::draw_panel(d, o, side_x, body_y, side_w, body_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.85f},
        {th.separator.r, th.separator.g, th.separator.b, 0.8f}, 4.0f, 1.0f);

    const int sel = std::clamp(editor_selected_band_, -1, pe::kMaxBands - 1);
    if (d.draw_text) {
        constexpr float kPad = 12.0f;
        float ty = body_y + kPad;

        char title[48];
        if (sel >= 0) {
            std::snprintf(title, sizeof(title), "Band %d", sel + 1);
        } else {
            std::snprintf(title, sizeof(title), "No band selected");
        }
        d.draw_text(o, side_x + kPad, ty, title,
            {th.bright_text.r, th.bright_text.g,
             th.bright_text.b, 0.95f}, 1.1f);
        ty += 22.0f;

        if (sel >= 0) {
            char line[64];

            std::snprintf(line, sizeof(line), "Type: %s",
                pe::band_type_name(types[sel]));
            d.draw_text(o, side_x + kPad, ty, line,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 0.85f);
            ty += 18.0f;

            // Frequency — display with Hz / kHz units.
            if (freqs[sel] >= 1000.0f)
                std::snprintf(line, sizeof(line), "Freq: %.2f kHz",
                              freqs[sel] / 1000.0f);
            else
                std::snprintf(line, sizeof(line), "Freq: %.1f Hz",
                              freqs[sel]);
            d.draw_text(o, side_x + kPad, ty, line,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 0.85f);
            ty += 18.0f;

            std::snprintf(line, sizeof(line), "Gain: %+.2f dB", gains[sel]);
            d.draw_text(o, side_x + kPad, ty, line,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 0.85f);
            ty += 18.0f;

            std::snprintf(line, sizeof(line), "Q: %.2f", Qs[sel]);
            d.draw_text(o, side_x + kPad, ty, line,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 0.85f);
            ty += 22.0f;

            d.draw_text(o, side_x + kPad, ty,
                "Press T to cycle type",
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.6f}, 0.7f);
        }

        // Band-count control at the bottom of the side panel.
        const float bc_y = body_y + body_h - 48.0f;
        d.draw_text(o, side_x + kPad, bc_y,
            "Active bands: ",
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.85f}, 0.9f);
        char bcbuf[8];
        std::snprintf(bcbuf, sizeof(bcbuf), "%d", active_bands);
        d.draw_text(o, side_x + kPad + 110.0f, bc_y, bcbuf,
            {th.bright_text.r, th.bright_text.g,
             th.bright_text.b, 0.95f}, 1.0f);
        d.draw_text(o, side_x + kPad, bc_y + 18.0f,
            "(use inspector to change)",
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.55f}, 0.7f);
    }
}
