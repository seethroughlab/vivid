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

// A laid-out node. `chain` addresses the underlying op for params/removal: -1 = instrument,
// 0+ = effect index, -2 = output (no params, not removable).
struct AudioNodeBox {
    int   kind = 0;      // 0 instrument / 1 effect / 2 output
    int   chain = -2;
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

    // Deterministic node layout (nodes fitted to the graph sub-region). Shared by draw + input.
    std::vector<AudioNodeBox> layout() const;
    // The "+ FX" affordance rect (adds an effect to the end of the chain).
    Rect add_button_rect() const;
    // The remove (x) rect for an effect node card (only meaningful for kind==1 boxes).
    Rect remove_rect(const AudioNodeBox& b) const;
    // The inline param cells for the selected op (chain: -1 instrument / 0+ effect); empty otherwise.
    std::vector<AudioParamCell> param_cells(int sel_chain) const;

    // Render: the graph + (if a node is selected) its highlight + inline param strip.
    void draw(Renderer2D& r, int sel_chain) const;

private:
    Rect graph_region() const;   // the node-graph area (above the param strip)
    Rect param_region() const;   // the selected-node param strip (bottom band)

    vivid::session::Session* s_ = nullptr;
    int   track_ = -1;
    float x0_ = 0, y0_ = 0, x1_ = 0, y1_ = 0;
};

}  // namespace vivid::ui
