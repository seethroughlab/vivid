#pragma once
// UI-4a: host-composed COMPOUND inspector widgets (ADR-0013 principle 4 — "inspectors are a
// lightweight host-composed strip … via a compound-widget registry: ADSR / XY-pad / color / LFO").
// An operator declares a compound param group by putting a compound `display_hint` on the FIRST
// param of the group; the widget then claims `compound_span(hint)` consecutive params and the
// inspector draws ONE widget across that many rows instead of a knob per param. Pure geometry +
// draw, so the dock draw path and the input hit-test share it (like param_widget.h).
#include "operator_api/types.h"   // VIVID_DISPLAY_* (macros only)
#include "ui/layout.h"            // Rect
#include "ui/renderer_2d.h"
#include "ui/ui_style.h"

#include <algorithm>
#include <cstdint>

namespace vivid::ui {

// Is this display_hint a compound widget the host knows how to render? (Only implemented widgets
// return true, so an un-implemented hint falls back to the normal per-param widget.)
inline bool is_compound_widget(int hint) {
    return hint == VIVID_DISPLAY_XY_PAD || hint == VIVID_DISPLAY_COLOR ||
           hint == VIVID_DISPLAY_ADSR   || hint == VIVID_DISPLAY_LFO;
}

// How many consecutive params a compound widget claims (the group it renders as one unit).
inline int compound_span(int hint) {
    switch (hint) {
        case VIVID_DISPLAY_XY_PAD: return 2;   // x, y
        case VIVID_DISPLAY_COLOR:  return 3;   // r, g, b
        case VIVID_DISPLAY_ADSR:   return 4;   // attack, decay, sustain, release
        case VIVID_DISPLAY_LFO:    return 1;   // waveform enum
        default:                   return 1;
    }
}

// Map a cursor position inside a pad rect to normalized (x01,y01), y pointing UP.
inline void xy_from_cursor(const Rect& rc, double mx, double my, float& x01, float& y01) {
    x01 = std::clamp(static_cast<float>((mx - rc.x) / rc.w), 0.f, 1.f);
    y01 = std::clamp(1.f - static_cast<float>((my - rc.y) / rc.h), 0.f, 1.f);
}

// Draw an XY pad: a 2D field with a draggable handle at (x01, y01) (y up). `accent` tints the
// crosshair + handle; `label` (e.g. "warp / density") names the two axes.
inline void draw_xy_pad(Renderer2D& r, const Rect& rc, float x01, float y01,
                        const float accent[3], const char* label) {
    recess(r, rc, true);
    // center grid lines
    r.draw_line(rc.x, rc.y + rc.h * 0.5f, rc.x + rc.w, rc.y + rc.h * 0.5f, 1.f, 0.20f, 0.20f, 0.23f, 1.f);
    r.draw_line(rc.x + rc.w * 0.5f, rc.y, rc.x + rc.w * 0.5f, rc.y + rc.h, 1.f, 0.20f, 0.20f, 0.23f, 1.f);
    const float hx = rc.x + std::clamp(x01, 0.f, 1.f) * rc.w;
    const float hy = rc.y + (1.f - std::clamp(y01, 0.f, 1.f)) * rc.h;   // y up
    r.draw_line(rc.x, hy, rc.x + rc.w, hy, 1.f, accent[0], accent[1], accent[2], 0.35f);
    r.draw_line(hx, rc.y, hx, rc.y + rc.h, 1.f, accent[0], accent[1], accent[2], 0.35f);
    r.draw_circle(hx, hy, 5.f, 0.f, accent[0], accent[1], accent[2], 1.f);   // filled handle
    if (label && *label) r.draw_text(rc.x + 4.f, rc.y + 2.f, label, 0.6f, 0.6f, 0.64f, 1.f, 0.6f);
}

// Draw a live color swatch (the preview half of the COLOR widget). The r/g/b channels themselves
// render as ordinary sliders at their per-param rects, so the standard horizontal drag edits them;
// this is just the grouping preview. `sw` is the swatch rect (typically the label column, spanning
// the group's rows).
inline void draw_color_swatch(Renderer2D& r, const Rect& sw, float rf, float gf, float bf) {
    recess(r, sw, true);
    r.draw_rounded_rect(sw.x + 2.f, sw.y + 2.f, sw.w - 4.f, sw.h - 4.f, 2.f,
                        std::clamp(rf, 0.f, 1.f), std::clamp(gf, 0.f, 1.f), std::clamp(bf, 0.f, 1.f), 1.f);
}

// Draw an ADSR envelope PREVIEW (the grouping half of the widget; the A/D/S/R channels themselves
// render as ordinary knobs so the standard knob-drag edits them, like COLOR's channel sliders).
// a01/d01/r01 are the time params normalized to their [min,max]; s01 is the 0..1 sustain level.
inline void draw_adsr(Renderer2D& r, const Rect& rc, float a01, float d01, float s01, float r01,
                      const float accent[3], const char* label) {
    recess(r, rc, true);
    const float uw = rc.w - 8.f, base = rc.y + rc.h - 5.f, top = rc.y + 5.f;
    const float sus_y = top + (1.f - std::clamp(s01, 0.f, 1.f)) * (base - top);
    float a = std::max(a01, 0.f), d = std::max(d01, 0.f), rl = std::max(r01, 0.f);
    const float sum = a + d + rl;
    if (sum <= 1e-4f) { a = d = rl = 1.f; }               // no times set → show equal ramps
    const float span = a + d + rl, tw = uw * 0.78f, ws = uw * 0.22f;   // reserve a sustain plateau
    const float wa = tw * a / span, wd = tw * d / span, wr = tw * rl / span;
    const float x0 = rc.x + 4.f;
    const float x1 = x0 + wa, x2 = x1 + wd, x3 = x2 + ws, x4 = x3 + wr;
    const float xs[5] = { x0, x1, x2, x3, x4 };
    const float ys[5] = { base, top, sus_y, sus_y, base };
    r.draw_polyline(xs, ys, 5, 1.8f, accent[0], accent[1], accent[2], 0.95f);
    r.draw_circle(x1, top,   2.5f, 0.f, accent[0], accent[1], accent[2], 1.f);   // peak
    r.draw_circle(x3, sus_y, 2.5f, 0.f, accent[0], accent[1], accent[2], 1.f);   // sustain
    if (label && *label) r.draw_text(rc.x + 4.f, rc.y + 2.f, label, 0.6f, 0.6f, 0.64f, 1.f, 0.6f);
}

// One unipolar (0..1) sample of LFO waveform `w` at phase `ph` (0..1) — mirrors the synth's shapes.
inline float lfo_preview_sample(int w, float ph) {
    switch (w) {
        case 1: return 1.f - std::fabs(2.f * ph - 1.f);                  // triangle
        case 2: return ph < 0.5f ? 1.f : 0.f;                           // square
        case 3: return ph;                                             // saw
        default: return 0.5f - 0.5f * std::cos(6.2831853f * ph);        // sine
    }
}

// Draw an LFO waveform PREVIEW (2 cycles) + its name. The waveform param is an enum; clicking the
// widget cycles it (handled by the inspector's input path). `wave` is the 0..3 waveform index.
inline void draw_lfo(Renderer2D& r, const Rect& rc, int wave, const char* wave_name,
                     const float accent[3], const char* label) {
    recess(r, rc, true);
    const float base = rc.y + rc.h - 5.f, top = rc.y + 5.f, x0 = rc.x + 4.f, uw = rc.w - 8.f;
    constexpr int N = 48; float xs[N], ys[N];
    for (int i = 0; i < N; ++i) {
        const float ph = 2.f * (i / float(N - 1));          // 2 cycles
        xs[i] = x0 + uw * (i / float(N - 1));
        ys[i] = base - lfo_preview_sample(wave, ph - std::floor(ph)) * (base - top);
    }
    r.draw_polyline(xs, ys, N, 1.8f, accent[0], accent[1], accent[2], 0.95f);
    if (wave_name && *wave_name) r.draw_text(rc.x + rc.w - 52.f, rc.y + 2.f, wave_name, accent[0], accent[1], accent[2], 0.9f, 0.6f);
    if (label && *label) r.draw_text(rc.x + 4.f, rc.y + 2.f, label, 0.6f, 0.6f, 0.64f, 1.f, 0.6f);
}

}  // namespace vivid::ui
