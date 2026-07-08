#pragma once
#include "ui/renderer_2d.h"
#include "gpu/shader_uniforms.h"
#include "gpu/visual_graph.h"
#include "mapping.h"
#include "ui/param_widget.h"   // NodeWidget + node_widget_kind (dock draw / input agree)
#include <vector>
#include <string>
#include <utility>

namespace vivid::ui {

// One Tab-chooser row: a spawnable visuals op (op_type from the registry) or an
// audio data-source (char_id). env: 0 = gpu op, 1 = audio source.
struct ChooserEntry { std::string label; bool is_op; std::string op_type; int char_id; int env; };

// A minimal node editor on Renderer2D. Left: audio data-source nodes (each a live
// characteristic). Right: the rewireable visuals chain — op-nodes (Plasma/Video/
// Feedback/Blur) with texture input (left) and output (right) ports that you wire
// output->input, terminating in an Output node that drives the viewer. Data nodes
// wire into the ops' parameter ports (the audio->visual bridge). The chain itself
// lives in VisualGraph; this class owns the layout + interaction.
class NodeGraph {
public:
    NodeGraph();

    void set_value(int char_id, float v);
    void apply_params();   // resolve each node's params from the registry; publish viz.* sources
    void add_data_node(const std::string& title, int char_id);

    void set_bounds(float x0, float y0, float x1, float y1);
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
    void get_op(int i, int& op, int& input, int& id, float& x, float& y) const;
    std::string op_type_at(int i) const;   // the node's operator name (persist key)
    void get_op_base(int i, float out[4]) const;
    void chain_load_begin();
    void chain_load_add(const std::string& op_type, int id, float x, float y);
    void chain_load_set_input(int i, int input);
    std::string op_asset_at(int i) const;                       // node's data asset (CustomShader .glsl), "" if none
    void        set_op_asset_at(int i, const std::string& asset);
    int         op_at(double sx, double sy) const;              // op node under a screen cursor, -1 if none
    std::string op_source_path(int i) const;                    // absolute editable source (CustomShader .glsl), "" if none
    bool        swap_op_type(int i, const std::string& type);   // re-instantiate node i as `type` (id/input/pos kept)

    // Visual-node selection + inspector: the bottom dock edits the selected node's
    // base param values (the resolved value = clamp(base + live modulation)).
    int  selected_op() const { return sel_op_; }
    void select_op(int i) { sel_op_ = i; }
    int  op_kind(int i) const;                            // VOp as int, -1 if invalid
    const char* op_kind_name(int i) const;               // "Plasma" / "Feedback" / ...
    int  op_param_count_at(int i) const;
    const char* op_param_label_at(int i, int local) const;
    float op_param_base_at(int i, int local) const;
    void  set_op_param_base_at(int i, int local, float v);
    float op_param_value_at(int i, int local) const;     // resolved (base + modulation)
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
    void get_view(float& ox, float& oy, float& scale) const { ox = view_ox_; oy = view_oy_; scale = view_scale_; }
    void set_view(float ox, float oy, float scale) { view_ox_ = ox; view_oy_ = oy; view_scale_ = scale; }

    // Operator chooser (Tab): a filtered palette that spawns a node at the cursor.
    bool chooser_open() const { return chooser_open_; }
    void chooser_show(double sx, double sy);  // open at the cursor
    void chooser_hide() { chooser_open_ = false; }
    void chooser_move(int dir);               // move selection (+1 down / -1 up)
    void chooser_backspace();
    void chooser_char(unsigned int c);        // typed filter character
    void chooser_confirm();                   // spawn the selected entry

private:
    static constexpr int kHistN = 64;   // data-node value history (rolling sparkline)
    struct DataNode { float x, y, w, h; std::string title; int char_id; float value; int flash;
                      float hist[kHistN]; int hist_head; };
    std::vector<DataNode> data_;
    vivid::MappingRegistry reg_;
    float sx_ = 900.f, sy_ = 488.f, sw_ = 0.f, sh_ = 0.f;   // vestigial (persistence)
    float bx0_ = 520.f, by0_ = 448.f, bx1_ = 1272.f, by1_ = 792.f;
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
    float  view_ox_ = 0.f, view_oy_ = 0.f, view_scale_ = 1.f;  // world->screen pan/zoom
    float  pan_last_x_ = 0.f, pan_last_y_ = 0.f;     // last cursor during a canvas pan
    void to_world(double sx, double sy, double& wx, double& wy) const {
        wx = (sx - view_ox_) / view_scale_; wy = (sy - view_oy_) / view_scale_;
    }

    // Tab chooser state.
    bool        chooser_open_ = false;
    std::string chooser_filter_;
    int         chooser_sel_ = 0;                     // index into chooser_hits_
    float       chooser_sx_ = 0.f, chooser_sy_ = 0.f; // cursor at open (spawn anchor)
    std::vector<ChooserEntry> chooser_catalog_;       // registry ops + audio sources (built on open)
    std::vector<int> chooser_hits_;                   // catalog indices matching the filter
    void chooser_rebuild();
    void chooser_build_catalog();
    void draw_chooser(Renderer2D& r);

    vivid::VisualGraph* vg_ = nullptr;

    static void data_out(const DataNode& n, float& px, float& py);
    static bool in_rect(float rx, float ry, float rw, float rh, double x, double y);
    int  find_source_node(const std::string& src) const;

    void sync_op_pos();
    void op_node_rect(int i, float& x, float& y, float& w, float& h) const;
    bool op_in_port(int i, float& px, float& py) const;   // false if op has no input
    bool op_out_port(int i, float& px, float& py) const;  // false if op has no output
    int  first_node_of(vivid::VOp op) const;              // -1 if none
    // Per-node param port: position of node_idx's local param row. False if out of range.
    bool param_port(int node_idx, int local, float& px, float& py) const;
    bool nearest_param(double x, double y, double maxd, int& node_idx, int& local) const;
    int  nearest_op_in(double x, double y, double maxd) const; // node index, -1
    int  nearest_op_out(double x, double y, double maxd) const;// node index, -1
    void draw_op_palette(Renderer2D& r);
    int  palette_hit(double x, double y) const;           // VOp to add, or -1
    bool relayout_hit(double x, double y) const;          // the "Re-layout" button
};

}  // namespace vivid::ui
