#include "ui/node_graph.h"
#include <cmath>

namespace vivid::ui {

NodeGraph::NodeGraph() {
    sx_ = 936.f; sy_ = 430.f; sw_ = 172.f; sh_ = 44.f + kNumShaderUniforms * 24.f;
    for (int i = 0; i < kNumShaderUniforms; ++i) connected_[i] = -1;
    // Start with the master output level wired to "glow" (out-of-box reactivity).
    data_.push_back({ 540.f, 470.f, 168.f, 72.f, "Output \xC2\xB7 Level", 0, 0.f, 0 });
    connected_[3] = 0;  // glow <- level
}

void NodeGraph::set_value(int char_id, float v) {
    for (auto& n : data_) if (n.char_id == char_id) n.value = v;
}
void NodeGraph::fill_uniforms(float* out) const {
    for (int i = 0; i < kNumShaderUniforms; ++i)
        out[i] = (connected_[i] >= 0 && connected_[i] < int(data_.size())) ? data_[connected_[i]].value : 0.f;
}
void NodeGraph::add_data_node(const std::string& title, int char_id) {
    float y = 470.f + data_.size() * 84.f;
    data_.push_back({ 540.f, y, 168.f, 72.f, title, char_id, 0.f, 90 });
}

void NodeGraph::data_out(const DataNode& n, float& px, float& py) { px = n.x + n.w; py = n.y + n.h * 0.5f; }
void NodeGraph::shader_in(int port, float& px, float& py) const { px = sx_; py = sy_ + 36.f + port * 24.f; }
int NodeGraph::nearest_shader_in(double x, double y, double max_dist) const {
    int best = -1; double bd = max_dist;
    for (int i = 0; i < kNumShaderUniforms; ++i) {
        float px, py; shader_in(i, px, py);
        double d = std::hypot(x - px, y - py);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
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
                "NETWORK — drag a data node's port onto a shader uniform (warp / hue / density / glow)",
                0.45f, 0.48f, 0.53f, 1.0f, 0.9f);

    // Wires first (under the nodes)
    for (int i = 0; i < kNumShaderUniforms; ++i) {
        if (connected_[i] < 0 || connected_[i] >= int(data_.size())) continue;
        float ox, oy, ix, iy; data_out(data_[connected_[i]], ox, oy); shader_in(i, ix, iy);
        draw_wire(r, ox, oy, ix, iy, 0.45f, 0.78f, 0.85f);
    }
    if (wire_from_ >= 0 && wire_from_ < int(data_.size())) {
        float ox, oy; data_out(data_[wire_from_], ox, oy);
        draw_wire(r, ox, oy, float(cx_), float(cy_), 0.55f, 0.85f, 0.80f);
    }

    // Shader node — one input port per named uniform
    r.draw_rounded_rect(sx_, sy_, sw_, sh_, 6.f, 0.14f, 0.15f, 0.18f, 1.0f);
    r.draw_rect(sx_, sy_, sw_, 3.f, 0.35f, 0.55f, 0.95f, 1.0f);
    r.draw_text(sx_ + 12.f, sy_ + 10.f, "Visuals", 0.90f, 0.92f, 0.95f, 1.0f);
    r.draw_text(sx_ + 80.f, sy_ + 12.f, "plasma + fx", 0.5f, 0.53f, 0.58f, 1.0f, 0.82f);
    for (int i = 0; i < kNumShaderUniforms; ++i) {
        float px, py; shader_in(i, px, py);
        const bool on = connected_[i] >= 0;
        r.draw_rect(px - 6.f, py - 6.f, 12.f, 12.f,
                    on ? 0.45f : 0.30f, on ? 0.78f : 0.33f, on ? 0.85f : 0.38f, 1.0f);
        r.draw_text(px + 14.f, py - 7.f, kShaderUniformNames[i],
                    on ? 0.85f : 0.55f, on ? 0.9f : 0.58f, on ? 0.95f : 0.63f, 1.0f, 0.9f);
    }

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
}

bool NodeGraph::on_down(double x, double y) {
    cx_ = x; cy_ = y;
    // Click a shader input port -> disconnect it.
    int port = nearest_shader_in(x, y, 14.0);
    if (port >= 0) { connected_[port] = -1; return true; }
    // Drag from a data node's output port -> start a wire.
    for (int i = 0; i < int(data_.size()); ++i) {
        float px, py; data_out(data_[i], px, py);
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
        int port = nearest_shader_in(x, y, 18.0);
        if (port >= 0) connected_[port] = wire_from_;
    }
    drag_mode_ = 0; drag_idx_ = -1; wire_from_ = -1;
}

}  // namespace vivid::ui
