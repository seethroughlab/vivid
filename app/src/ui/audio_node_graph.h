#pragma once
// UI-3: a per-track AUDIO NODE GRAPH view + light interaction. Reads the authoritative topology
// the RT engine runs (the session_track_audio_graph_* introspection API) and draws it as a node
// graph — the audio peer of the visuals node graph, hosted as a detail-region deep view
// (ADR-0013). Stage 1 interaction: click a node to select it, edit its params inline, add an
// effect (+), remove an effect (x). Auto-lays-out left->right by depth and auto-fits the region.
// The layout is a deterministic function of (session, track, bounds), so the draw path and the
// input path share it for hit-testing (this component holds no per-frame state).
//
// Free rewiring (drag ports to build arbitrary topologies) is Stage 2 — it needs the audio-graph
// edit C-API + the graph as the editable source of truth (AG-1 step 2).
#include "ui/renderer_2d.h"
#include "ui/layout.h"        // vivid::ui::Rect

#include <vector>

namespace vivid::session { struct Session; }

namespace vivid::ui {

// A laid-out node. `node_id` is the stable graph node id — it addresses the node for params,
// removal, and rewiring (the chain-index model can't address nodes in a non-linear graph).
struct AudioNodeBox {
    int   kind = 0;         // 0 instrument / 1 effect / 2 output
    int   node_id = -1;     // stable graph node id
    float x = 0, y = 0, w = 0, h = 0;
};

// One param cell in the selected node's inline editor strip.
struct AudioParamCell {
    int   index = 0;
    float x = 0, y = 0, w = 0, h = 0;
    float knob_cx = 0, knob_cy = 0, knob_r = 0;
};

class AudioNodeGraph {
public:
    void set_source(vivid::session::Session* s, int track) { s_ = s; track_ = track; }
    void set_bounds(float x0, float y0, float x1, float y1) { x0_ = x0; y0_ = y0; x1_ = x1; y1_ = y1; }
    // View transform applied on top of the auto-fit (2i): zoom around the region origin + pan.
    void set_view(float zoom, float pan_x, float pan_y) { zoom_ = zoom; pan_x_ = pan_x; pan_y_ = pan_y; }
    Rect graph_region() const;   // the node-graph area (above the param strip) — for input zoom/pan

    // Deterministic node layout (nodes fitted to the graph sub-region). Shared by draw + input.
    std::vector<AudioNodeBox> layout() const;
    // The "+ FX" affordance rect (adds an effect to the end of the chain).
    Rect add_button_rect() const;
    // The remove (x) rect for an effect node card (only meaningful for kind==1 boxes).
    Rect remove_rect(const AudioNodeBox& b) const;
    // Wire ports for drag-to-rewire: the output port (right edge; source of a new edge — absent on
    // the Output node) and the input port (left edge; target of an edge — absent on instruments).
    Rect out_port_rect(const AudioNodeBox& b) const;
    Rect in_port_rect(const AudioNodeBox& b) const;
    // The inline param cells for the selected node (by node id; -1 = none); empty otherwise.
    std::vector<AudioParamCell> param_cells(int sel_node) const;

    // Render: the graph + (if a node is selected) its highlight + inline param strip. When
    // wire_from >= 0 a rewire drag is in progress: draw a ghost wire from that node's output port
    // to the cursor (cx, cy).
    void draw(Renderer2D& r, int sel_node, int wire_from = -1, float cx = 0.f, float cy = 0.f) const;

private:
    Rect param_region() const;   // the selected-node param strip (bottom band)

    vivid::session::Session* s_ = nullptr;
    int   track_ = -1;
    float x0_ = 0, y0_ = 0, x1_ = 0, y1_ = 0;
    float zoom_ = 1.f, pan_x_ = 0.f, pan_y_ = 0.f;   // 2i view transform (applied in layout())
};

}  // namespace vivid::ui
