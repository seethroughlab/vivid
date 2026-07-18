#pragma once
#include "ui/node_canvas.h"   // NodeView, Rect, node_card/grid/wire, error vocab

// ADR-0023 Layer 2 — the GraphCanvas: the shared graph-area DRAW skeleton both node editors (the
// visuals `node_graph` and the per-track `audio_node_graph`) render through, built on the
// `node_canvas.h` marks. Each editor owns one as a member and primes its view/region per frame
// (mirroring how AudioNodeGraph is primed via set_source/set_bounds) — no persisted-storage change.
//
// v1 owns only the genuinely-shared, behavior-preserving skeleton: the node-card chrome (error border
// + card + selection ring + error badge), the background grid, and the ghost/drag-preview wire.
// Everything that legitimately diverges stays in each editor's own draw loop: the coordinate space
// (visual draws in WORLD space via set_transform; audio bakes SCREEN coords in layout()), wire
// enumeration/endpoints/color, port + label content, the preview-well contents (GPU thumbnail /
// sparkline / waveform), and the whole audio param band.
namespace vivid::ui {

class GraphCanvas {
public:
    // Primed by the editor each frame (only grid() reads these today).
    void set_view(const NodeView& v) { view_ = v; }
    void set_region(const Rect& r)   { region_ = r; }

    // The shared node-card chrome. COORDINATE-AGNOSTIC: it draws relative to `rect` with the mark
    // metrics, so whether the chrome ends up zoom-scaled is decided by the ambient renderer transform
    // the caller already set (world transform for the visuals graph -> scales; identity for the audio
    // graph -> constant). That is what lets both editors share it without unifying their conventions.
    // `broken` gates the ADR-0019 red border + "!" badge; `selected` draws the blue selection ring.
    void card(Renderer2D& r, const Rect& rect, const float accent[3], bool selected, bool broken) const {
        if (broken) node_error_border(r, rect.x, rect.y, rect.w, rect.h);
        node_card(r, rect.x, rect.y, rect.w, rect.h, accent, selected);
        if (broken) node_error_badge(r, rect.x, rect.y);
    }

    // World-space background grid over the primed region (the visuals graph draws it; the audio graph
    // stays grid-less for now — a deliberate look decision, not a limitation of the canvas).
    void grid(Renderer2D& r) const {
        node_grid(r, view_, region_.x, region_.y, region_.x + region_.w, region_.y + region_.h);
    }

    // A drag-preview wire from a source port to the cursor. The endpoints (and which port they are)
    // are the editor's business — the canvas only owns the bezier + color.
    void ghost_wire(Renderer2D& r, float x0, float y0, float x1, float y1, const float col[3]) const {
        node_wire(r, x0, y0, x1, y1, col[0], col[1], col[2]);
    }

private:
    NodeView view_;
    Rect     region_{};
};

}  // namespace vivid::ui
