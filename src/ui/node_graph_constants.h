#ifndef VIVID_UI_NODE_GRAPH_CONSTANTS_H
#define VIVID_UI_NODE_GRAPH_CONSTANTS_H

#include "operator_api/types.h"
#include <array>

namespace vivid::ui {

// Layout constants
static constexpr float kGraphX = 0.0f;
static constexpr float kGraphY = 0.0f;
static constexpr float kInspectorW = 320.0f;
static constexpr float kNodeW = 140.0f;
static constexpr float kColSpacing = 200.0f;
static constexpr float kRowSpacing = 16.0f;
static constexpr float kPortDotSize = 6.0f;
static constexpr float kLeftMargin = 30.0f;
static constexpr float kTopMargin = 30.0f;
static constexpr float kLineH = 18.0f;
static constexpr float kNodePadY = 8.0f;

// Domain body heights
static constexpr float kAccentBarH = 3.0f;
static constexpr float kGpuThumbH = 88.0f;     // 140 * 10/16 ~ 87.5
static constexpr float kAudioWaveH = 40.0f;
static constexpr float kControlSparkH = 30.0f;

// Colors (std::array instead of C-style arrays)
static constexpr std::array<float, 3> kNodeBg      = { 0.12f, 0.13f, 0.15f };
static constexpr std::array<float, 3> kNodeSelBg    = { 0.18f, 0.22f, 0.30f };
static constexpr std::array<float, 4> kConnColor    = { 0.5f, 0.6f, 0.65f, 0.7f };
static constexpr std::array<float, 4> kConnSelColor = { 0.6f, 0.75f, 0.85f, 0.9f };
static constexpr std::array<float, 3> kInspBg       = { 0.10f, 0.11f, 0.13f };
static constexpr std::array<float, 3> kAccent       = { 0.35f, 0.55f, 0.85f };
static constexpr std::array<float, 3> kDimText      = { 0.55f, 0.58f, 0.62f };
static constexpr std::array<float, 3> kSliderTrack  = { 0.18f, 0.19f, 0.22f };
static constexpr std::array<float, 3> kSliderFill   = { 0.25f, 0.42f, 0.68f };
static constexpr std::array<float, 3> kDarkBg       = { 0.07f, 0.08f, 0.09f };

// Domain accent colors
static constexpr std::array<float, 3> kGpuAccent     = { 0.306f, 0.804f, 0.769f };     // #4ECDC4 cyan
static constexpr std::array<float, 3> kAudioAccent   = { 0.941f, 0.627f, 0.188f };     // #F0A030 amber
static constexpr std::array<float, 3> kControlAccent = { 0.753f, 0.784f, 0.816f };     // #C0C8D0 gray

// Inspector widget sizes
static constexpr float kAdsrPreviewH = 80.0f;
static constexpr float kNotePatternPreviewH = 60.0f;
static constexpr float kSliderH = 10.0f;
static constexpr float kCheckboxSize = 14.0f;
static constexpr float kDropdownH = 18.0f;
static constexpr float kDropdownItemH = 20.0f;
static constexpr float kResInputW = 40.0f;
static constexpr float kInspPadX = 16.0f;
static constexpr float kInspContentW = kInspectorW - 2 * kInspPadX;

// Wire rendering
static constexpr float kWireHoverBright = 1.3f;
static constexpr float kWireThickness = 3.0f;
static constexpr float kWireHoverThickness = 5.0f;

// Bezier wire rendering
static constexpr int kBezierSegments = 30;

// Operator chooser popup
static constexpr int kChooserMaxVisible = 12;
static constexpr float kChooserW = 300.0f;
static constexpr float kChooserHeaderH = 28.0f;
static constexpr float kChooserItemH = 22.0f;
static constexpr float kChooserY = 80.0f;

// Context menu
static constexpr float kCtxMenuW = 120.0f;
static constexpr float kCtxMenuItemH = 22.0f;
static constexpr float kCtxMenuPadTop = 3.0f;

// Inspector scrollbar
static constexpr float kInspScrollbarW = 6.0f;
static constexpr float kInspScrollSpeed = 40.0f;     // px per scroll tick
static constexpr float kInspScrollbarMinThumb = 20.0f;

// Shared domain helpers (inline so they can live in the header)

inline const float* domain_color(VividDomain domain) {
    switch (domain) {
        case VIVID_DOMAIN_GPU:     return kGpuAccent.data();
        case VIVID_DOMAIN_AUDIO:   return kAudioAccent.data();
        case VIVID_DOMAIN_CONTROL: return kControlAccent.data();
        default:                   return kControlAccent.data();
    }
}

inline float domain_body_height(VividDomain domain, bool has_custom_thumb = false) {
    if (has_custom_thumb && domain != VIVID_DOMAIN_GPU) return kGpuThumbH;
    switch (domain) {
        case VIVID_DOMAIN_GPU:     return kGpuThumbH;
        case VIVID_DOMAIN_AUDIO:   return kAudioWaveH;
        case VIVID_DOMAIN_CONTROL: return kControlSparkH;
        default:                   return kControlSparkH;
    }
}

// Bezier evaluation
inline void eval_bezier(float t, float x0, float y0, float x1, float y1,
                        float x2, float y2, float x3, float y3,
                        float& ox, float& oy) {
    float u = 1.0f - t;
    float uu = u * u, uuu = uu * u;
    float tt = t * t, ttt = tt * t;
    ox = uuu * x0 + 3 * uu * t * x1 + 3 * u * tt * x2 + ttt * x3;
    oy = uuu * y0 + 3 * uu * t * y1 + 3 * u * tt * y2 + ttt * y3;
}

// Point-to-segment squared distance
inline float point_seg_dist2(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float len2 = dx * dx + dy * dy;
    float t = (len2 > 0) ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.0f;
    t = std::max(0.0f, std::min(1.0f, t));
    float cx = ax + t * dx, cy = ay + t * dy;
    float ex = px - cx, ey = py - cy;
    return ex * ex + ey * ey;
}

// Wire geometry helpers

// Returns the 4 bezier control points for a connection
inline std::array<std::pair<float,float>, 4> wire_bezier_points(float sx, float sy, float ex, float ey) {
    float cp_off = std::fabs(ex - sx) * 0.5f;
    return {{
        {sx, sy},
        {sx + cp_off, sy},
        {ex - cp_off, ey},
        {ex, ey}
    }};
}

// Returns the 3 Z-route segments (each as a pair of endpoints)
inline std::array<std::pair<std::pair<float,float>, std::pair<float,float>>, 3>
wire_zroute_segments(float sx, float sy, float ex, float ey) {
    float mid_x = (sx + ex) * 0.5f;
    return {{
        {{sx, sy}, {mid_x, sy}},
        {{mid_x, sy}, {mid_x, ey}},
        {{mid_x, ey}, {ex, ey}}
    }};
}

// Traverse a wire (bezier or z-route) and call visit(x0, y0, x1, y1) for each segment.
template<typename Visitor>
void traverse_wire(float ssx, float ssy, float sex, float sey,
                   bool bezier, Visitor&& visit) {
    if (bezier) {
        auto pts = wire_bezier_points(ssx, ssy, sex, sey);
        float px = ssx, py = ssy;
        for (int seg = 1; seg <= kBezierSegments; ++seg) {
            float t = static_cast<float>(seg) / kBezierSegments;
            float nx, ny;
            eval_bezier(t, pts[0].first, pts[0].second, pts[1].first, pts[1].second,
                        pts[2].first, pts[2].second, pts[3].first, pts[3].second, nx, ny);
            visit(px, py, nx, ny);
            px = nx; py = ny;
        }
    } else {
        auto segs = wire_zroute_segments(ssx, ssy, sex, sey);
        for (const auto& seg : segs) {
            visit(seg.first.first, seg.first.second, seg.second.first, seg.second.second);
        }
    }
}

// Performance bar layout
static constexpr float kPerfBarH = 28.0f;
static constexpr float kPerfBarPadX = 10.0f;
static constexpr float kPerfSepW = 1.0f;
static constexpr float kPerfSepMargin = 8.0f;
static constexpr float kPerfMiniGraphW = 60.0f;
static constexpr float kPerfMiniGraphH = 14.0f;
static constexpr float kPerfExpandedW = 200.0f;
static constexpr float kPerfExpandedH = 100.0f;

// Performance bar colors
static constexpr std::array<float, 3> kPerfFpsColor = { 0.40f, 0.85f, 0.45f };   // green
static constexpr std::array<float, 3> kPerfMsColor  = { 0.45f, 0.65f, 0.95f };   // blue
static constexpr std::array<float, 3> kPerfMemColor = { 0.95f, 0.65f, 0.25f };   // orange
static constexpr std::array<float, 4> kPerfBarBg    = { 0.06f, 0.07f, 0.08f, 0.85f };

// Performance ring buffer
static constexpr uint32_t kPerfHistoryLen = 128;
static constexpr uint32_t kPerfMemSampleInterval = 30;

} // namespace vivid::ui

#endif // VIVID_UI_NODE_GRAPH_CONSTANTS_H
