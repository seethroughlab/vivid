#pragma once
#include "ui/renderer_2d.h"
#include "ui/ui_style.h"   // Rect
#include <algorithm>

// ADR-0048/0049: the shared waveform component — "one waveform language" for the audio clip editor and
// the Sampler editor, so they can't drift. It is COMPOSABLE, not monolithic: a coordinate transform
// (normalized 0..1 sample position <-> screen x, with h-zoom/scroll + amplitude) plus one small draw
// method per overlay (peak bars, center line, loop dimming, handle, region, ticks, markers, playhead).
// Each editor composes the pieces it needs. The colors ARE the language — kept here so both match.
namespace vivid::ui {

// The waveform palette. These live HERE, not in each editor: when the marker colors were caller-side
// file-statics the two editors drifted (trim handles were yellow in the clip editor and amber in the
// Sampler). Every overlay method defaults to its own color, so composing the component is enough to be
// in the language; an editor passes an explicit color only for a marker class it alone has.
inline constexpr float kWaveTrim[3]   = { 0.94f, 0.63f, 0.19f };   // trim / loop / in-out handles (amber)
inline constexpr float kWaveDivider[3]= { 0.58f, 0.66f, 0.78f };   // slice boundaries (steel)
inline constexpr float kWaveRegion[3] = { 0.35f, 0.55f, 0.85f };   // the selected slice / region (blue)
inline constexpr float kWaveTransient[3] = { 0.50f, 0.50f, 0.36f };// detected onsets (dim ticks)
inline constexpr float kWaveWarp[3]   = { 0.96f, 0.62f, 0.24f };   // warp markers (orange; clip editor only)
inline constexpr float kWavePlayhead[3] = { 0.95f, 0.35f, 0.35f }; // playhead (red)

struct WaveformView {
    Rect   rect;            // the canvas the waveform fills (already clipped by the caller)
    double x0  = 0.0;       // leftmost visible normalized position (h-scroll)
    float  px  = 600.f;     // pixels per normalized unit (h-zoom)
    float  amp = 1.f;       // vertical amplitude zoom

    float  x_of(double n)  const { return rect.x + static_cast<float>(n - x0) * px; }
    double norm_of(double sx) const { return x0 + (sx - rect.x) / px; }
    float  mid()           const { return rect.y + rect.h * 0.5f; }

    // Peak-bar waveform. `bins` = per-bin absolute-peak envelope (0..1). Bars inside [lo,hi) read bright
    // (in the loop/selection), outside dim. Pass lo=0,hi=1 to brighten all (e.g. a whole loaded sample).
    void bins(Renderer2D& r, const float* peaks, int n, double lo = 0.0, double hi = 1.0) const {
        if (!peaks || n <= 0) return;
        const float binw = std::max(1.f, px / std::max(1, n));
        const double vLo = norm_of(rect.x), vHi = norm_of(rect.x + rect.w);
        const float midY = mid();
        for (int i = 0; i < n; ++i) {
            const double np = static_cast<double>(i) / n;
            if (np < vLo - 0.01 || np > vHi + 0.01) continue;
            const float wx = x_of(np);
            const float h = std::min(peaks[i] * rect.h * 0.46f * amp, rect.h * 0.49f);
            const bool in = (np >= lo && np < hi);
            r.draw_rect(wx, midY - h, binw, h * 2.f,
                        in ? 0.32f : 0.16f, in ? 0.72f : 0.26f, in ? 0.78f : 0.30f, 1.0f);
        }
    }
    void center_line(Renderer2D& r) const {
        r.draw_rect(rect.x, mid(), rect.w, 1.f, 0.18f, 0.20f, 0.24f, 1.0f);
    }
    // Dim everything outside [lo,hi) (the loop/trim window, or the played region).
    void dim_outside(Renderer2D& r, double lo, double hi) const {
        const float xl = x_of(lo), xr = x_of(hi);
        if (xl > rect.x)          r.draw_rect(rect.x, rect.y, std::min(xl, rect.x + rect.w) - rect.x, rect.h, 0.f, 0.f, 0.f, 0.45f);
        if (xr < rect.x + rect.w) r.draw_rect(std::max(xr, rect.x), rect.y, rect.x + rect.w - std::max(xr, rect.x), rect.h, 0.f, 0.f, 0.f, 0.45f);
    }
    // A vertical handle line at normalized `n` (trim edge, root note, …). Clipped to the canvas.
    void handle(Renderer2D& r, double n, const float* col = kWaveTrim, float w = 2.5f) const {
        const float x = x_of(n);
        if (x >= rect.x && x <= rect.x + rect.w)
            r.draw_rect(x - w * 0.5f, rect.y, w, rect.h, col[0], col[1], col[2], 1.0f);
    }
    // A translucent region [lo,hi) fill (a selected slice, an active zone).
    void region(Renderer2D& r, double lo, double hi, const float* col = kWaveRegion, float alpha = 0.20f) const {
        const float xl = std::max(x_of(lo), rect.x), xr = std::min(x_of(hi), rect.x + rect.w);
        if (xr > xl) r.draw_rect(xl, rect.y, xr - xl, rect.h, col[0], col[1], col[2], alpha);
    }
    // Bottom ticks (detected transients).
    void ticks(Renderer2D& r, const float* ns, int n, const float* col = kWaveTransient) const {
        for (int i = 0; i < n; ++i) { const float tx = x_of(ns[i]); if (tx >= rect.x && tx < rect.x + rect.w) r.draw_rect(tx, rect.y + rect.h - 9.f, 1.f, 8.f, col[0], col[1], col[2], 0.7f); }
    }
    // Full-height divider lines (slice boundaries, warp markers). `grab_tab` adds a top grab handle.
    void dividers(Renderer2D& r, const float* ns, int n, const float* col = kWaveDivider,
                  float alpha = 0.85f, bool grab_tab = false) const {
        for (int i = 0; i < n; ++i) {
            const float x = x_of(ns[i]);
            if (x < rect.x || x > rect.x + rect.w) continue;
            r.draw_rect(x, rect.y, 1.f, rect.h, col[0], col[1], col[2], alpha);
            if (grab_tab) r.draw_rect(x - 3.f, rect.y, 7.f, 6.f, col[0], col[1], col[2], 1.0f);
        }
    }
    void playhead(Renderer2D& r, double n, const float* col = kWavePlayhead) const {
        const float x = x_of(n);
        if (x >= rect.x && x < rect.x + rect.w) r.draw_rect(x, rect.y, 1.5f, rect.h, col[0], col[1], col[2], 1.0f);
    }
};

}  // namespace vivid::ui
