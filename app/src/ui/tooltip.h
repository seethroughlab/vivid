#pragma once
#include "ui/editor_controls.h"   // hover_status — the house "what will happen" pill
#include "ui/layout.h"            // Rect
#include <cstdio>                 // snprintf
#include <cstring>                // strncmp

// A dwell-gated hover tooltip. The app is immediate-mode: every control re-derives `hov` each frame
// and throws it away, so a tooltip needs the one piece of state that isn't recomputable — how long
// the cursor has been on the SAME item. `TipState` is that state; it lives on the Window next to
// `toasts` and is ticked once per frame by whoever owns the surface being tipped.
//
// The pill body is `hover_status()` (ui/editor_controls.h), so tooltips look like the clip/sampler
// editors' contextual readout instead of introducing a second chrome vocabulary.
namespace vivid::ui {

constexpr double kTipDelay = 0.5;   // seconds of dwell before a tip appears

struct TipState {
    char   text[96] = {};   // a fixed buffer, not a const char*: some tips are composed per frame
    // Where the pill hangs, in screen coords — a PLACEMENT rect, not the hit rect. A caller tipping
    // a row of controls of differing heights should pass a common band (e.g. the whole transport
    // bar) so the pill doesn't bob up and down as the cursor crosses the row.
    Rect   anchor{};
    double since = 0.0;     // when the cursor entered THIS item (dwell clock)
    bool   live  = false;
};

// Offer a tip for the item under the cursor. Re-offering the same text keeps the dwell clock
// running; a different text restarts it, so sliding along a toolbar re-arms rather than snapping
// the pill from button to button.
inline void tip_set(TipState& t, const Rect& anchor, const char* text, double now) {
    if (!text || !*text) { t.live = false; return; }
    if (!t.live || std::strncmp(t.text, text, sizeof t.text - 1) != 0) {
        std::snprintf(t.text, sizeof t.text, "%s", text);
        t.since = now;
    }
    t.anchor = anchor;
    t.live   = true;
}

inline void tip_clear(TipState& t) { t.live = false; }

// Draw the pill under its anchor once the dwell has elapsed. A no-op otherwise, so the caller can
// call this unconditionally. Must run at identity transform (the frame loop's overlay pass).
inline void draw_tooltip(Renderer2D& r, const TipState& t, int win_w, double now) {
    if (!t.live || now - t.since < kTipDelay) return;
    const Style& s = style();
    const float tw = r.text_width(t.text, s.fs_kicker) + 16.f;   // + hover_status's 2*pad
    float x = t.anchor.x;
    if (x + tw > win_w - 6.f) x = win_w - 6.f - tw;              // keep it inside the window
    if (x < 6.f) x = 6.f;
    hover_status(r, x, t.anchor.y + t.anchor.h + 6.f, t.text);
}

}  // namespace vivid::ui
