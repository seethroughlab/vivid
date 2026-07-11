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
    if (selected) r.draw_rect(x - 3.f, y - 3.f, w + 6.f, h + 6.f, s.sel[0], s.sel[1], s.sel[2], 1.0f);
    r.draw_rect(x - 1.f, y - 1.f, w + 2.f, h + 2.f, s.border[0], s.border[1], s.border[2], 1.0f);   // border
    r.draw_rect(x, y, w, h, s.card[0], s.card[1], s.card[2], 1.0f);                                 // body
    r.draw_rect(x + 1.f, y + 3.f, w - 2.f, 19.f, s.card_hi[0], s.card_hi[1], s.card_hi[2], 1.0f);   // header strip
    r.draw_rect(x, y, w, 3.f, accent[0], accent[1], accent[2], 1.0f);                               // accent bar
}

// A recessed preview panel (a node thumbnail well): a 1px frame + a near-black inset. Each editor
// fills it with its own content — the visuals graph blits a GPU texture, the audio graph draws a
// live waveform (node_waveform), an op can draw itself.
inline void node_preview_panel(Renderer2D& r, float x, float y, float w, float h) {
    r.draw_rect(x - 1.f, y - 1.f, w + 2.f, h + 2.f, 0.07f, 0.08f, 0.10f, 1.0f);   // frame
    r.draw_rect(x, y, w, h, 0.03f, 0.035f, 0.045f, 1.0f);                          // inset well
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
