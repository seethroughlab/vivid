#pragma once
#include "ui/layout.h"   // Rect, std::max/min via <algorithm>

namespace vivid { class ShaderLibrary; }

namespace vivid::ui {
class Renderer2D;

// ADR-0021 / P1 — the shader library view: an overlay listing EVERY shader the library found,
// including the ones that failed to parse or were shadowed. Those carry `registered == false`, so
// they are absent from the OpRegistry and therefore from the Tab chooser — this panel is the only
// place they (and their error) are visible. Geometry lives here so the draw and the modal hit-test
// in input.cpp agree, exactly like mapping_overview.

struct ShaderViewGeom { float px, py, w, h, rowh, hdr; int vis; };
inline ShaderViewGeom shader_view_geom(int n, int win_w) {
    ShaderViewGeom o; o.w = 640.f; o.rowh = 30.f; o.hdr = 58.f;
    o.vis = std::max(1, std::min(n, 14));
    o.h = o.hdr + o.vis * o.rowh + 14.f;
    o.px = (win_w - o.w) * 0.5f; o.py = 84.f;
    return o;
}
// Per-row action rects (right-anchored): Open (in editor), Fork (into the user tier).
struct ShaderViewRow { Rect open, fork; };
inline ShaderViewRow shader_view_row(float px, float w, float ry) {
    return {
        { px + w - 108.f, ry + 4.f, 46.f, 20.f },   // open
        { px + w -  54.f, ry + 4.f, 44.f, 20.f },   // fork
    };
}

// Derive a free op-type name for forking `base` (base2, base3, …), using `taken(name)` to test
// occupancy. Pure, so it is unit-testable without a registry.
template <typename Taken>
inline std::string shader_fork_name(const std::string& base, Taken taken) {
    for (int i = 2; i < 1000; ++i) {
        std::string cand = base + std::to_string(i);
        if (!taken(cand)) return cand;
    }
    return base + "_copy";
}

void draw_shader_library_view(Renderer2D& ui, const ShaderLibrary& lib, int win_w, int win_h);

}  // namespace vivid::ui
