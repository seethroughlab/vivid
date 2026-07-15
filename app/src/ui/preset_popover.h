#pragma once
#include "ui/layout.h"   // Rect, std::max/min

namespace vivid { struct App; }

namespace vivid::ui {
class Renderer2D;

// ADR-0021/P4 — the node-preset popover: lists the presets for the selected node's op type (click a
// row to recall it, × to delete a user preset), plus a "Save current" row at the top that snapshots
// the node's params under an auto-incremented name. Geometry lives here so the draw (frame.cpp) and
// the modal hit-test (input.cpp) agree, exactly like shader_library_view / mapping_overview.

struct PresetGeom { float px, py, w, h, rowh, hdr; int vis; };
inline PresetGeom preset_geom(int n, int win_w) {
    PresetGeom o; o.w = 380.f; o.rowh = 26.f; o.hdr = 56.f;
    o.vis = std::max(0, std::min(n, 12));
    o.h = o.hdr + (o.vis + 1) * o.rowh + 12.f;   // +1 for the "Save current" row
    o.px = (win_w - o.w) * 0.5f; o.py = 96.f;
    return o;
}
// The "Save current" action row (top), then the y of preset row i (0-based).
inline Rect preset_save_row(float px, float py, float w, float hdr, float rowh) {
    return { px + 8.f, py + hdr, w - 16.f, rowh - 2.f };
}
inline Rect preset_list_row(float px, float py, float w, float hdr, float rowh, int i) {
    return { px + 8.f, py + hdr + (i + 1) * rowh, w - 16.f, rowh - 2.f };
}
// The delete (×) hit box at the right end of a preset row.
inline Rect preset_del_rect(const Rect& row) { return { row.x + row.w - 22.f, row.y, 20.f, row.h }; }

// Draw the popover for node `node_idx` (its op type names the preset set). No-op if node_idx < 0.
void draw_preset_popover(Renderer2D& ui, App& app, int node_idx, int win_w, int win_h);

}  // namespace vivid::ui
