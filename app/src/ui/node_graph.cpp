#include "ui/node_graph.h"
#include <cmath>

namespace vivid::ui {

NodeGraph::NodeGraph() {
    sx_ = 940.f; sy_ = 482.f; sw_ = 160.f; sh_ = 72.f;
    // Start with the master output level wired in (out-of-box reactivity).
    data_.push_back({ 540.f, 470.f, 168.f, 72.f, "Output \xC2\xB7 Level", 0, 0.f, 0 });
    connected_ = 0;
}

void NodeGraph::set_value(int char_id, float v) {
    for (auto& n : data_) if (n.char_id == char_id) n.value = v;
}
float NodeGraph::shader_reactive() const {
    return (connected_ >= 0 && connected_ < int(data_.size())) ? data_[connected_].value : 0.f;
}
void NodeGraph::add_data_node(const std::string& title, int char_id) {
    float y = 470.f + data_.size() * 84.f;
    data_.push_back({ 540.f, y, 168.f, 72.f, title, char_id, 0.f, 90 });
}

void NodeGraph::data_out(const DataNode& n, float& px, float& py) { px = n.x + n.w; py = n.y + n.h * 0.5f; }
void NodeGraph::shader_in(float& px, float& py) const { px = sx_; py = sy_ + sh_ * 0.5f; }
bool NodeGraph::in_rect(float rx, float ry, float rw, float rh, double x, double y) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void draw_wire(Renderer2D& r, float x0, float y0, float x1, float y1,
                      float cr, float cg, float cb) {
    const int N = 26; float xs[N], ys[N];
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
    r.draw_text(512.f, 452.f,
                "NETWORK — right-click the track to send a characteristic here; drag its port to the shader",
                0.45f, 0.48f, 0.53f, 1.0f, 0.9f);

    // Shader node
    r.draw_rounded_rect(sx_, sy_, sw_, sh_, 6.f, 0.14f, 0.15f, 0.18f, 1.0f);
    r.draw_rect(sx_, sy_, sw_, 3.f, 0.35f, 0.55f, 0.95f, 1.0f);
    r.draw_text(sx_ + 12.f, sy_ + 12.f, "Plasma", 0.90f, 0.92f, 0.95f, 1.0f);
    r.draw_text(sx_ + 12.f, sy_ + 32.f, "shader op", 0.5f, 0.53f, 0.58f, 1.0f, 0.85f);
    r.draw_text(sx_ + 12.f, sy_ + 52.f, "reactive", 0.55f, 0.6f, 0.68f, 1.0f, 0.85f);
    { float px, py; shader_in(px, py); r.draw_rect(px - 6.f, py - 6.f, 12.f, 12.f, 0.35f, 0.55f, 0.95f, 1.0f); }

    // Data nodes
    for (auto& n : data_) {
        if (n.flash > 0) { r.draw_rect(n.x - 3.f, n.y - 3.f, n.w + 6.f, n.h + 6.f, 0.31f, 0.80f, 0.75f, 1.0f); n.flash--; }
        r.draw_rounded_rect(n.x, n.y, n.w, n.h, 6.f, 0.14f, 0.15f, 0.18f, 1.0f);
        r.draw_rect(n.x, n.y, n.w, 3.f, 0.31f, 0.80f, 0.75f, 1.0f);
        r.draw_text(n.x + 12.f, n.y + 12.f, n.title.c_str(), 0.90f, 0.92f, 0.95f, 1.0f);
        r.draw_text(n.x + 12.f, n.y + 32.f, "data source", 0.5f, 0.53f, 0.58f, 1.0f, 0.85f);
        r.draw_rect(n.x + 12.f, n.y + 52.f, (n.w - 40.f) * (n.value > 1 ? 1 : n.value), 7.f, 0.31f, 0.80f, 0.75f, 1.0f);
        float px, py; data_out(n, px, py);
        r.draw_rect(px - 6.f, py - 6.f, 12.f, 12.f, 0.31f, 0.80f, 0.75f, 1.0f);
    }

    if (connected_ >= 0 && connected_ < int(data_.size())) {
        float ox, oy, ix, iy; data_out(data_[connected_], ox, oy); shader_in(ix, iy);
        draw_wire(r, ox, oy, ix, iy, 0.45f, 0.78f, 0.85f);
    }
    if (wire_from_ >= 0 && wire_from_ < int(data_.size())) {
        float ox, oy; data_out(data_[wire_from_], ox, oy);
        draw_wire(r, ox, oy, float(cx_), float(cy_), 0.55f, 0.85f, 0.80f);
    }
}

bool NodeGraph::on_down(double x, double y) {
    cx_ = x; cy_ = y;
    float px, py;
    shader_in(px, py);
    if (std::hypot(x - px, y - py) < 14.0) { connected_ = -1; return true; }  // disconnect
    for (int i = 0; i < int(data_.size()); ++i) {
        data_out(data_[i], px, py);
        if (std::hypot(x - px, y - py) < 14.0) { drag_mode_ = 3; wire_from_ = i; return true; }
    }
    if (in_rect(sx_, sy_, sw_, sh_, x, y)) { drag_mode_ = 2; dx_ = x - sx_; dy_ = y - sy_; return true; }
    for (int i = 0; i < int(data_.size()); ++i) {
        if (in_rect(data_[i].x, data_[i].y, data_[i].w, data_[i].h, x, y)) {
            drag_mode_ = 1; drag_idx_ = i; dx_ = x - data_[i].x; dy_ = y - data_[i].y; return true;
        }
    }
    return false;
}

void NodeGraph::on_move(double x, double y) {
    cx_ = x; cy_ = y;
    if (drag_mode_ == 1 && drag_idx_ >= 0 && drag_idx_ < int(data_.size())) {
        data_[drag_idx_].x = float(x - dx_); data_[drag_idx_].y = float(y - dy_);
    } else if (drag_mode_ == 2) {
        sx_ = float(x - dx_); sy_ = float(y - dy_);
    }
}

void NodeGraph::on_up(double x, double y) {
    if (drag_mode_ == 3 && wire_from_ >= 0) {
        float px, py; shader_in(px, py);
        if (std::hypot(x - px, y - py) < 16.0) connected_ = wire_from_;
    }
    drag_mode_ = 0; drag_idx_ = -1; wire_from_ = -1;
}

}  // namespace vivid::ui
