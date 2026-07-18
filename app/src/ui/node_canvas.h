#pragma once
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

// Pan/zoom view transform (world <-> screen). Both editors kept private copies of this; now shared.
struct NodeView {
    float ox = 0.f, oy = 0.f, scale = 1.f;
    void to_world(double sx, double sy, double& wx, double& wy) const {
        wx = (sx - ox) / scale; wy = (sy - oy) / scale;
    }
    // Zoom by `factor` about the screen point (sx,sy), keeping that world point under the cursor.
    void zoom_at(double sx, double sy, float factor) {
        double wx, wy; to_world(sx, sy, wx, wy);
        scale = std::clamp(scale * factor, 0.35f, 3.0f);
        ox = static_cast<float>(sx) - static_cast<float>(wx) * scale;
        oy = static_cast<float>(sy) - static_cast<float>(wy) * scale;
    }
    void pan(float dx, float dy) { ox += dx; oy += dy; }
};

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
// Below its header, a node card stacks a column of left-edge PORT ROWS — a signal input (when the
// node takes one), then one row per exposed param, then an optional trailing "+param" add-row — and
// a recessed preview well fills the space beneath them. Both graph editors share this vertical
// structure; describing it in one place means a card's height, its ports' drawn centres, and their
// hit-rects are all one formula and can't drift apart. The height metrics are fields because the two
// editors use different pitches.
struct CardPorts {
    float header_h = 22.f;   // title-strip height
    float row_h    = 15.f;   // one port-row height
    float prev_h   = 30.f;   // preview-well height reserved beneath the rows
    bool  sig_in   = false;  // row 0 is a signal input
    int   params   = 0;      // one row per exposed param
    bool  add_row  = false;  // a trailing "+param" row (e.g. a plugin card)

    int   rows()          const { return (sig_in ? 1 : 0) + params + (add_row ? 1 : 0); }
    int   rows_reserved() const { return std::max(1, rows()); }   // a card always reserves >= 1 row
    float height()        const { return header_h + rows_reserved() * row_h + prev_h + 6.f; }

    int   sig_in_row()     const { return 0; }
    int   param_row(int k) const { return (sig_in ? 1 : 0) + k; }
    int   add_row_index()  const { return (sig_in ? 1 : 0) + params; }

    // Centre-y of port row `k` on a card whose top edge is at `card_y`.
    float row_cy(float card_y, int k) const { return card_y + header_h + k * row_h + row_h * 0.5f; }
    // The recessed preview well beneath the rows, within card rect `c` (c.h should be height()).
    Rect  preview(const Rect& c) const {
        const float y = c.y + header_h + rows_reserved() * row_h + 1.f;
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

}  // namespace vivid::ui
