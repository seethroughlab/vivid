#pragma once
#include "ui/node_canvas.h"   // Rect
#include <string>
#include <vector>

// ADR-0023 Layer 1 — the `GraphModelAdapter`: the read surface the shared canvas enumerates a graph
// through, independent of which domain model backs it (the visuals `VisualGraph`/`NodeGraph`, or the
// per-track audio `session_track_audio_graph_*` API). Both editors already compute exactly this
// node-level shape for `canvas_.card()`; the adapter is the common contract, so the Layer-2 canvas (and
// the future shared draw loop, ADR-0023 #3) can iterate nodes without knowing the domain.
//
// SCOPE (deliberately node-level): this is the card-chrome data the two editors genuinely share —
// id, draw-space rect, accent, selection, health, title, error. What legitimately DIVERGES stays a
// per-editor domain overlay drawn outside the adapter: wires (visual port->port + param-wires vs audio
// kind-colored edges), ports (different geometry/semantics), the preview well contents (GPU thumbnail /
// waveform / sparkline), the visuals bridge data-nodes, and the audio param strip. WRITE commands
// (connect/disconnect/set-param/add/remove/move) wait for the Layer-3 controller — they'd have no caller
// yet, and each editor already owns its own gesture FSM (ADR-0023 step 6).
//
// COORDINATE NOTE: `rect` is in WORLD coordinates for BOTH editors — each draws its graph content through
// the canvas `NodeView` transform ("true zoom", ADR-0023 #3). (Historically the audio graph baked screen
// coords in `layout()`; #3 moved it onto the shared world-space transform.)
namespace vivid::ui {

struct AdapterNode {
    int         id       = -1;      // stable node id (visuals `VisualNode.id` / audio graph node id)
    Rect        rect{};             // card rect in the owning editor's draw space (see COORDINATE NOTE)
    float       accent[3] = { 0.5f, 0.5f, 0.5f };  // card accent color (by value — sources are per-node locals)
    bool        selected = false;   // the inspector selection (drives the blue ring in card())
    bool        broken   = false;   // ADR-0019 error state (drives the red border + "!" badge in card())
    std::string title;              // header label
    std::string error;              // first error line (empty = healthy); the editor draws it over the preview
};

// The read-mostly view a shared canvas/draw loop enumerates a graph through. Each editor implements it
// over its own model AND consumes its own result in its card loop (so the contract has real consumers,
// not just a future one). Snapshot semantics: `collect_nodes` returns the drawable operator nodes for
// the current frame, in draw/index order (visuals op nodes; audio laid-out boxes).
class GraphModelAdapter {
public:
    virtual ~GraphModelAdapter() = default;
    virtual void collect_nodes(std::vector<AdapterNode>& out) const = 0;
    virtual int  selected_node_id() const = 0;   // stable id of the selected node, or -1
};

}  // namespace vivid::ui
