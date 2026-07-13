#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// The visual output's FORMAT: its aspect ratio, its pixel size, and how it fits into a surface
// that may not share its shape (ADR-0014). These are the values behind the Output node's params —
// the Output node owns the output's identity, and VisualGraph sizes its render targets from it.
//
// Pure + header-only + wgpu-free, so the tables and the fit math are unit-testable headless.
namespace vivid {

enum class FitMode { Fit = 0, Fill = 1, Stretch = 2 };
inline constexpr const char* kFitLabels[] = { "Fit", "Fill", "Stretch" };
inline constexpr int kNumFits = 3;

// Aspect presets (the Output node's `aspect` param — an enum index).
inline constexpr const char* kAspectLabels[] = { "16:9", "4:3", "1:1", "9:16", "21:9", "16:10" };
inline constexpr float       kAspectRatios[] = { 16.f / 9.f, 4.f / 3.f, 1.f, 9.f / 16.f, 64.f / 27.f, 1.6f };
inline constexpr int         kNumAspects = 6;

// Size presets (the Output node's `height` param — an enum index). The HEIGHT is the preset and
// the width follows from the aspect, so the two params compose into a 6x6 grid instead of a
// combinatorial list of resolutions — and vertical output (9:16 @ 1080 = 608x1080) just works.
inline constexpr const char* kHeightLabels[] = { "360", "540", "720", "1080", "1440", "2160" };
inline constexpr uint32_t    kHeightValues[] = { 360u, 540u, 720u, 1080u, 1440u, 2160u };
inline constexpr int         kNumHeights = 6;

inline constexpr int kDefaultAspect = 0;   // 16:9
inline constexpr int kDefaultHeight = 2;   // 720  -> 1280x720

// Render-target size for an (aspect, height) preset pair. Both dimensions are kept even.
inline void output_size_for(int aspect_i, int height_i, uint32_t& w, uint32_t& h) {
    aspect_i = std::clamp(aspect_i, 0, kNumAspects - 1);
    height_i = std::clamp(height_i, 0, kNumHeights - 1);
    h = kHeightValues[height_i];
    w = static_cast<uint32_t>(std::lround(h * kAspectRatios[aspect_i]));
    w &= ~1u; h &= ~1u;
    w = std::max(w, 2u); h = std::max(h, 2u);
}

// The UV window that maps a destination quad onto a source texture of aspect `src_a`:
//   Fit     — the whole source is visible, bars appear (the window grows past [0,1])
//   Fill    — the destination is covered, the source is cropped (the window shrinks inside [0,1])
//   Stretch — the source is distorted to the destination (identity window)
// The present shader paints anything outside [0,1] black, which is what makes Fit letterbox.
struct BlitFit { float su, sv, ou, ov; };
inline BlitFit blit_fit(float src_a, float dst_a, FitMode m) {
    float su = 1.f, sv = 1.f;
    const float r = (src_a > 0.0001f && dst_a > 0.0001f) ? dst_a / src_a : 1.f;
    if (m == FitMode::Fit) {          // destination wider than source -> widen the u window (pillarbox)
        if (r > 1.f) su = r; else sv = 1.f / r;
    } else if (m == FitMode::Fill) {  // cover: shrink the window on the axis that would overflow
        if (r > 1.f) sv = 1.f / r; else su = r;
    }
    return { su, sv, (1.f - su) * 0.5f, (1.f - sv) * 0.5f };
}

}  // namespace vivid
