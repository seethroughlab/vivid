#pragma once
#include "ui/renderer_2d.h"
#include <vector>
#include <string>

namespace vivid::ui {

// A minimal custom node editor on Renderer2D (we build our own rather than
// extract classic's 17K-LOC NodeGraphUI). Holds N data-source nodes (each
// carrying a live audio characteristic) + one "Plasma" shader op, and a single
// wire into the shader's reactive input. P8's bridge spawns data nodes via
// add_data_node(); the wire selects which characteristic drives the visuals.
class NodeGraph {
public:
    NodeGraph();

    void  set_value(int char_id, float v);          // feed a live characteristic value
    float shader_reactive() const;                  // wired node's value, else 0
    void  add_data_node(const std::string& title, int char_id);

    void draw(Renderer2D& r);

    // Mouse in screen px. on_down returns true if it consumed the event.
    bool on_down(double x, double y);
    void on_move(double x, double y);
    void on_up(double x, double y);

private:
    struct DataNode { float x, y, w, h; std::string title; int char_id; float value; int flash; };
    std::vector<DataNode> data_;
    float sx_, sy_, sw_, sh_;   // shader node rect
    int   connected_ = -1;      // index in data_ wired to the shader, or -1

    int    drag_mode_ = 0;      // 0 none, 1 data-node, 2 shader-node, 3 wire
    int    drag_idx_ = -1;
    int    wire_from_ = -1;     // data node a wire is being dragged from
    double dx_ = 0, dy_ = 0, cx_ = 0, cy_ = 0;

    static void data_out(const DataNode& n, float& px, float& py);
    void shader_in(float& px, float& py) const;
    static bool in_rect(float rx, float ry, float rw, float rh, double x, double y);
};

}  // namespace vivid::ui
