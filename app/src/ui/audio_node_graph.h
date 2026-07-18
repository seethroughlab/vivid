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
#include "ui/node_canvas.h"   // CardPorts — the shared card port-row layout (ADR-0023)
#include "ui/graph_canvas.h"  // GraphCanvas — the shared graph-area draw skeleton (ADR-0023 Layer 2)

#include <vector>

namespace vivid::session { struct Session; }
namespace vivid { struct App; struct Window; }   // ADR-0023 6d: the gesture methods take the shell context

namespace vivid::ui {

class NodeGraph;   // the visuals graph owns the MappingRegistry (the bridge); queried for mapped state

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

// The small map dot in a param cell's top-right corner: click it to open the bridge map-source
// picker for that node param. Shared by draw + input so the hit-rect matches the drawn dot.
inline Rect ag_param_map_dot(const AudioParamCell& c) { return { c.x + c.w - 13.f, c.y + 1.f, 10.f, 10.f }; }

// Curated inspector (Phase 2b): one pinned param drawn as a full-width vertical row —
// [ label | widget (slider/toggle/enum) | value | × remove ], plus a map dot. `widget` is a
// vivid::ui::NodeWidget. Shared by draw + input so the hit-rects match what's drawn.
struct AudioPinRow {
    int  index = 0;      // param index
    int  widget = 0;     // NodeWidget kind (slider/toggle/enum/knob)
    Rect row, label, widget_rect, value, remove, mapdot;
};

// A compound-widget preview (UI-4a): an ADSR envelope or LFO waveform drawn above the knob row.
// `index` is the group-leader param; `rect` is its slot in the top preview strip.
struct AudioCompoundPreview {
    int  hint = 0;
    int  index = -1;
    Rect rect;
};

class AudioNodeGraph {
public:
    void set_source(vivid::session::Session* s, int track) { s_ = s; track_ = track; }
    void set_bounds(float x0, float y0, float x1, float y1) {
        x0_ = x0; y0_ = y0; x1_ = x1; y1_ = y1;
        // Until a gesture or a load sets the camera, seed it to the panel origin so a fresh graph opens
        // in the dock at identity scale. (ADR-0023: the audio camera is now an ABSOLUTE NodeView like
        // the visual editor's — no more per-frame region-relative derivation.)
        if (!view_init_) { view_.ox = x0_; view_.oy = y0_; }
    }
    // The pan/zoom camera — an absolute world->screen NodeView, matching the visual editor (ADR-0023).
    // Persisted with the session (ox/oy/scale). set_view marks it user-set so set_bounds stops seeding;
    // the gesture methods (on_scroll/on_move/on_down) mutate `view_` directly + set `view_init_`.
    void set_view(const NodeView& v) { view_ = v; view_init_ = true; }
    const NodeView& view() const { return view_; }

    // The selected node (UI-4a): the param band grows to host a compound-widget preview (ADSR/LFO)
    // when the selection carries one, so draw + input must agree on the selection before sizing.
    void set_selection(int node_id) { sel_node_ = node_id; }
    // The bridge (MappingRegistry, owned by the visuals graph): draw lights a param cell's map dot
    // when that node param has a mapped source. Optional (null = don't light any dot).
    void set_mapping(const NodeGraph* g) { map_ = g; }
    Rect graph_region() const;   // the node-graph area (above the param strip) — for input zoom/pan

    // Deterministic node layout (nodes fitted to the graph sub-region). Shared by draw + input.
    std::vector<AudioNodeBox> layout() const;
    // The "+ FX" affordance rect (adds an effect to the end of the chain).
    Rect add_button_rect() const;
    // The "+ Src" affordance rect (adds a parallel instrument source — the key-split builder).
    Rect source_add_button_rect() const;
    // Key-range drag handles for a selected SOURCE node (the [lo,hi] the source voices). Both are
    // empty rects when the selected node is not a source. Shared by draw + input hit-test.
    bool sel_is_source(int sel_node) const;
    Rect key_lo_rect(int sel_node) const;
    Rect key_hi_rect(int sel_node) const;
    // The remove (x) rect for an effect node card (only meaningful for kind==1 boxes).
    Rect remove_rect(const AudioNodeBox& b) const;
    // Wire ports for drag-to-rewire: the output port (right edge; source of a new edge — absent on
    // the Output node) and the signal input port (top-left row; target of an edge — absent on
    // instruments/modulators, which have no signal input).
    Rect out_port_rect(const AudioNodeBox& b) const;
    Rect in_port_rect(const AudioNodeBox& b) const;
    // The params a node EXPOSES as ports (ADR-0022, mirroring the visuals graph): every param for a
    // native op, the pinned/curated subset for a plugin. Compound-widget leaders (the LFO waveform
    // enum) are skipped so the port list stays clean. Indices into the node's param list.
    std::vector<int> exposed_params(int node_id) const;
    // A param port down the card's left edge (below the signal-in port): `slot` indexes into
    // exposed_params(). A control wire from a modulator drops here. Empty rect if slot is invalid.
    Rect param_port_rect(const AudioNodeBox& b, int slot) const;
    // The "+" affordance under a PLUGIN node's ports (opens the searchable pin picker to expose one
    // more param). Empty rect for a native node (all params already exposed). Shared draw + input.
    Rect add_param_port_rect(const AudioNodeBox& b) const;
    // Hit-test the param ports of `b`: returns the exposed-list slot under (mx,my), or -1. Shared so
    // the drag-to-connect release and the draw agree on where a port is.
    int  param_port_hit(const AudioNodeBox& b, float mx, float my) const;
    // The card height for a node — a function of its exposed-param count (variable, like the visuals
    // graph), so layout() can size each box. A plugin's "+" row is included.
    float card_height(int node_id) const;
    // The inline param cells for the selected node (by node id; -1 = none); empty otherwise. The
    // LFO enum leader is claimed by its preview and omitted (no knob); ADSR channels stay as knobs.
    std::vector<AudioParamCell> param_cells(int sel_node) const;
    // Compound-widget previews (ADSR/LFO) for the selected node — shared by draw + input hit-test.
    std::vector<AudioCompoundPreview> compound_previews() const;
    // Curated inspector (Phase 2b): true when the selected node is a plugin (VST3) that uses the
    // vertical pinned-param inspector instead of the native knob strip.
    bool is_plugin_node(int sel_node) const;
    // The pinned params of the selected node as vertical rows; empty for a non-plugin node. Shared
    // by draw + input. `add_param_button_rect` is the "+ Add param" affordance below the rows.
    std::vector<AudioPinRow> pinned_rows(int sel_node) const;
    Rect add_param_button_rect(int sel_node) const;

    // Interaction (ADR-0023 6d): the audio editor owns its gesture FSM (mirroring the visual
    // NodeGraph's on_down/on_move/on_up). Because it does not own the session, the methods take the
    // shell context and route domain edits through the session C-API + the EditGateway, exactly as
    // the free functions they replace did. on_down returns true when it consumed the press.
    bool on_down(App& app, Window& win, double mx, double my);
    void on_move(App& app, Window& win, double mx, double my);
    bool on_up(App& app, Window& win, double mx, double my);   // true = a rewire was completed
    void on_scroll(App& app, Window& win, double yoff, double mx, double my);

    // Render: the graph + (if a node is selected) its highlight + inline param strip. When a rewire
    // drag is in progress (the wire_from member is set), draw a ghost wire from that node's output
    // port to the cursor (cx, cy).
    void draw(Renderer2D& r, int sel_node, float cx = 0.f, float cy = 0.f) const;

private:
    Rect  param_region() const;   // the selected-node param strip (bottom band)
    float param_band_h() const;   // band height — taller when the selection has a compound preview
    // The shared card port-row layout for a node (its exposed-param count + kind + plugin state).
    // One source of truth for card height, port centres, and the preview well (draw + hit-test).
    CardPorts card_ports(int node_id, int kind) const;

    vivid::session::Session* s_ = nullptr;
    const NodeGraph* map_ = nullptr;   // the bridge (for the mapped-state dot); not owned
    int   track_ = -1;
    int   sel_node_ = -1;
    float x0_ = 0, y0_ = 0, x1_ = 0, y1_ = 0;
    NodeView view_;              // absolute world->screen camera (ADR-0023; was region-relative zoom/pan)
    bool     view_init_ = false; // false until a gesture/load sets it; set_bounds seeds the panel origin meanwhile
    GraphCanvas canvas_;   // ADR-0023 Layer 2: the shared card/grid/ghost-wire draw skeleton

    // The in-flight gesture (ADR-0023 6c/6d): private state owned by the on_down/on_move/on_up FSM
    // above (nothing outside this class touches it). Exactly one gesture is live at a time.
    int    param_drag  = -1;                            // param index being dragged (-1 = none)
    float  param_v0    = 0.f; double param_y0 = 0.0;    // knob-strip vertical drag: value + grab-y at start
    bool   param_horiz = false; float param_rx = 0.f, param_rw = 1.f;   // slider-row horizontal drag: mx->[rx,rx+rw]
    int    key_drag    = -1;                            // source key-range handle: 0 lo / 1 hi / -1 none
    int    key_v0      = 0; double key_y0 = 0.0;        // key-range drag: value + grab-y at start
    int    wire_from   = -1;                            // rewire drag: source node id (-1 = none); read by draw
    int    node_drag   = -1; float node_dx = 0.f, node_dy = 0.f;   // node reposition: id + world-unit grab offset
    bool   panning     = false; double pan_mx0 = 0, pan_my0 = 0; float pan_ox0 = 0, pan_oy0 = 0;   // pan gesture
    double last_click_t = -1;                           // double-click-to-reset-view timer
    int    last_node   = -1; double last_node_t = -1;   // double-click a node -> open its plugin editor
};

}  // namespace vivid::ui
