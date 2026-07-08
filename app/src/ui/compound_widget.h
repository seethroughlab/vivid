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

#include <algorithm>
#include <cstdint>

namespace vivid::ui {

// Is this display_hint a compound widget the host knows how to render? (Only implemented widgets
// return true, so an un-implemented hint falls back to the normal per-param widget.)
inline bool is_compound_widget(int hint) {
    return hint == VIVID_DISPLAY_XY_PAD;   // UI-4a.1 (COLOR/ADSR/LFO land in later slices)
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
    r.draw_rounded_rect(rc.x, rc.y, rc.w, rc.h, 3.f, 0.10f, 0.11f, 0.13f, 1.f);
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

}  // namespace vivid::ui
