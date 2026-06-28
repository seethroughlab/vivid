#pragma once
#include "ui/renderer_2d.h"

namespace vivid::ui {

// A minimal custom node editor on Renderer2D (we build our own rather than
// extract classic's 17K-LOC NodeGraphUI). For P7 it holds two fixed nodes — a
// "Master RMS" data source and the "Plasma" shader op — and one wire between
// them. The wire's presence drives whether the shader reacts: connected => the
// shader's reactive uniform = the data value; disconnected => 0.
class NodeGraph {
public:
    NodeGraph();

    void  set_data_value(float v) { data_value_ = v; }
    float shader_reactive() const { return connected_ ? data_value_ : 0.f; }
    bool  connected() const { return connected_; }

    void draw(Renderer2D& r);

    // Mouse in screen pixels. on_down returns true if it consumed the event
    // (so the caller doesn't also treat it as a session-grid click).
    bool on_down(double x, double y);
    void on_move(double x, double y);
    void on_up(double x, double y);

private:
    struct Node { float x, y, w, h; const char* title; const char* sub; bool is_data; };
    Node data_;
    Node shader_;
    bool  connected_ = true;     // start wired (matches the P6 default)
    float data_value_ = 0.f;

    int     drag_mode_ = 0;      // 0 none, 1 node, 2 wire
    Node*   drag_node_ = nullptr;
    double  dx_ = 0, dy_ = 0;    // grab offset
    double  cur_x_ = 0, cur_y_ = 0;
    bool    wiring_ = false;     // dragging a wire out of the data port

    static bool in_rect(const Node& n, double x, double y);
    static void out_port(const Node& n, float& px, float& py);  // data → right
    static void in_port(const Node& n, float& px, float& py);   // shader ← left
};

}  // namespace vivid::ui
