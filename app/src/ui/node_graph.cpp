#include "ui/node_graph.h"
#include <cmath>

namespace vivid::ui {

NodeGraph::NodeGraph() {
    data_   = { 560.f, 500.f, 156.f, 70.f, "Master RMS", "audio level", true };
    shader_ = { 920.f, 500.f, 156.f, 70.f, "Plasma",     "shader op",   false };
}

bool NodeGraph::in_rect(const Node& n, double x, double y) {
    return x >= n.x && x < n.x + n.w && y >= n.y && y < n.y + n.h;
}
void NodeGraph::out_port(const Node& n, float& px, float& py) { px = n.x + n.w; py = n.y + n.h * 0.5f; }
void NodeGraph::in_port(const Node& n, float& px, float& py)  { px = n.x;       py = n.y + n.h * 0.5f; }

static void draw_wire(Renderer2D& r, float x0, float y0, float x1, float y1,
                      float cr, float cg, float cb) {
    const int N = 26;
    float xs[N], ys[N];
    float dx = std::fabs(x1 - x0) * 0.5f; if (dx < 32.f) dx = 32.f;
    const float c1x = x0 + dx, c1y = y0, c2x = x1 - dx, c2y = y1;
    for (int i = 0; i < N; ++i) {
        const float t = i / float(N - 1), u = 1.f - t;
        xs[i] = u*u*u*x0 + 3*u*u*t*c1x + 3*u*t*t*c2x + t*t*t*x1;
        ys[i] = u*u*u*y0 + 3*u*u*t*c1y + 3*u*t*t*c2y + t*t*t*y1;
    }
    r.draw_polyline(xs, ys, N, 2.2f, cr, cg, cb, 1.0f);
}

void NodeGraph::draw(Renderer2D& r) {
    r.draw_text(512.f, 452.f, "NETWORK — drag the data port onto the shader input; click the input to disconnect",
                0.45f, 0.48f, 0.53f, 1.0f, 0.9f);

    auto node = [&](const Node& n) {
        r.draw_rounded_rect(n.x, n.y, n.w, n.h, 6.f, 0.14f, 0.15f, 0.18f, 1.0f);
        if (n.is_data) r.draw_rect(n.x, n.y, n.w, 3.f, 0.31f, 0.80f, 0.75f, 1.0f);  // teal accent
        else           r.draw_rect(n.x, n.y, n.w, 3.f, 0.35f, 0.55f, 0.95f, 1.0f);  // blue accent
        r.draw_text(n.x + 12.f, n.y + 12.f, n.title, 0.90f, 0.92f, 0.95f, 1.0f);
        r.draw_text(n.x + 12.f, n.y + 32.f, n.sub, 0.5f, 0.53f, 0.58f, 1.0f, 0.85f);
        float px, py;
        if (n.is_data) {
            // live value bar + output port
            r.draw_rect(n.x + 12.f, n.y + 52.f, (n.w - 40.f) * data_value_, 7.f, 0.31f, 0.80f, 0.75f, 1.0f);
            out_port(n, px, py);
            r.draw_rect(px - 6.f, py - 6.f, 12.f, 12.f, 0.31f, 0.80f, 0.75f, 1.0f);
        } else {
            in_port(n, px, py);
            r.draw_rect(px - 6.f, py - 6.f, 12.f, 12.f, 0.35f, 0.55f, 0.95f, 1.0f);
            r.draw_text(n.x + 12.f, n.y + 52.f, "reactive", 0.55f, 0.6f, 0.68f, 1.0f, 0.85f);
        }
    };
    node(data_);
    node(shader_);

    if (connected_) {
        float ox, oy, ix, iy; out_port(data_, ox, oy); in_port(shader_, ix, iy);
        draw_wire(r, ox, oy, ix, iy, 0.45f, 0.78f, 0.85f);
    }
    if (wiring_) {
        float ox, oy; out_port(data_, ox, oy);
        draw_wire(r, ox, oy, float(cur_x_), float(cur_y_), 0.55f, 0.85f, 0.80f);
    }
}

bool NodeGraph::on_down(double x, double y) {
    cur_x_ = x; cur_y_ = y;
    float px, py;
    out_port(data_, px, py);
    if (std::hypot(x - px, y - py) < 14.0) { drag_mode_ = 2; wiring_ = true; return true; }
    in_port(shader_, px, py);
    if (std::hypot(x - px, y - py) < 14.0) { connected_ = false; return true; }  // disconnect
    if (in_rect(data_, x, y))   { drag_mode_ = 1; drag_node_ = &data_;   dx_ = x - data_.x;   dy_ = y - data_.y;   return true; }
    if (in_rect(shader_, x, y)) { drag_mode_ = 1; drag_node_ = &shader_; dx_ = x - shader_.x; dy_ = y - shader_.y; return true; }
    return false;
}

void NodeGraph::on_move(double x, double y) {
    cur_x_ = x; cur_y_ = y;
    if (drag_mode_ == 1 && drag_node_) {
        drag_node_->x = float(x - dx_);
        drag_node_->y = float(y - dy_);
    }
}

void NodeGraph::on_up(double x, double y) {
    if (drag_mode_ == 2 && wiring_) {
        float px, py; in_port(shader_, px, py);
        if (std::hypot(x - px, y - py) < 16.0) connected_ = true;
    }
    drag_mode_ = 0; drag_node_ = nullptr; wiring_ = false;
}

}  // namespace vivid::ui
