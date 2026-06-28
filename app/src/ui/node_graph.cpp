#include "ui/node_graph.h"
#include "gpu/visual_graph.h"
#include <cmath>
#include <algorithm>

namespace vivid::ui {

// char_id (master=0/1, track t = 100+t*2+kind) -> canonical source id string.
static std::string source_id_for(int char_id) {
    if (char_id < 100) return char_id == 0 ? "master.level" : "master.transient";
    const int t = (char_id - 100) / 2, kind = (char_id - 100) % 2;
    return "track_" + std::to_string(t) + (kind == 0 ? ".level" : ".transient");
}
static std::string port_dest(int p) { return std::string("uniform.") + kShaderUniformNames[p]; }

NodeGraph::NodeGraph() {
    sx_ = 900.f; sy_ = 488.f; sw_ = 172.f; sh_ = 44.f + kNumShaderUniforms * 24.f;
    // Start with the master output level wired to "glow" (out-of-box reactivity).
    data_.push_back({ 560.f, 488.f, 168.f, 72.f, "Output \xC2\xB7 Level", 0, 0.f, 0 });
    reg_.connect("master.level", port_dest(3));  // glow <- level
}

// First data node whose characteristic matches `src` (for drawing its wire).
int NodeGraph::find_source_node(const std::string& src) const {
    for (int i = 0; i < int(data_.size()); ++i)
        if (source_id_for(data_[i].char_id) == src) return i;
    return -1;
}

void NodeGraph::set_bounds(float x0, float y0, float x1, float y1) {
    if (bounds_init_) {
        const float ddx = x0 - bx0_;  // pane left edge moved (splitter) -> carry nodes along
        if (ddx != 0.f) { for (auto& n : data_) n.x += ddx; sx_ += ddx; }
    }
    bx0_ = x0; by0_ = y0; bx1_ = x1; by1_ = y1; bounds_init_ = true;
    for (auto& n : data_) {
        n.x = std::clamp(n.x, bx0_, std::max(bx0_, bx1_ - n.w));
        n.y = std::clamp(n.y, by0_, std::max(by0_, by1_ - n.h));
    }
    sx_ = std::clamp(sx_, bx0_, std::max(bx0_, bx1_ - sw_));
    sy_ = std::clamp(sy_, by0_, std::max(by0_, by1_ - sh_));
}

void NodeGraph::set_value(int char_id, float v) {
    for (auto& n : data_) if (n.char_id == char_id) n.value = v;  // node bar display
    reg_.set_source(source_id_for(char_id), v);                   // mapping source
}
void NodeGraph::fill_uniforms(float* out) const {
    for (int i = 0; i < kNumShaderUniforms; ++i) out[i] = reg_.dest_value(port_dest(i));
}
void NodeGraph::add_data_node(const std::string& title, int char_id) {
    float y = by0_ + 22.f + data_.size() * 84.f;
    if (y > by1_ - 72.f) y = by1_ - 72.f;
    data_.push_back({ bx0_ + 20.f, y, 168.f, 72.f, title, char_id, 0.f, 90 });
}

void NodeGraph::get_node(int i, float& x, float& y, int& char_id, std::string& title) const {
    if (i < 0 || i >= static_cast<int>(data_.size())) return;
    x = data_[i].x; y = data_[i].y; char_id = data_[i].char_id; title = data_[i].title;
}
void NodeGraph::reset_nodes() {
    data_.clear();
    reg_.clear_mappings();
}
void NodeGraph::add_node_raw(const std::string& title, int char_id, float x, float y) {
    data_.push_back({ x, y, 168.f, 72.f, title, char_id, 0.f, 0 });
}

void NodeGraph::data_out(const DataNode& n, float& px, float& py) { px = n.x + n.w; py = n.y + n.h * 0.5f; }

// Op-node stack on the right of the visuals pane: 0=generator, 1=feedback, 2=blur,
// 3=output. Heights sized to their param-port counts (gen has 4).
void NodeGraph::op_box(int op, float& x, float& y, float& w, float& h) const {
    static const float kOpW = 158.f, kGap = 12.f;
    static const float hh[4] = { 32.f + 4 * 22.f, 54.f, 54.f, 40.f };
    x = bx1_ - kOpW - 8.f;
    y = by0_ + 4.f;
    for (int k = 0; k < op && k < 4; ++k) y += hh[k] + kGap;
    w = kOpW; h = hh[op < 0 ? 0 : (op > 3 ? 3 : op)];
}
// Param port -> the op-node that owns it (ports 0-3 generator, 4 feedback, 5 blur).
void NodeGraph::shader_in(int port, float& px, float& py) const {
    int op = 0, local = 0;
    if (port <= 3) { op = 0; local = port; }
    else if (port == 4) { op = 1; }
    else { op = 2; }
    float x, y, w, h; op_box(op, x, y, w, h);
    px = x;
    py = y + 30.f + local * 22.f;
}
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
    // Confine all graph drawing to the visuals pane.
    r.push_clip_rect(bx0_ - 6.f, by0_ - 18.f, (bx1_ - bx0_) + 12.f, (by1_ - by0_) + 24.f);
    r.draw_text(bx0_, by0_ - 16.f,
                "NETWORK — visuals chain on the right; drag a data port onto an op param. Click the generator to switch Plasma/Video.",
                0.45f, 0.48f, 0.53f, 1.0f, 0.86f);

    // Wires first (under the nodes): each mapped uniform -> its source data node.
    for (int i = 0; i < kNumShaderUniforms; ++i) {
        const std::string* src = reg_.source_of(port_dest(i));
        if (!src) continue;
        const int ni = find_source_node(*src);
        if (ni < 0) continue;
        float ox, oy, ix, iy; data_out(data_[ni], ox, oy); shader_in(i, ix, iy);
        draw_wire(r, ox, oy, ix, iy, 0.45f, 0.78f, 0.85f);
    }
    if (wire_from_ >= 0 && wire_from_ < int(data_.size())) {
        float ox, oy; data_out(data_[wire_from_], ox, oy);
        draw_wire(r, ox, oy, float(cx_), float(cy_), 0.55f, 0.85f, 0.80f);
    }

    // Op-node chain: generator -> feedback -> blur -> output.
    const bool video = vg_ && vg_->generator() == vivid::VOp::Video;
    const char* op_title[4] = { video ? "Video" : "Plasma", "Feedback", "Blur", "Output" };
    const char* op_sub[4]   = { video ? "source \xC2\xB7 V/N" : "generator \xC2\xB7 V", "fx", "fx", "\xE2\x86\x92 viewer" };
    // chain flow connectors (top of each box to the next)
    for (int op = 0; op < 3; ++op) {
        float x0, y0, w0, h0, x1, y1, w1, h1; op_box(op, x0, y0, w0, h0); op_box(op + 1, x1, y1, w1, h1);
        r.draw_rect(x0 + w0 * 0.5f - 1.f, y0 + h0, 2.f, (y1 - (y0 + h0)), 0.30f, 0.42f, 0.55f, 1.0f);
    }
    for (int op = 0; op < 4; ++op) {
        float x, y, w, h; op_box(op, x, y, w, h);
        const bool gen = (op == 0);
        r.draw_rounded_rect(x, y, w, h, 6.f, gen ? 0.16f : 0.13f, 0.15f, gen ? 0.20f : 0.18f, 1.0f);
        r.draw_rect(x, y, w, 3.f, gen ? 0.45f : 0.35f, gen ? 0.62f : 0.55f, 0.95f, 1.0f);
        r.draw_text(x + 12.f, y + 9.f, op_title[op], 0.90f, 0.92f, 0.95f, 1.0f, 0.95f);
        r.draw_text(x + w - 64.f, y + 11.f, op_sub[op], 0.5f, 0.53f, 0.58f, 1.0f, 0.78f);
    }
    // param ports on their owning op-nodes
    for (int i = 0; i < kNumShaderUniforms; ++i) {
        float px, py; shader_in(i, px, py);
        const bool on = reg_.source_of(port_dest(i)) != nullptr;
        r.draw_rect(px - 6.f, py - 6.f, 12.f, 12.f,
                    on ? 0.45f : 0.30f, on ? 0.78f : 0.33f, on ? 0.85f : 0.38f, 1.0f);
        r.draw_text(px + 14.f, py - 7.f, kShaderUniformNames[i],
                    on ? 0.85f : 0.55f, on ? 0.9f : 0.58f, on ? 0.95f : 0.63f, 1.0f, 0.88f);
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
    r.pop_clip_rect();
}

bool NodeGraph::on_down(double x, double y) {
    cx_ = x; cy_ = y;
    // Click a shader input port -> disconnect it.
    int port = nearest_shader_in(x, y, 14.0);
    if (port >= 0) { reg_.disconnect(port_dest(port)); return true; }
    // Drag from a data node's output port -> start a wire.
    for (int i = 0; i < int(data_.size()); ++i) {
        float px, py; data_out(data_[i], px, py);
        if (std::hypot(x - px, y - py) < 14.0) { drag_mode_ = 3; wire_from_ = i; return true; }
    }
    // Click the generator op-node -> switch Plasma <-> Video.
    { float gx, gy, gw, gh; op_box(0, gx, gy, gw, gh);
      if (in_rect(gx, gy, gw, gh, x, y)) {
          if (vg_) vg_->set_generator(vg_->generator() == vivid::VOp::Video ? vivid::VOp::Plasma : vivid::VOp::Video);
          return true;
      } }
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
    if (drag_mode_ == 3 && wire_from_ >= 0 && wire_from_ < int(data_.size())) {
        int port = nearest_shader_in(x, y, 18.0);
        if (port >= 0) reg_.connect(source_id_for(data_[wire_from_].char_id), port_dest(port));
    }
    drag_mode_ = 0; drag_idx_ = -1; wire_from_ = -1;
}

}  // namespace vivid::ui
