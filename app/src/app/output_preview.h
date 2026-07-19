#pragma once
#include "ui/layout.h"   // ui::Rect + the pure preview_* geometry helpers + kPreview*/kPanePad/kPanelHdH
#include <algorithm>     // std::clamp / std::min / std::max

// ADR-0014 / ADR-0025: the floating OUTPUT PREVIEW panel — the rendered visuals output that floats over
// the visuals-graph column, movable/resizable/closable. This is per-window VIEW state; ADR-0025's
// pressure-point #2 ("move interaction ownership out of Window into persistent view objects") extracts it
// from the Window state bag into its own small owner. Its geometry helpers + clamp are pure functions of
// the panel's x/y/w/aspect and the visuals-column rect, so they carry no Window/GPU dependency and are
// unit-testable (tests/test_output_preview.cpp).
namespace vivid {

struct OutputPreview {
    bool   show = true;
    float  x = -1.f, y = -1.f, w = 420.f;   // x < 0 = not placed yet (parked bottom-right on first use)
    bool   dragging = false, resizing = false;
    double grab_x = 0, grab_y = 0;          // cursor->panel offset while dragging/resizing
    // The live output aspect (w/h), cached from VisualGraph::rt_aspect() each frame so the geometry can
    // derive the panel height without this type knowing about the GPU layer. Derived state — not a param.
    float  out_aspect = 16.f / 9.f;

    ui::Rect panel()  const { return ui::preview_panel(x, y, w, out_aspect); }
    ui::Rect viewer() const { return ui::preview_viewer_rect(x, y, w, out_aspect); }
    ui::Rect header() const { return ui::preview_header_rect(x, y, w); }
    ui::Rect close()  const { return ui::preview_close_rect(x, y, w); }
    ui::Rect grip()   const { return ui::preview_grip_rect(x, y, w, out_aspect); }

    // Keep the panel inside the visuals column `col`. MUST run every frame, not just on placement: the
    // panel's height is derived from the output aspect, so switching the Output node to (say) 9:16 makes a
    // fixed-width preview much TALLER. Left unbounded that pushed the blit rect outside the framebuffer,
    // and wgpu aborts the process on an out-of-bounds scissor. Places the panel bottom-right on first use.
    void clamp(const ui::Rect& col) {
        if (col.w <= 0.f || col.h <= 0.f) return;
        const float max_w = std::max(ui::kPreviewMinW, col.w - 2.f * ui::kPanePad);
        const float max_h = std::max(ui::kPanelHdH + 40.f, col.h - 2.f * ui::kPanePad);
        w = std::clamp(w, ui::kPreviewMinW, std::min(ui::kPreviewMaxW, max_w));
        // Height follows the aspect — if that overflows the column, give width back until it fits.
        if (ui::kPanelHdH + ui::preview_body_h(w, out_aspect) > max_h)
            w = std::max(ui::kPreviewMinW, (max_h - ui::kPanelHdH) * out_aspect);
        const ui::Rect p = ui::preview_panel(0.f, 0.f, w, out_aspect);
        if (x < 0.f) {   // never placed: park it bottom-right of the column
            x = col.x + col.w - p.w - ui::kPanePad;
            y = col.y + col.h - p.h - ui::kPanePad;
        }
        x = std::clamp(x, col.x, std::max(col.x, col.x + col.w - p.w));
        y = std::clamp(y, col.y, std::max(col.y, col.y + col.h - p.h));
    }
};

}  // namespace vivid
