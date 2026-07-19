#pragma once
#include "ui/node_canvas.h"    // NodeView, Rect, node_card/grid/wire, error vocab
#include "ui/graph_adapter.h"  // GraphModelAdapter + AdapterNode — the node set draw_cards iterates
#include <vector>

// ADR-0023 Layer 2 — the GraphCanvas: the shared graph-area DRAW skeleton both node editors (the
// visuals `node_graph` and the per-track `audio_node_graph`) render through, built on the
// `node_canvas.h` marks. Each editor owns one as a member; the canvas is the SOLE owner of that
// editor's pan/zoom camera (ADR-0023 #1 — the editor reaches it through `canvas_.view()` rather than
// keeping its own `NodeView`), and the editor primes the draw region per frame.
//
// It owns only the genuinely-shared, behavior-preserving skeleton: the node-card chrome (error border
// + card + selection ring + error badge), the background grid, and the ghost/drag-preview wire.
// Everything that legitimately diverges stays in each editor's own draw loop: wire enumeration/
// endpoints/color, port + label content, the preview-well contents (GPU thumbnail / sparkline /
// waveform), and the whole audio param band. (Both editors now draw WORLD-space through the shared
// NodeView transform — ADR-0023 #3 moved the audio graph off its old screen-space `layout()` baking.)
namespace vivid::ui {

// ADR-0023 #3c: the per-card domain drawing GraphCanvas::draw_cards calls around the shared card()
// chrome. before_card draws UNDER the card (e.g. an active-output ring); after_card draws the domain
// content on top (label, ports, preview well). Both are const — the card loop is a pure read of the model.
struct CardDelegate {
    virtual ~CardDelegate() = default;
    virtual void before_card(Renderer2D& r, const AdapterNode& n, int idx) const { (void)r; (void)n; (void)idx; }
    virtual void after_card(Renderer2D& r, const AdapterNode& n, int idx) const = 0;
};

class GraphCanvas {
public:
    // The canvas OWNS the pan/zoom camera (ADR-0023 #1): both editors mutate it through view()
    // instead of keeping their own NodeView copy. The owning editor persists it with the session.
    NodeView&       view()       { return view_; }
    const NodeView& view() const { return view_; }
    void set_region(const Rect& r) { region_ = r; }   // draw region, primed by the editor each frame

    // ADR-0023 #3d: the shared CAMERA gestures — pan / zoom-around-cursor / reset — the common
    // interaction both editors drive identically on the canvas-owned view. The math (incl. the shared
    // zoom clamp) lives on NodeView (ui/node_view.h — a pure, unit-tested unit); the canvas just
    // forwards. (Select, node-drag, rewire and the domain-specific gestures stay per-editor: their
    // hit-tests are entangled with each model's port geometry and edge types.)
    void pan(float dx, float dy) { view_.pan(dx, dy); }
    void zoom_at(double sx, double sy, float factor) { view_.zoom_at(sx, sy, factor); }
    void reset(const Rect& region) { view_.reset_to(region.x, region.y); }   // fit the region at identity scale

    // The shared node-card chrome. Draws relative to `rect` in the ambient (world) transform both
    // editors now set, so the chrome scales with zoom uniformly (ADR-0023 #3). `broken` gates the
    // ADR-0019 red border + "!" badge; `selected` draws the blue selection ring.
    void card(Renderer2D& r, const Rect& rect, const float accent[3], bool selected, bool broken) const {
        if (broken) node_error_border(r, rect.x, rect.y, rect.w, rect.h);
        node_card(r, rect.x, rect.y, rect.w, rect.h, accent, selected);
        if (broken) node_error_badge(r, rect.x, rect.y);
    }

    // ADR-0023 #3c: the shared CARD LOOP. Iterates the adapter's nodes and draws each card, letting the
    // owning editor paint its domain content UNDER (before_card — e.g. an active-output ring) and OVER
    // (after_card — label, ports, preview well) the card via a CardDelegate. This is the one genuinely
    // repeated draw pattern; wires, the ghost, and any extra passes stay in each editor's draw() around
    // this call, where their content AND draw-order legitimately differ.
    void draw_cards(Renderer2D& r, const GraphModelAdapter& model, const CardDelegate& d) const {
        std::vector<AdapterNode> nodes; model.collect_nodes(nodes);
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            const AdapterNode& nd = nodes[i];
            d.before_card(r, nd, i);
            card(r, nd.rect, nd.accent, nd.selected, nd.broken);
            d.after_card(r, nd, i);
        }
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
