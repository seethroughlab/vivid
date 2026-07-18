#pragma once
#include "ui/renderer_2d.h"
#include "gpu/shader_uniforms.h"
#include "gpu/visual_graph.h"
#include "mapping.h"
#include "ui/param_widget.h"   // NodeWidget + node_widget_kind (dock draw / input agree)
#include "ui/node_canvas.h"    // NodeView — the shared pan/zoom transform (ADR-0023)
#include "ui/graph_canvas.h"   // GraphCanvas — the shared graph-area draw skeleton (ADR-0023 Layer 2)
#include "ui/graph_adapter.h"  // GraphModelAdapter — the shared node-enumeration contract (ADR-0023 Layer 1)
#include "ui/chooser.h"        // the shared Tab palette (also used by the audio graph)
#include "gpu/shader_library.h"  // ADR-0016: badge a shader row SHADER, not OP
#include <vector>
#include <string>
#include <set>
#include <utility>

namespace vivid { class EditGateway; }

namespace vivid::ui {


// A minimal node editor on Renderer2D. Left: audio data-source nodes (each a live
// characteristic). Right: the rewireable visuals chain — op-nodes (Plasma/Video/
// Feedback/Blur) with texture input (left) and output (right) ports that you wire
// output->input, terminating in an Output node that drives the viewer. Data nodes
// wire into the ops' parameter ports (the audio->visual bridge). The chain itself
// lives in VisualGraph; this class owns the layout + interaction.
class NodeGraph : public GraphModelAdapter {
public:
    NodeGraph();

    // ADR-0023 Layer 1: the shared node-enumeration contract (op nodes only; the bridge data-nodes
    // are a visuals-domain overlay). Consumed by draw()'s own op-card loop; the audio peer implements
    // the same interface, so a shared draw loop can enumerate either (ADR-0023 #3).
    void collect_nodes(std::vector<AdapterNode>& out) const override;
    int  selected_node_id() const override;

    void set_value(int char_id, float v);
    void apply_params();   // resolve each node's params from the registry; publish viz.* sources
    void add_data_node(const std::string& title, int char_id);

    void set_bounds(float x0, float y0, float x1, float y1);
    void set_frame(float x0, float y0, float x1, float y1);   // full visuals-column rect (grid + clip)
    void set_visual_graph(vivid::VisualGraph* vg);   // also seeds the default mapping

    // Persistence + inspection.
    int  node_count() const { return static_cast<int>(data_.size()); }
    void get_node(int i, float& x, float& y, int& char_id, std::string& title) const;
    void get_shader(float& x, float& y) const { x = sx_; y = sy_; }
    void reset_nodes();
    void add_node_raw(const std::string& title, int char_id, float x, float y);
    void set_shader(float x, float y) { sx_ = x; sy_ = y; }
    const std::vector<vivid::Mapping>& mappings() const { return reg_.mappings(); }
    void add_mapping(const std::string& src, const std::string& dst, float amt,
                     float curve = 0.f, bool invert = false, float lo = 0.f, float hi = 1.f) {
        reg_.connect(src, dst, amt);
        if (auto* m = reg_.find(dst)) { m->curve = curve; m->invert = invert; m->out_lo = lo; m->out_hi = hi; }
    }
    // A track (stable id) was deleted: drop the mappings sourced from it.
    int drop_track_sources(int id) { return reg_.drop_track_sources(id); }
    // Mapping shaping edits (from the M overview).
    void set_mapping_amount(const std::string& dst, float a) { if (auto* m = reg_.find(dst)) m->amount = a; }
    void set_mapping_curve(const std::string& dst, float c)  { if (auto* m = reg_.find(dst)) m->curve = c; }
    void toggle_mapping_invert(const std::string& dst)       { if (auto* m = reg_.find(dst)) m->invert = !m->invert; }
    void set_mapping_lo(const std::string& dst, float v)     { if (auto* m = reg_.find(dst)) m->out_lo = v; }
    void set_mapping_hi(const std::string& dst, float v)     { if (auto* m = reg_.find(dst)) m->out_hi = v; }
    // Return path: registry can drive any dest; main feeds extra sources
    // (the visuals' uniform values) and applies audio-param dests each frame.
    float dest_value(const std::string& dest) const { return reg_.dest_value(dest); }
    const std::string* source_of(const std::string& dest) const { return reg_.source_of(dest); }
    void  set_named_source(const std::string& id, float v) { reg_.set_source(id, v); }
    void  disconnect_dest(const std::string& dest) { reg_.disconnect(dest); }
    // Chain (op type + input edge + id + position + base params) persistence.
    int  op_count() const;
    void get_op(int i, int& input, int& id, float& x, float& y) const;   // op TYPE: op_kind_name(i)
    std::string op_type_at(int i) const;   // the node's operator name (persist key)
    void get_op_base(int i, float out[4]) const;
    void chain_load_begin();
    void chain_load_add(const std::string& op_type, int id, float x, float y);
    void chain_load_set_input(int i, int input);
    void chain_load_set_input_b(int i, int input);   // second input (2-in ops)
    int  op_input_b_at(int i) const;                 // -1 if none
    std::vector<int> op_inputs_at(int i) const;      // all texture input edges (trailing -1 trimmed) — persist
    void set_op_input_at(int i, int port, int src);  // wire src -> node i's texture input `port`
    std::string op_asset_at(int i) const;                       // node's data asset (CustomShader .glsl), "" if none
    void        set_op_asset_at(int i, const std::string& asset);
    int         op_at(double sx, double sy) const;              // op node under a screen cursor, -1 if none
    std::string op_source_path(int i) const;                    // absolute editable source (CustomShader .glsl), "" if none
    bool        swap_op_type(int i, const std::string& type);   // re-instantiate node i as `type` (id/input/pos kept)

    // Visual-node selection + inspector: the bottom dock edits the selected node's
    // base param values (the resolved value = clamp(base + live modulation)).
    int  selected_op() const { return sel_op_; }
    void select_op(int i) { sel_op_ = i; }
    const char* op_kind_name(int i) const;               // "Plasma" / "Feedback" / ...
    int  op_param_count_at(int i) const;
    const char* op_param_label_at(int i, int local) const;
    float op_param_base_at(int i, int local) const;
    void  set_op_param_base_at(int i, int local, float v);
    float op_param_value_at(int i, int local) const;     // resolved (base + modulation)
    const char* op_file_param_at(int i, int local) const;         // FILE/TEXT param string ("" if none)
    void  set_op_file_param_at(int i, int local, const std::string& v);
    bool  op_param_wired_at(int i, int local) const;     // a data source drives it
    // Param metadata (from the operator descriptor) so the dock can pick a widget.
    int   op_param_type_at(int i, int local) const;         // VividParamType (FLOAT/INT/BOOL/...)
    int   op_param_hint_at(int i, int local) const;         // VividDisplayHint (DEFAULT/KNOB/COLOR/XY/...)
    float op_param_min_at(int i, int local) const;
    float op_param_max_at(int i, int local) const;
    int   op_param_choice_count_at(int i, int local) const; // >0 for enums
    const char* op_param_choice_label_at(int i, int local, int choice) const;

    // UI-4b: operator-exported custom editor. op_has_editor(i) is true when node i's op is a loaded
    // dylib that exports the editor ABI; op_draw_editor forwards the host-built context to it.
    bool op_has_editor(int i) const;
    VividEditorMetadata op_editor_metadata(int i) const;   // default/min size + title suffix
    void op_draw_editor(int i, VividEditorContext* ctx) const;

    void layout_nodes();                 // auto-arrange op nodes into a layered left->right layout
    void draw(Renderer2D& r);            // includes live node thumbnails (draw_texture)
    void draw_overlays(Renderer2D& r);   // chooser etc. — drawn after the node graph
    bool on_down(double x, double y);
    void on_move(double x, double y);
    void on_up(double x, double y);
    void zoom_at(double sx, double sy, float factor);   // scroll-wheel zoom around the cursor
    void get_view(float& ox, float& oy, float& scale) const { const NodeView& v = canvas_.view(); ox = v.ox; oy = v.oy; scale = v.scale; }
    void set_view(float ox, float oy, float scale) { canvas_.view() = { ox, oy, scale }; }

    // Operator chooser (Tab): a filtered palette that spawns a node at the cursor. The widget is
    // shared with the audio graph (ui/chooser.h); this class supplies the catalog + the spawn.
    bool chooser_open() const { return chooser_.open(); }
    // ADR-0016: so the chooser can badge a row SHADER (a file you can open and edit) rather
    // than OP (a compiled dylib). Optional — null just means every row reads as an op.
    void set_shader_library(const vivid::ShaderLibrary* lib) { shaders_ = lib; }
    // ADR-0018: op types quarantined this launch — shown greyed in the Tab chooser with a reason.
    void set_quarantined(std::set<std::string> q) { quarantined_ = std::move(q); }
    // ADR-0017: the undo command sink, so UI graph edits are captured (nullptr = no undo, e.g. tests).
    void set_edit_gateway(vivid::EditGateway* g) { edit_gateway_ = g; }
    void note_edit(const char* label, const char* key = "") { note_edit_(label, key); }   // for op-editor callbacks
    void chooser_show(double sx, double sy);  // open at the cursor
    void chooser_hide() { chooser_.hide(); }
    void chooser_move(int dir) { chooser_.move(dir); }
    void chooser_backspace()   { chooser_.backspace(); }
    void chooser_char(unsigned int c) { chooser_.type(c); }
    void chooser_confirm();                   // spawn the selected entry

    // ADR-0021/P3: create an op node at a screen position (as the chooser does) and, if given,
    // set the named FILE param to `file_value` (falls back to the node's first FILE param when
    // `file_param` is empty). Returns the new node index, or -1. Used by the file-drop handler.
    int drop_spawn(const std::string& op_type, double sx, double sy,
                   const std::string& file_param, const std::string& file_value);

private:
    static constexpr int kHistN = 64;   // data-node value history (rolling sparkline)
    struct DataNode { float x, y, w, h; std::string title; int char_id; float value; int flash;
                      float hist[kHistN]; int hist_head; };
    std::vector<DataNode> data_;
    vivid::MappingRegistry reg_;
    float sx_ = 900.f, sy_ = 488.f;   // persisted shader-node position (get_shader/set_shader)
    float bx0_ = 520.f, by0_ = 448.f, bx1_ = 1272.f, by1_ = 792.f;  // node-layout / hit-test bounds (inset)
    float fx0_ = 512.f, fy0_ = 440.f, fx1_ = 1280.f, fy1_ = 800.f;  // full visuals-column rect (clip + grid)
    bool  bounds_init_ = false;

    // Op-node layout (parallel to vg_->nodes()); the chain lives in VisualGraph.
    std::vector<std::pair<float, float>> op_pos_;
    bool op_pos_init_ = false;

    // 0 none, 1 data-node drag, 2 op-node drag, 3 data->param wire, 4 op->op wire, 5 pan
    int    drag_mode_ = 0;
    int    drag_idx_ = -1;     // dragged node (data for mode 1, op for mode 2)
    int    wire_from_ = -1;    // data node (mode 3) or op node (mode 4) the wire starts at
    double dx_ = 0, dy_ = 0, cx_ = 0, cy_ = 0;
    int    sel_op_ = -1;     // selected visual node (inspector target), -1 = none
    GraphCanvas canvas_;   // ADR-0023 Layer 2: the shared draw skeleton AND the owner of the pan/zoom camera (#1)
    float  pan_last_x_ = 0.f, pan_last_y_ = 0.f;     // last cursor during a canvas pan
    void to_world(double sx, double sy, double& wx, double& wy) const { canvas_.view().to_world(sx, sy, wx, wy); }

    Chooser chooser_;                  // the shared Tab palette (ui/chooser.h)
    void chooser_spawn(const Chooser::Entry& e);   // create the node the chooser handed back

    vivid::VisualGraph* vg_ = nullptr;
    const vivid::ShaderLibrary* shaders_ = nullptr;   // ADR-0016 (optional; chooser badge)
    std::set<std::string> quarantined_;               // ADR-0018 (disabled ops shown greyed)
    vivid::EditGateway* edit_gateway_ = nullptr;      // ADR-0017 (optional; UI edit capture)
    void note_edit_(const char* label, const char* key = "");   // fold a graph edit into the gesture

    static void data_out(const DataNode& n, float& px, float& py);
    int  find_source_node(const std::string& src) const;

    void sync_op_pos();
    CardPorts card_ports(int i) const;   // shared card port-row layout (ADR-0023)
    void op_node_rect(int i, float& x, float& y, float& w, float& h) const;
    bool op_in_port(int i, int port, float& px, float& py) const;  // texture input port `port`; false if out of range
    bool op_out_port(int i, float& px, float& py) const;  // false if op has no output
    void set_op_input_port(int node, int port, int src);  // wire src -> node's texture input `port` (-1 clears)
    int  first_node_of(const std::string& op_type) const; // -1 if none
    // Per-node param port: position of node_idx's local param row. False if out of range.
    bool param_port(int node_idx, int local, float& px, float& py) const;
    bool nearest_param(double x, double y, double maxd, int& node_idx, int& local) const;
    int  nearest_op_in(double x, double y, double maxd, int& port) const; // node index (-1 none) + which input port
    int  nearest_op_out(double x, double y, double maxd) const;// node index, -1
};

}  // namespace vivid::ui
