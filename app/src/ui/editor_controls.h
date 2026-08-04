#pragma once
#include "ui/renderer_2d.h"
#include "ui/ui_style.h"   // Style, Rect, hit(), item_box, toggle, draw_text_r, fit_text
#include <initializer_list>

// ADR-0048: the clip/sample editor control substrate. Small, reusable low-level controls with ONE
// bounded Rect shared by draw and hit-test — replacing the editor's old bare-text-at-magic-offset
// controls whose hit rectangles were authored separately (and mismatched). Every control reads as a
// control: a 1px frame, hard 90° corners, Vivid dark-steel tokens, and hover / pressed / active states.
//
// Layout: the caller lays out a control's Rect (packing left-to-right in the inspector strip), DRAWS it
// with that Rect, and HIT-TESTS the same Rect — so the two can never drift. Controls whose interior has
// parts (segmented cells, stepper ±) expose a pure `*_hit(rect, ...)` that maps a point to a part.

namespace vivid::ui {

// Center `text` (both axes) within `b`, ellipsizing to fit. Shared by the controls below.
inline void control_text(Renderer2D& r, Rect b, const char* text, float scale,
                         const float* c, float alpha = 1.0f) {
    if (!text || !*text) return;
    const std::string s = fit_text(r, text, b.w - 8.f, scale);
    const float tw = r.text_width(s.c_str(), scale);
    const float th = 15.f * scale;   // 15px base font (matches ui_style type ramp)
    r.draw_text(b.x + (b.w - tw) * 0.5f, b.y + (b.h - th) * 0.5f, s.c_str(), c[0], c[1], c[2], alpha, scale);
}

// ---- icon button: a bounded square/rect with a centered glyph; hover + pressed states. -------------
// Hit-test is just hit(rect, ...): the drawn Rect IS the click target.
inline void icon_button(Renderer2D& r, Rect b, const char* glyph,
                        bool hot = false, bool pressed = false, const float* accent = nullptr) {
    const Style& s = style();
    const float* bg = (pressed || hot) ? s.card_hi : s.card;
    r.draw_rect(b.x, b.y, b.w, b.h, bg[0], bg[1], bg[2], (pressed || hot) ? 1.0f : 0.9f);
    const float* fr = pressed ? s.sel : (hot ? s.border : s.border_soft);
    r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, fr[0], fr[1], fr[2], 1.0f);
    const float* tc = accent ? accent : ((hot || pressed) ? s.text : s.body);
    control_text(r, b, glyph, s.fs_label, tc);
}

// ---- segmented control: N cells across `b`, one selected (accent). `hot` = hovered cell (-1 none). ---
inline void segmented(Renderer2D& r, Rect b, std::initializer_list<const char*> items,
                      int selected, int hot = -1, const float* accent = nullptr) {
    const Style& s = style();
    const int n = static_cast<int>(items.size());
    if (n <= 0) return;
    const float cw = b.w / static_cast<float>(n);
    r.draw_rect(b.x, b.y, b.w, b.h, s.recess[0], s.recess[1], s.recess[2], 1.0f);
    int i = 0;
    for (const char* t : items) {
        const Rect cell{ b.x + i * cw, b.y, cw, b.h };
        const bool sel = (i == selected);
        if (sel)            r.draw_rect(cell.x, cell.y, cell.w, cell.h, s.card_hi[0], s.card_hi[1], s.card_hi[2], 1.0f);
        else if (i == hot)  r.draw_rect(cell.x, cell.y, cell.w, cell.h, s.card[0], s.card[1], s.card[2], 1.0f);
        if (i > 0) r.draw_rect(cell.x, b.y, 1.f, b.h, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);
        const float* tc = sel ? (accent ? accent : s.text) : (i == hot ? s.body : s.dim);
        control_text(r, cell, t, s.fs_label, tc);
        ++i;
    }
    r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
}
// segmented_hit(rect, n, mx, my) → cell index (or -1) lives in ui/layout.h (pure geometry, wgpu-free).

// ---- stepper: [ − | KICKER value | + ]. `hot` = hovered part (-1 dec, 0 body, +1 inc). --------------
inline void stepper(Renderer2D& r, Rect b, const char* kicker, const char* value, int hot = 0) {
    const Style& s = style();
    const float bw = b.h;   // square ± buttons at each end
    const Rect dec{ b.x, b.y, bw, b.h };
    const Rect inc{ b.x + b.w - bw, b.y, bw, b.h };
    const Rect body{ b.x + bw, b.y, b.w - 2.f * bw, b.h };
    r.draw_rect(body.x, body.y, body.w, body.h, s.recess[0], s.recess[1], s.recess[2], 1.0f);
    const float* dbg = (hot < 0) ? s.card_hi : s.card;
    const float* ibg = (hot > 0) ? s.card_hi : s.card;
    r.draw_rect(dec.x, dec.y, dec.w, dec.h, dbg[0], dbg[1], dbg[2], 1.0f);
    r.draw_rect(inc.x, inc.y, inc.w, inc.h, ibg[0], ibg[1], ibg[2], 1.0f);
    control_text(r, dec, "\xE2\x88\x92", s.fs_body, hot < 0 ? s.text : s.body);   // −
    control_text(r, inc, "+",           s.fs_body, hot > 0 ? s.text : s.body);
    // body: KICKER (dim, uppercase) + value (primary), left-aligned with a small gap
    if (kicker && *kicker) {
        const std::string k(kicker);
        r.draw_text(body.x + 7.f, body.y + (b.h - 15.f * s.fs_kicker) * 0.5f, k.c_str(),
                    s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_kicker);
    }
    if (value && *value)
        draw_text_r(r, body.x + body.w - 7.f, body.y + (b.h - 15.f * s.fs_value) * 0.5f,
                    value, s.text, 1.0f, s.fs_value);
    r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
}
// stepper_hit(rect, mx, my) → -1 dec / +1 inc / 0 body lives in ui/layout.h (pure geometry, wgpu-free).

// ---- compact menu button: a bounded label + chevron; opens a popup the caller owns. Hit = hit(rect). -
inline void menu_button(Renderer2D& r, Rect b, const char* label, bool hot = false, bool open = false) {
    const Style& s = style();
    const float* bg = (hot || open) ? s.card_hi : s.card;
    r.draw_rect(b.x, b.y, b.w, b.h, bg[0], bg[1], bg[2], 1.0f);
    const float* fr = open ? s.sel : (hot ? s.border : s.border_soft);
    r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, fr[0], fr[1], fr[2], 1.0f);
    const float* tc = (hot || open) ? s.text : s.body;
    if (label && *label)
        r.draw_text(b.x + 8.f, b.y + (b.h - 15.f * s.fs_label) * 0.5f,
                    fit_text(r, label, b.w - 22.f, s.fs_label).c_str(), tc[0], tc[1], tc[2], 1.0f, s.fs_label);
    draw_text_r(r, b.x + b.w - 7.f, b.y + (b.h - 15.f * s.fs_kicker) * 0.5f, "\xE2\x8C\x84", s.dim, 1.0f, s.fs_kicker);  // ⌄
}

// ---- hover-status pill: the contextual "what will happen" readout that replaces the footer crawl. ----
// Drawn at (x,y) as its top-left; sized to its text. Never a hit target.
inline void hover_status(Renderer2D& r, float x, float y, const char* text, const float* accent = nullptr) {
    if (!text || !*text) return;
    const Style& s = style();
    const float tw = r.text_width(text, s.fs_kicker), pad = 8.f, h = 18.f;
    r.draw_rect(x, y, tw + 2.f * pad, h, 0.043f, 0.051f, 0.059f, 0.87f);   // near-black scrim
    r.draw_rect_outline(x, y, tw + 2.f * pad, h, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
    const float* tc = accent ? accent : s.body;
    r.draw_text(x + pad, y + (h - 15.f * s.fs_kicker) * 0.5f, text, tc[0], tc[1], tc[2], 1.0f, s.fs_kicker);
}

}  // namespace vivid::ui
