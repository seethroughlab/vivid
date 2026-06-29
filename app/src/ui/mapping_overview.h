#pragma once
#include <string>
#include "ui/layout.h"   // Rect, std::max/min via <algorithm>

namespace vivid_poc { struct Session; }

namespace vivid::ui {
class Renderer2D;
class NodeGraph;

// Shared geometry for the mapping overview (so draw + hit-test agree).
struct OvGeom { float px, py, w, h, rowh, hdr; int vis; };
inline OvGeom ov_geom(int n, int win_w) {
    OvGeom o; o.w = 772.f; o.rowh = 24.f; o.hdr = 58.f;
    o.vis = std::max(1, std::min(n, 15));
    o.h = o.hdr + o.vis * o.rowh + 14.f;
    o.px = (win_w - o.w) * 0.5f; o.py = 84.f;
    return o;
}
// Per-row control rects (right-anchored): invert chip, amount/curve/lo/hi +/- steppers, clear.
struct OvRow {
    Rect inv, amtMinus, amtPlus, curMinus, curPlus, loMinus, loPlus, hiMinus, hiPlus, clear;
    float amtValX, curValX, loValX, hiValX;
};
inline OvRow ov_row(float px, float w, float ry) {
    return {
        { px + w - 330.f, ry, 26.f, 18.f },   // inv
        { px + w - 296.f, ry, 13.f, 18.f },   // amt -
        { px + w - 250.f, ry, 13.f, 18.f },   // amt +
        { px + w - 228.f, ry, 13.f, 18.f },   // curve -
        { px + w - 182.f, ry, 13.f, 18.f },   // curve +
        { px + w - 160.f, ry, 13.f, 18.f },   // lo -
        { px + w - 114.f, ry, 13.f, 18.f },   // lo +
        { px + w -  92.f, ry, 13.f, 18.f },   // hi -
        { px + w -  46.f, ry, 13.f, 18.f },   // hi +
        { px + w -  22.f, ry, 14.f, 18.f },   // clear
        px + w - 292.f, px + w - 224.f, px + w - 156.f, px + w - 88.f  // value text x: amt/curve/lo/hi
    };
}

// Human-readable label for a mapping destination id.
std::string mapping_dest_label(vivid_poc::Session* s, const std::string& dest);

// P28: the mapping overview — every source->dest mapping in one panel, with per-row
// amount/curve/range steppers + polarity toggle + clear. Direction-colored arrow.
void draw_mapping_overview(Renderer2D& ui, NodeGraph* g, vivid_poc::Session* s, int win_w, int win_h);

}  // namespace vivid::ui
