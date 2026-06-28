#pragma once
#include "ui/renderer_2d.h"
#include "gpu/shader_uniforms.h"
#include "mapping.h"
#include <vector>
#include <string>

namespace vivid { class VisualGraph; }

namespace vivid::ui {

// A minimal custom node editor on Renderer2D (we build our own rather than
// extract classic's 17K-LOC NodeGraphUI). Holds N data-source nodes (each
// carrying a live audio characteristic) + one "Plasma" shader op exposing
// kNumShaderUniforms named input ports. P8's bridge spawns data nodes via
// add_data_node(); P11 lets any characteristic wire to any uniform — one wire
// per port — so the graph maps audio → named shader parameters.
class NodeGraph {
public:
    NodeGraph();

    void set_value(int char_id, float v);            // feed a live characteristic value
    void fill_uniforms(float* out) const;            // kNumShaderUniforms values (0 if unwired)
    void add_data_node(const std::string& title, int char_id);

    // Confine the graph to the visuals pane: shifts nodes when the pane's left
    // edge moves (splitter) and clamps them inside. Drawing is clipped to it.
    void set_bounds(float x0, float y0, float x1, float y1);

    // The visuals chain shown as op-nodes (generator/feedback/blur/output); the
    // generator node is clickable to switch Plasma<->Video.
    void set_visual_graph(vivid::VisualGraph* vg) { vg_ = vg; }

    // Persistence + inspection accessors.
    int  node_count() const { return static_cast<int>(data_.size()); }
    void get_node(int i, float& x, float& y, int& char_id, std::string& title) const;
    void get_shader(float& x, float& y) const { x = sx_; y = sy_; }
    void reset_nodes();                       // clear all data nodes + mappings
    void add_node_raw(const std::string& title, int char_id, float x, float y);
    void set_shader(float x, float y) { sx_ = x; sy_ = y; }
    const std::vector<vivid::Mapping>& mappings() const { return reg_.mappings(); }
    void add_mapping(const std::string& src, const std::string& dst, float amt) { reg_.connect(src, dst, amt); }

    void draw(Renderer2D& r);

    // Mouse in screen px. on_down returns true if it consumed the event.
    bool on_down(double x, double y);
    void on_move(double x, double y);
    void on_up(double x, double y);

private:
    struct DataNode { float x, y, w, h; std::string title; int char_id; float value; int flash; };
    std::vector<DataNode> data_;
    float sx_, sy_, sw_, sh_;                    // shader node rect
    vivid::MappingRegistry reg_;                 // source_id -> dest_id mappings + source values
    float bx0_ = 520.f, by0_ = 448.f, bx1_ = 1272.f, by1_ = 792.f;  // visuals-pane bounds
    bool  bounds_init_ = false;

    int    drag_mode_ = 0;      // 0 none, 1 data-node, 2 shader-node, 3 wire
    int    drag_idx_ = -1;
    int    wire_from_ = -1;     // data node a wire is being dragged from
    double dx_ = 0, dy_ = 0, cx_ = 0, cy_ = 0;

    static void data_out(const DataNode& n, float& px, float& py);
    void  shader_in(int port, float& px, float& py) const;   // input port `port` position
    int   nearest_shader_in(double x, double y, double max_dist) const;  // -1 if none
    int   find_source_node(const std::string& src) const;
    void  op_box(int op, float& x, float& y, float& w, float& h) const;  // 0=gen 1=fb 2=blur 3=out
    static bool in_rect(float rx, float ry, float rw, float rh, double x, double y);

    vivid::VisualGraph* vg_ = nullptr;
};

}  // namespace vivid::ui
