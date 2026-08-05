#pragma once
#include "ui/node_view.h"     // NodeView — the pure world<->screen camera (transform + gesture math)
#include "ui/renderer_2d.h"
#include "ui/ui_style.h"
#include <algorithm>
#include <cmath>

// Shared node-editor substrate. The view transform + the generic card / wire / port / grid drawing
// vocabulary that BOTH node editors — the visuals graph (`node_graph`) and the per-track audio graph
// (`audio_node_graph`) — draw through, so they render identically (steel cards, hard angles, blue
// selection) and the interaction math lives in exactly one place. Domain specifics (GPU thumbnails,
// data-node sparklines, param bands) stay in each editor; everything here is graph-agnostic.
namespace vivid::ui {

// NodeView (the pan/zoom world<->screen camera + its gesture math) lives in ui/node_view.h — a pure,
// renderer-free header so the interaction math is a standalone, headlessly-testable unit.

// The audio graph stores a zoom + a pan that are RELATIVE to its graph region (so the graph stays
// put when the region moves — e.g. when the param band grows/shrinks). This reconstructs the
// absolute world->screen NodeView the shared transform math uses. (The visuals graph stores an
// absolute NodeView directly; the audio graph derives one here — ADR-0023.)
inline NodeView region_view(const Rect& region, float zoom, float pan_x, float pan_y) {
    return { region.x + pan_x, region.y + pan_y, zoom };
}

// A bezier wire from an output port to an input port.
inline void node_wire(Renderer2D& r, float x0, float y0, float x1, float y1, float cr, float cg, float cb) {
    const int N = 26; float xs[N], ys[N];
    float dx = std::fabs(x1 - x0) * 0.5f; if (dx < 28.f) dx = 28.f;
    const float c1x = x0 + dx, c1y = y0, c2x = x1 - dx, c2y = y1;
    for (int i = 0; i < N; ++i) {
        const float t = i / float(N - 1), u = 1.f - t;
        xs[i] = u*u*u*x0 + 3*u*u*t*c1x + 3*u*t*t*c2x + t*t*t*x1;
        ys[i] = u*u*u*y0 + 3*u*u*t*c1y + 3*u*t*t*c2y + t*t*t*y1;
    }
    r.draw_polyline(xs, ys, N, 2.2f, cr, cg, cb, 1.0f);
}

// A port marker (hard-edged square nub).
inline void node_port(Renderer2D& r, float px, float py, float rad, float cr, float cg, float cb) {
    r.draw_rect(px - rad, py - rad, rad * 2.f, rad * 2.f, cr, cg, cb, 1.0f);
}

// The world-space background grid across the region [x0,y0]-[x1,y1], 1px-on-screen lines.
inline void node_grid(Renderer2D& r, const NodeView& v, float x0, float y0, float x1, float y1) {
    const float gs = 38.f;
    double wl, wt, wr, wb; v.to_world(x0, y0, wl, wt); v.to_world(x1, y1, wr, wb);
    const float lw = 1.f / v.scale;
    for (float gx = std::floor(static_cast<float>(wl) / gs) * gs; gx < static_cast<float>(wr); gx += gs)
        r.draw_rect(gx, static_cast<float>(wt), lw, static_cast<float>(wb - wt), 0.105f, 0.115f, 0.14f, 1.0f);
    for (float gy = std::floor(static_cast<float>(wt) / gs) * gs; gy < static_cast<float>(wb); gy += gs)
        r.draw_rect(static_cast<float>(wl), gy, static_cast<float>(wr - wl), lw, 0.105f, 0.115f, 0.14f, 1.0f);
}

// A node card: 1px steel border + flat body + header strip + a top accent bar. `selected` draws the
// blue selection ring behind it. The shared node look for both editors.
inline void node_card(Renderer2D& r, float x, float y, float w, float h,
                      const float* accent, bool selected) {
    const Style& s = style();
    item_box(r, { x, y, w, h }, accent, false, selected, AccentEdge::Top);
    r.draw_rect(x + 1.f, y + 3.f, w - 2.f, 19.f, s.card_hi[0], s.card_hi[1], s.card_hi[2], 1.0f);   // header strip
}

// --- Card port-row + preview-well layout (ADR-0023) -------------------------------------------
// Below its header, a node card stacks a column of left-edge PORT ROWS — some LEAD input rows (a
// signal input on an audio node; texture inputs on a visual op), then one row per exposed/visible
// param, then an optional trailing "+param" add-row — and a preview/thumbnail region fills the space
// beneath them. Both graph editors share this vertical structure; describing it in one place means a
// card's height and its ports' drawn centres are one formula and can't drift apart. The metrics
// (heights, pitch, tail) are fields because the two editors differ: audio uses a 22px header / 15px
// rows / a fill-to-bottom waveform well; the visuals graph uses 30 / 18 / a fixed 46px thumbnail.
struct CardPorts {
    float header_h  = 22.f;   // title-strip height
    float head_h    = 0.f;    // a preview strip ABOVE the rows, below the header (0 = none). Used for a
                              // note-generator's always-visible live thumbnail (its rows would push a
                              // bottom well off the dock). Rows + hit-tests shift down by it.
    float row_h     = 15.f;   // one port-row height
    float tail_h    = 30.f;   // preview/thumbnail region height below the rows (0 = none)
    float tail_pad  = 6.f;    // gap below the tail region to the card bottom
    int   lead_rows = 0;      // input rows before the params (audio: 0/1 signal-in; visual: 0..2 texture ins)
    int   params    = 0;      // one row per exposed/visible param
    bool  add_row   = false;  // a trailing "+param" row (e.g. a plugin card)

    int   rows()          const { return lead_rows + params + (add_row ? 1 : 0); }
    int   rows_reserved() const { return std::max(1, rows()); }   // a card always reserves >= 1 row
    float height()        const { return header_h + head_h + rows_reserved() * row_h + tail_h + tail_pad; }

    int   param_row(int k) const { return lead_rows + k; }   // row index of the k-th param
    int   add_row_index()  const { return lead_rows + params; }

    // Centre-y of port row `k` on a card whose top edge is at `card_y` (lead rows are 0..lead_rows-1).
    float row_cy(float card_y, int k) const { return card_y + header_h + head_h + k * row_h + row_h * 0.5f; }
    // The head preview strip (below the header, above the rows), within card rect `c`. Empty if head_h==0.
    Rect  head_preview(const Rect& c) const {
        return { c.x + 6.f, c.y + header_h + 1.f, c.w - 12.f, head_h - 2.f };
    }
    // A fill-to-bottom recessed preview well beneath the rows (the audio convention), within card
    // rect `c`. The visuals graph places a fixed-height thumbnail itself — this is not shared.
    Rect  preview(const Rect& c) const {
        const float y = c.y + header_h + head_h + rows_reserved() * row_h + 1.f;
        return { c.x + 6.f, y, c.w - 12.f, (c.y + c.h) - y - 4.f };
    }
};

// --- Broken-node vocabulary (ADR-0019). A node the engine knows is broken must LOOK broken, in
// both editors, from one implementation. Each editor supplies its own error string (a shader that
// won't compile, an op type that isn't registered, a plugin that failed to load); the drawing is
// shared here. ---

// A health-tinted error frame, drawn BEHIND node_card (same footprint as the active-output ring).
inline void node_error_border(Renderer2D& r, float x, float y, float w, float h) {
    const Style& s = style();
    r.draw_rect(x - 2.f, y - 2.f, w + 4.f, h + 4.f, s.red[0], s.red[1], s.red[2], 1.0f);
}

// The clickable "!" chip at the header's left edge. Its geometry is a pure function of the card
// origin so the editor's hit-test can recompute it without threading a rect through. When a node
// carries an error, the editor draws its type label shifted right by `node_error_label_shift` to
// make room. Returns nothing; use node_error_badge_rect for hit-testing.
inline Rect node_error_badge_rect(float x, float y) { return { x + 4.f, y + 5.f, 13.f, 13.f }; }
inline constexpr float node_error_label_shift = 14.f;
inline void node_error_badge(Renderer2D& r, float x, float y) {
    const Style& s = style();
    const Rect b = node_error_badge_rect(x, y);
    r.draw_rect(b.x, b.y, b.w, b.h, s.red[0], s.red[1], s.red[2], 1.0f);
    r.draw_text(b.x + 4.5f, b.y + 0.5f, "!", 0.10f, 0.02f, 0.03f, 1.0f, 0.82f);
}

// The first line of an error, over the preview well (the node keeps rendering its last-good output —
// a source falls back to black, a filter passes through — so the picture alone would say nothing).
inline void node_error_note(Renderer2D& r, float tx, float ty, float tw, float th, const std::string& msg) {
    std::string first = msg.substr(0, msg.find('\n'));
    if (first.size() > 24) first = first.substr(0, 23) + "\xE2\x80\xA6";
    r.draw_rect(tx, ty + th - 14.f, tw, 14.f, 0.15f, 0.05f, 0.05f, 0.92f);
    r.draw_text(tx + 4.f, ty + th - 12.f, first.c_str(), 0.98f, 0.55f, 0.55f, 1.0f, 0.62f);
}

// ADR-0033 P1: the marquee (rubber-band) rectangle drawn during a multi-select drag. Drawn in WORLD
// space by both editors (inside the shared NodeView transform) so the band tracks the cards under
// zoom/pan. `m` may have inverted extents at the call site; normalize before drawing.
inline void node_marquee(Renderer2D& r, const Rect& m) {
    const Style& s = style();
    r.draw_rect(m.x, m.y, m.w, m.h, s.sel[0], s.sel[1], s.sel[2], 0.14f);              // translucent fill
    r.draw_rect_outline(m.x, m.y, m.w, m.h, 1.f, s.sel[0], s.sel[1], s.sel[2], 0.85f); // 1px border
}

// ADR-0033 P5: a sticky-note card — a warm, paper-like panel with a gold top strip, deliberately
// unlike the steel op cards so a note reads as an annotation, not a graph node. The text (with
// wrapping + edit caret) is drawn by the editor over this chrome.
inline void node_sticky(Renderer2D& r, const Rect& c, bool selected) {
    const Style& s = style();
    r.draw_rect(c.x, c.y, c.w, c.h, 0.17f, 0.155f, 0.105f, 0.96f);            // warm dark-paper fill
    r.draw_rect(c.x, c.y, c.w, 3.f, s.gold[0], s.gold[1], s.gold[2], 0.90f);  // gold "note" top strip
    const float* b = selected ? s.sel : s.border;
    r.draw_rect_outline(c.x, c.y, c.w, c.h, 1.f, b[0], b[1], b[2], selected ? 0.95f : 0.70f);
}

// A recessed preview panel (a node thumbnail well): a 1px frame + a near-black inset. Each editor
// fills it with its own content — the visuals graph blits a GPU texture, the audio graph draws a
// live waveform (node_waveform), an op can draw itself.
inline void node_preview_panel(Renderer2D& r, float x, float y, float w, float h) {
    recess(r, { x, y, w, h }, true);
}

// Draw a waveform (n samples, roughly in [-1,1]) as a centred polyline inside the rect. Clamped so a
// hot signal stays in the well. The default audio-node preview: the node's real output signal.
inline void node_waveform(Renderer2D& r, float x, float y, float w, float h,
                          const float* v, int n, float cr, float cg, float cb) {
    if (!v || n < 2 || w <= 1.f) return;
    const float midY = y + h * 0.5f, amp = h * 0.46f;
    constexpr int kMax = 256; if (n > kMax) n = kMax;
    float xs[kMax], ys[kMax];
    for (int i = 0; i < n; ++i) {
        xs[i] = x + w * (i / static_cast<float>(n - 1));
        ys[i] = midY - std::clamp(v[i], -1.f, 1.f) * amp;
    }
    r.draw_polyline(xs, ys, static_cast<uint32_t>(n), 1.2f, cr, cg, cb, 0.95f);
}

// Draw a per-bin peak envelope (n bins, each 0..1) as centred mirrored bars — a static waveform
// thumbnail of a loaded sample (Sampler node), distinct from the live scope polyline above.
inline void node_sample_peaks(Renderer2D& r, float x, float y, float w, float h,
                              const float* bins, int n, float cr, float cg, float cb) {
    if (!bins || n < 1 || w <= 1.f) return;
    const float midY = y + h * 0.5f, amp = h * 0.5f - 1.f;
    const float colw = w / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
        const float a = std::clamp(bins[i], 0.f, 1.f) * amp;
        r.draw_rect(x + colw * i, midY - a, std::max(1.f, colw - 0.5f), a * 2.f + 1.f, cr, cg, cb, 0.9f);
    }
}

}  // namespace vivid::ui
