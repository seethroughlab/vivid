#pragma once
#include "ui/renderer_2d.h"
#include "gpu/shader_uniforms.h"
#include "gpu/visual_graph.h"
#include "mapping.h"
#include <vector>
#include <string>
#include <utility>

namespace vivid::ui {

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
    void fill_uniforms(float* out) const;            // kNumShaderUniforms param values
    void add_data_node(const std::string& title, int char_id);

    void set_bounds(float x0, float y0, float x1, float y1);
    void set_visual_graph(vivid::VisualGraph* vg) { vg_ = vg; }

    // Persistence + inspection.
    int  node_count() const { return static_cast<int>(data_.size()); }
    void get_node(int i, float& x, float& y, int& char_id, std::string& title) const;
    void get_shader(float& x, float& y) const { x = sx_; y = sy_; }
    void reset_nodes();
    void add_node_raw(const std::string& title, int char_id, float x, float y);
    void set_shader(float x, float y) { sx_ = x; sy_ = y; }
    const std::vector<vivid::Mapping>& mappings() const { return reg_.mappings(); }
    void add_mapping(const std::string& src, const std::string& dst, float amt) { reg_.connect(src, dst, amt); }
    // Return path (P27): registry can drive any dest; main feeds extra sources
    // (the visuals' uniform values) and applies audio-param dests each frame.
    float dest_value(const std::string& dest) const { return reg_.dest_value(dest); }
    const std::string* source_of(const std::string& dest) const { return reg_.source_of(dest); }
    void  set_named_source(const std::string& id, float v) { reg_.set_source(id, v); }
    void  disconnect_dest(const std::string& dest) { reg_.disconnect(dest); }
    // Chain (op type + input edge + position) persistence.
    int  op_count() const;
    void get_op(int i, int& op, int& input, float& x, float& y) const;
    void chain_load_begin();
    void chain_load_add(int op, float x, float y);
    void chain_load_set_input(int i, int input);

    void draw(Renderer2D& r);
    // Live node thumbnails: op_thumb_rect(i) gives the on-screen strip for op
    // node i (false for Output / off-pane); main blits each node's output there
    // after the UI pass.
    int  op_thumb_count() const;
    bool op_thumb_rect(int i, float& x, float& y, float& w, float& h) const;
    bool on_down(double x, double y);
    void on_move(double x, double y);
    void on_up(double x, double y);

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
    float  grid_off_x_ = 0.f, grid_off_y_ = 0.f;     // grid scroll (follows panning)
    float  pan_last_x_ = 0.f, pan_last_y_ = 0.f;     // last cursor during a canvas pan

    // Tab chooser state.
    bool        chooser_open_ = false;
    std::string chooser_filter_;
    int         chooser_sel_ = 0;                     // index into chooser_hits_
    float       chooser_sx_ = 0.f, chooser_sy_ = 0.f; // cursor at open (spawn anchor)
    std::vector<int> chooser_hits_;                   // catalog indices matching the filter
    void chooser_rebuild();
    void draw_chooser(Renderer2D& r);

    vivid::VisualGraph* vg_ = nullptr;

    static void data_out(const DataNode& n, float& px, float& py);
    static bool in_rect(float rx, float ry, float rw, float rh, double x, double y);
    static int  op_param_uniforms(vivid::VOp op, int out[4]);  // uniforms an op owns; count
    int  find_source_node(const std::string& src) const;

    void sync_op_pos();
    void op_node_rect(int i, float& x, float& y, float& w, float& h) const;
    bool op_in_port(int i, float& px, float& py) const;   // false if op has no input
    bool op_out_port(int i, float& px, float& py) const;  // false if op has no output
    int  first_node_of(vivid::VOp op) const;              // -1 if none
    bool param_port(int uniform, float& px, float& py) const;  // false if unowned
    int  nearest_param(double x, double y, double maxd) const; // uniform index, -1
    int  nearest_op_in(double x, double y, double maxd) const; // node index, -1
    int  nearest_op_out(double x, double y, double maxd) const;// node index, -1
    void draw_op_palette(Renderer2D& r);
    int  palette_hit(double x, double y) const;           // VOp to add, or -1
};

}  // namespace vivid::ui
