#pragma once
#include "ui/renderer_2d.h"
#include <cmath>
#include <algorithm>

// Shared visual design language for the PoC UI (borrowed from vivid-classic):
// a small palette/spacing struct + reusable widget helpers (knob, card, section
// header) built on Renderer2D's existing primitives. Keeps colours/metrics out
// of the draw code so the interface reads cohesively.
namespace vivid::ui {

struct Style {
    // surfaces
    float bg[3]      = { 0.045f, 0.050f, 0.060f };
    float panel[3]   = { 0.100f, 0.110f, 0.130f };
    float card[3]    = { 0.130f, 0.140f, 0.170f };
    float card_hi[3] = { 0.170f, 0.185f, 0.215f };   // hover / selected
    float recess[3]  = { 0.030f, 0.035f, 0.045f };   // inset wells (thumbnails, sliders)
    // text + lines
    float dim[3]     = { 0.500f, 0.530f, 0.580f };
    float text[3]    = { 0.880f, 0.900f, 0.940f };
    float sep[3]     = { 0.200f, 0.210f, 0.240f };
    // domain accents
    float audio[3]   = { 0.94f, 0.63f, 0.19f };      // amber  (audio / instrument)
    float gpu[3]     = { 0.35f, 0.66f, 0.90f };      // cyan   (visual / gpu)
    float fx[3]      = { 0.60f, 0.45f, 0.85f };      // violet (effects)
    float control[3] = { 0.55f, 0.60f, 0.66f };      // gray   (control)
    float teal[3]    = { 0.31f, 0.80f, 0.75f };      // bridge (data sources)
    float gold[3]    = { 0.95f, 0.78f, 0.30f };      // selection / queued
    // metrics
    float pad = 8.f, row_h = 22.f, section_gap = 12.f, accent_bar = 3.f;
};
inline const Style& style() { static const Style s; return s; }

// A card: filled rounded body with a thin accent bar across the top.
inline void draw_card(Renderer2D& r, float x, float y, float w, float h,
                      const float* accent, bool hot = false) {
    const Style& s = style();
    const float* bg = hot ? s.card_hi : s.card;
    r.draw_rounded_rect(x, y, w, h, 5.f, bg[0], bg[1], bg[2], 1.0f);
    r.draw_rect(x, y, w, s.accent_bar, accent[0], accent[1], accent[2], 1.0f);
}

// A small left-aligned section label in dim text with an accent tick.
inline void section_header(Renderer2D& r, float x, float y, const char* label,
                           const float* accent) {
    const Style& s = style();
    r.draw_rect(x, y + 1.f, 3.f, 9.f, accent[0], accent[1], accent[2], 1.0f);
    r.draw_text(x + 8.f, y, label, s.dim[0], s.dim[1], s.dim[2], 1.0f, 0.78f);
}

// A rotary knob: dim track arc + accent value arc + pointer; name above, value
// below (both centred on cx). `mapped` tints the value arc toward the bridge teal.
inline void knob(Renderer2D& r, float cx, float cy, float rad, float v01,
                 const char* label, const char* valtext, const float* accent, bool mapped = false) {
    v01 = v01 < 0.f ? 0.f : (v01 > 1.f ? 1.f : v01);
    const Style& s = style();
    const float a0 = 2.3562f;                 // 135 deg (lower-left), sweeping CW (y-down)
    const float sweep = 4.7124f;              // 270 deg
    r.draw_arc(cx, cy, rad, a0, a0 + sweep, 2.6f, 28, s.recess[0] + 0.06f, s.recess[1] + 0.06f, s.recess[2] + 0.07f, 1.0f);
    const float vr = mapped ? s.teal[0] : accent[0];
    const float vg = mapped ? s.teal[1] : accent[1];
    const float vb = mapped ? s.teal[2] : accent[2];
    if (v01 > 0.001f)
        r.draw_arc(cx, cy, rad, a0, a0 + sweep * v01, 2.6f, std::max(2, int(28 * v01)), vr, vg, vb, 1.0f);
    const float pa = a0 + sweep * v01;        // pointer to the current value
    r.draw_line(cx, cy, cx + std::cos(pa) * (rad - 2.f), cy + std::sin(pa) * (rad - 2.f), 2.0f, s.text[0], s.text[1], s.text[2], 1.0f);
    if (label) {
        const float tw = r.text_width(label, 0.66f);
        r.draw_text(cx - tw * 0.5f, cy - rad - 12.f, label, s.dim[0], s.dim[1], s.dim[2], 1.0f, 0.66f);
    }
    if (valtext) {
        const float tw = r.text_width(valtext, 0.66f);
        r.draw_text(cx - tw * 0.5f, cy + rad + 3.f, valtext, s.text[0], s.text[1], s.text[2], 1.0f, 0.66f);
    }
}

}  // namespace vivid::ui
