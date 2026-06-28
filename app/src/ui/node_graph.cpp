#include "ui/node_graph.h"
#include <cmath>
#include <algorithm>

namespace vivid::ui {

using vivid::VOp;

// ---- data-source identity + uniform routing ----
static std::string source_id_for(int char_id) {
    if (char_id < 100) return char_id == 0 ? "master.level" : "master.transient";
    const int t = (char_id - 100) / 2, kind = (char_id - 100) % 2;
    return "track_" + std::to_string(t) + (kind == 0 ? ".level" : ".transient");
}
static std::string port_dest(int p) { return std::string("uniform.") + kShaderUniformNames[p]; }

static VOp  uniform_owner(int u) { return u <= 3 ? VOp::Plasma : (u == 4 ? VOp::Feedback : VOp::Blur); }
static int  uniform_local(int u) { return u <= 3 ? u : 0; }
static bool op_has_input(VOp op)  { return op == VOp::Feedback || op == VOp::Blur || op == VOp::Output; }
static bool op_has_output(VOp op) { return op != VOp::Output; }
static const char* op_name(VOp op) {
    switch (op) { case VOp::Plasma: return "Plasma"; case VOp::Video: return "Video";
        case VOp::Feedback: return "Feedback"; case VOp::Blur: return "Blur"; default: return "Output"; }
}
int NodeGraph::op_param_uniforms(VOp op, int out[4]) {
    switch (op) {
        case VOp::Plasma:   out[0]=0; out[1]=1; out[2]=2; out[3]=3; return 4;
        case VOp::Feedback: out[0]=4; return 1;
        case VOp::Blur:     out[0]=5; return 1;
        default: return 0;
    }
}

NodeGraph::NodeGraph() {
    data_.push_back({ 560.f, 540.f, 168.f, 72.f, "Output \xC2\xB7 Level", 0, 0.f, 0 });
    reg_.connect("master.level", port_dest(3));  // glow <- level (out-of-box reactivity)
}

int NodeGraph::find_source_node(const std::string& src) const {
    for (int i = 0; i < int(data_.size()); ++i)
        if (source_id_for(data_[i].char_id) == src) return i;
    return -1;
}

// ---- op-node layout (positions parallel to vg_->nodes()) ----
void NodeGraph::sync_op_pos() {
    if (!vg_) return;
    const int n = static_cast<int>(vg_->nodes().size());
    if (!op_pos_init_) {
        op_pos_.clear();
        for (int i = 0; i < n; ++i) op_pos_.push_back({ bx0_ + 230.f + i * 150.f, by0_ + 30.f });
        op_pos_init_ = true;
    }
    while (static_cast<int>(op_pos_.size()) < n)
        op_pos_.push_back({ bx0_ + 230.f + (static_cast<int>(op_pos_.size())) * 60.f, by0_ + 150.f });
    while (static_cast<int>(op_pos_.size()) > n) op_pos_.pop_back();
    for (auto& p : op_pos_) {
        p.first  = std::clamp(p.first,  bx0_, std::max(bx0_, bx1_ - 146.f));
        p.second = std::clamp(p.second, by0_, std::max(by0_, by1_ - 116.f));
    }
}
static int param_count_of(VOp op) {
    switch (op) { case VOp::Plasma: return 4; case VOp::Feedback: case VOp::Blur: return 1; default: return 0; }
}
// Total left-edge input rows: the texture input (if any) + one per param.
static int op_input_rows(VOp op) { return (op_has_input(op) ? 1 : 0) + param_count_of(op); }
static float op_row_y(float y, int row) { return y + 30.f + row * 18.f; }  // center of input row

void NodeGraph::op_node_rect(int i, float& x, float& y, float& w, float& h) const {
    x = (i >= 0 && i < int(op_pos_.size())) ? op_pos_[i].first : bx0_;
    y = (i >= 0 && i < int(op_pos_.size())) ? op_pos_[i].second : by0_;
    w = 144.f;
    const VOp op = (vg_ && i >= 0 && i < int(vg_->nodes().size())) ? vg_->nodes()[i].op : VOp::Plasma;
    h = 30.f + std::max(1, op_input_rows(op)) * 18.f + 6.f;
}

// classic-style type accent (r,g,b) for an op.
static void op_accent(VOp op, float& r, float& g, float& b) {
    switch (op) {
        case VOp::Plasma: case VOp::Video: r = 0.35f; g = 0.55f; b = 0.95f; break;  // generator: blue
        case VOp::Output: r = 0.93f; g = 0.78f; b = 0.38f; break;                   // output: amber
        default:          r = 0.60f; g = 0.45f; b = 0.85f; break;                   // effect: violet
    }
}
static void port_dot(Renderer2D& rr, float px, float py, float rad, float r, float g, float b) {
    rr.draw_rounded_rect(px - rad, py - rad, rad * 2.f, rad * 2.f, rad, r, g, b, 1.0f);  // filled circle
}
bool NodeGraph::op_in_port(int i, float& px, float& py) const {  // texture input: left, row 0
    if (!vg_ || i < 0 || i >= int(vg_->nodes().size()) || !op_has_input(vg_->nodes()[i].op)) return false;
    float x, y, w, h; op_node_rect(i, x, y, w, h); px = x; py = op_row_y(y, 0); return true;
}
bool NodeGraph::op_out_port(int i, float& px, float& py) const {  // output: right, vertical centre
    if (!vg_ || i < 0 || i >= int(vg_->nodes().size()) || !op_has_output(vg_->nodes()[i].op)) return false;
    float x, y, w, h; op_node_rect(i, x, y, w, h); px = x + w; py = y + h * 0.5f; return true;
}
int NodeGraph::first_node_of(VOp op) const {
    if (!vg_) return -1;
    for (int i = 0; i < int(vg_->nodes().size()); ++i) if (vg_->nodes()[i].op == op) return i;
    return -1;
}
bool NodeGraph::param_port(int uniform, float& px, float& py) const {  // param input: left edge
    const VOp owner = uniform_owner(uniform);
    const int ni = first_node_of(owner);
    if (ni < 0) return false;
    float x, y, w, h; op_node_rect(ni, x, y, w, h);
    const int row = (op_has_input(owner) ? 1 : 0) + uniform_local(uniform);  // after the texture input
    px = x; py = op_row_y(y, row);
    return true;
}
int NodeGraph::nearest_param(double x, double y, double maxd) const {
    int best = -1; double bd = maxd;
    for (int u = 0; u < kNumShaderUniforms; ++u) {
        float px, py; if (!param_port(u, px, py)) continue;
        double d = std::hypot(x - px, y - py); if (d < bd) { bd = d; best = u; }
    }
    return best;
}
int NodeGraph::nearest_op_in(double x, double y, double maxd) const {
    int best = -1; double bd = maxd;
    if (vg_) for (int i = 0; i < int(vg_->nodes().size()); ++i) {
        float px, py; if (!op_in_port(i, px, py)) continue;
        double d = std::hypot(x - px, y - py); if (d < bd) { bd = d; best = i; }
    }
    return best;
}
int NodeGraph::nearest_op_out(double x, double y, double maxd) const {
    int best = -1; double bd = maxd;
    if (vg_) for (int i = 0; i < int(vg_->nodes().size()); ++i) {
        float px, py; if (!op_out_port(i, px, py)) continue;
        double d = std::hypot(x - px, y - py); if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void NodeGraph::set_bounds(float x0, float y0, float x1, float y1) {
    if (bounds_init_) {
        const float ddx = x0 - bx0_;
        if (ddx != 0.f) { for (auto& n : data_) n.x += ddx; for (auto& p : op_pos_) p.first += ddx; }
    }
    bx0_ = x0; by0_ = y0; bx1_ = x1; by1_ = y1; bounds_init_ = true;
    for (auto& n : data_) {
        n.x = std::clamp(n.x, bx0_, std::max(bx0_, bx1_ - n.w));
        n.y = std::clamp(n.y, by0_, std::max(by0_, by1_ - n.h));
    }
    sync_op_pos();
}

void NodeGraph::set_value(int char_id, float v) {
    for (auto& n : data_) if (n.char_id == char_id) n.value = v;
    reg_.set_source(source_id_for(char_id), v);
}
void NodeGraph::fill_uniforms(float* out) const {
    for (int i = 0; i < kNumShaderUniforms; ++i) out[i] = reg_.dest_value(port_dest(i));
}
void NodeGraph::add_data_node(const std::string& title, int char_id) {
    float y = by0_ + 150.f + data_.size() * 84.f;
    if (y > by1_ - 72.f) y = by1_ - 72.f;
    data_.push_back({ bx0_ + 20.f, y, 168.f, 72.f, title, char_id, 0.f, 90 });
}
void NodeGraph::get_node(int i, float& x, float& y, int& char_id, std::string& title) const {
    if (i < 0 || i >= int(data_.size())) return;
    x = data_[i].x; y = data_[i].y; char_id = data_[i].char_id; title = data_[i].title;
}
void NodeGraph::reset_nodes() { data_.clear(); reg_.clear_mappings(); }

int NodeGraph::op_count() const { return vg_ ? int(vg_->nodes().size()) : 0; }
void NodeGraph::get_op(int i, int& op, int& input, float& x, float& y) const {
    if (!vg_ || i < 0 || i >= int(vg_->nodes().size())) return;
    op = static_cast<int>(vg_->nodes()[i].op);
    input = vg_->nodes()[i].input;
    x = (i < int(op_pos_.size())) ? op_pos_[i].first : 0.f;
    y = (i < int(op_pos_.size())) ? op_pos_[i].second : 0.f;
}
void NodeGraph::chain_load_begin() { if (vg_) vg_->clear_nodes(); op_pos_.clear(); op_pos_init_ = true; }
void NodeGraph::chain_load_add(int op, float x, float y) {
    if (!vg_) return;
    vg_->add_node(static_cast<vivid::VOp>(op));
    op_pos_.push_back({ x, y });
}
void NodeGraph::chain_load_set_input(int i, int input) { if (vg_) vg_->set_input(i, input); }
void NodeGraph::add_node_raw(const std::string& title, int char_id, float x, float y) {
    data_.push_back({ x, y, 168.f, 72.f, title, char_id, 0.f, 0 });
}

void NodeGraph::data_out(const DataNode& n, float& px, float& py) { px = n.x + n.w; py = n.y + n.h * 0.5f; }
bool NodeGraph::in_rect(float rx, float ry, float rw, float rh, double x, double y) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static void draw_wire(Renderer2D& r, float x0, float y0, float x1, float y1, float cr, float cg, float cb) {
    const int N = 26; float xs[N], ys[N];
    float dx = std::fabs(x1 - x0) * 0.5f; if (dx < 28.f) dx = 28.f;
    const float c1x = x0 + dx, c1y = y0, c2x = x1 - dx, c2y = y1;
    for (int i = 0; i < N; ++i) {
        const float t = i / float(N - 1), u = 1.f - t;
        xs[i] = u*u*u*x0 + 3*u*u*t*c1x + 3*u*t*t*c2x + t*t*t*x1;
        ys[i] = u*u*u*y0 + 3*u*u*t*c1y + 3*u*t*t*c2y + t*t*t*y1;
    }
    r.draw_polyline(xs, ys, N, 2.2f, cr, cg, cb, 1.0f);
}

// ---- op palette (add nodes) ----
static const VOp kPalette[4] = { VOp::Plasma, VOp::Video, VOp::Feedback, VOp::Blur };
int NodeGraph::palette_hit(double x, double y) const {
    for (int j = 0; j < 4; ++j) {
        const float rx = bx0_ + j * 84.f, ry = by1_ - 22.f;
        if (in_rect(rx, ry, 80.f, 18.f, x, y)) return j;
    }
    return -1;
}
void NodeGraph::draw_op_palette(Renderer2D& r) {
    r.draw_text(bx0_, by1_ - 38.f, "add op:", 0.45f, 0.48f, 0.53f, 1.0f, 0.78f);
    for (int j = 0; j < 4; ++j) {
        const float rx = bx0_ + j * 84.f, ry = by1_ - 22.f;
        r.draw_rect(rx, ry, 80.f, 18.f, 0.14f, 0.16f, 0.19f, 1.0f);
        r.draw_rect(rx, ry, 3.f, 18.f, 0.35f, 0.55f, 0.95f, 1.0f);
        char b[20]; std::snprintf(b, sizeof b, "+ %s", op_name(kPalette[j]));
        r.draw_text(rx + 8.f, ry + 3.f, b, 0.78f, 0.8f, 0.84f, 1.0f, 0.78f);
    }
}

void NodeGraph::draw(Renderer2D& r) {
    sync_op_pos();
    r.push_clip_rect(bx0_ - 6.f, by0_ - 18.f, (bx1_ - bx0_) + 12.f, (by1_ - by0_) + 28.f);
    r.draw_text(bx0_, by0_ - 16.f,
                "NETWORK — wire op outputs (right) into inputs (left), ending in Output. Drag a data port onto an op param.",
                0.45f, 0.48f, 0.53f, 1.0f, 0.86f);

    const int n = vg_ ? int(vg_->nodes().size()) : 0;
    // chain wires (op output -> op input)
    for (int i = 0; i < n; ++i) {
        const int in = vg_->nodes()[i].input;
        float ox, oy, ix, iy;
        if (in >= 0 && in < n && op_out_port(in, ox, oy) && op_in_port(i, ix, iy))
            draw_wire(r, ox, oy, ix, iy, 0.50f, 0.60f, 0.68f);  // classic grayish-blue
    }
    // param wires (data node -> uniform port)
    for (int u = 0; u < kNumShaderUniforms; ++u) {
        const std::string* src = reg_.source_of(port_dest(u));
        if (!src) continue;
        const int dn = find_source_node(*src);
        float px, py; if (dn < 0 || !param_port(u, px, py)) continue;
        float ox, oy; data_out(data_[dn], ox, oy);
        draw_wire(r, ox, oy, px, py, 0.45f, 0.78f, 0.85f);
    }
    // drag preview
    if (drag_mode_ == 3 && wire_from_ >= 0 && wire_from_ < n + int(data_.size())) {
        float ox, oy; if (wire_from_ < int(data_.size())) { data_out(data_[wire_from_], ox, oy); draw_wire(r, ox, oy, float(cx_), float(cy_), 0.55f, 0.85f, 0.80f); }
    }
    if (drag_mode_ == 4 && wire_from_ >= 0) {
        float ox, oy; if (op_out_port(wire_from_, ox, oy)) draw_wire(r, ox, oy, float(cx_), float(cy_), 0.5f, 0.65f, 0.9f);
    }

    // op-nodes (classic-style cards)
    for (int i = 0; i < n; ++i) {
        const VOp op = vg_->nodes()[i].op;
        float x, y, w, h; op_node_rect(i, x, y, w, h);
        const bool out = (op == VOp::Output);
        float ar, ag, ab; op_accent(op, ar, ag, ab);
        r.draw_rounded_rect(x, y, w, h, 5.f, 0.12f, 0.13f, 0.155f, 1.0f);          // body
        r.draw_rect(x + 1.f, y + 3.f, w - 2.f, 19.f, 0.17f, 0.18f, 0.21f, 1.0f);   // header strip
        r.draw_rect(x, y, w, 3.f, ar, ag, ab, 1.0f);                               // accent bar
        r.draw_text(x + 10.f, y + 6.f, op_name(op), 0.90f, 0.92f, 0.95f, 1.0f, 0.95f);
        if (out) r.draw_text(x + w - 56.f, y + 6.f, "\xE2\x86\x92 viewer", 0.6f, 0.55f, 0.4f, 1.0f, 0.72f);
        float px, py;
        if (op_in_port(i, px, py)) {  // texture input
            port_dot(r, px, py, 5.f, 0.55f, 0.62f, 0.72f);
            r.draw_text(px + 10.f, py - 5.f, "in", 0.55f, 0.58f, 0.62f, 1.0f, 0.7f);
        }
        if (op_out_port(i, px, py)) port_dot(r, px, py, 5.f, 0.55f, 0.62f, 0.72f);  // output (right)
        if (!out) r.draw_text(x + w - 14.f, y + 5.f, "\xC3\x97", 0.7f, 0.45f, 0.45f, 1.0f, 0.95f);
    }
    // param input ports + labels (down the owning op's left edge)
    for (int u = 0; u < kNumShaderUniforms; ++u) {
        float px, py; if (!param_port(u, px, py)) continue;
        const bool on = reg_.source_of(port_dest(u)) != nullptr;
        port_dot(r, px, py, 4.f, on ? 0.31f : 0.34f, on ? 0.80f : 0.40f, on ? 0.75f : 0.45f);
        r.draw_text(px + 10.f, py - 5.f, kShaderUniformNames[u],
                    on ? 0.72f : 0.48f, on ? 0.82f : 0.5f, on ? 0.78f : 0.55f, 1.0f, 0.68f);
    }

    // data nodes (matching card style)
    for (auto& nd : data_) {
        if (nd.flash > 0) { r.draw_rounded_rect(nd.x - 3.f, nd.y - 3.f, nd.w + 6.f, nd.h + 6.f, 6.f, 0.31f, 0.80f, 0.75f, 1.0f); nd.flash--; }
        r.draw_rounded_rect(nd.x, nd.y, nd.w, nd.h, 5.f, 0.12f, 0.13f, 0.155f, 1.0f);
        r.draw_rect(nd.x + 1.f, nd.y + 3.f, nd.w - 2.f, 20.f, 0.15f, 0.18f, 0.18f, 1.0f);  // header strip
        r.draw_rect(nd.x, nd.y, nd.w, 3.f, 0.31f, 0.80f, 0.75f, 1.0f);                     // accent
        r.draw_text(nd.x + 12.f, nd.y + 6.f, nd.title.c_str(), 0.90f, 0.92f, 0.95f, 1.0f, 0.92f);
        r.draw_text(nd.x + 12.f, nd.y + 30.f, "data source", 0.5f, 0.53f, 0.58f, 1.0f, 0.8f);
        r.draw_rect(nd.x + 12.f, nd.y + 50.f, (nd.w - 40.f) * (nd.value > 1 ? 1 : nd.value), 7.f, 0.31f, 0.80f, 0.75f, 1.0f);
        float px, py; data_out(nd, px, py);
        port_dot(r, px, py, 5.f, 0.31f, 0.80f, 0.75f);
    }

    draw_op_palette(r);
    r.pop_clip_rect();
}

bool NodeGraph::on_down(double x, double y) {
    cx_ = x; cy_ = y;
    sync_op_pos();
    const int n = vg_ ? int(vg_->nodes().size()) : 0;

    // disconnect an op input or a param port
    int oi = nearest_op_in(x, y, 13.0);
    if (oi >= 0) { vg_->set_input(oi, -1); return true; }
    int up = nearest_param(x, y, 12.0);
    if (up >= 0) { reg_.disconnect(port_dest(up)); return true; }

    // start a wire from a data-node output port
    for (int i = 0; i < int(data_.size()); ++i) {
        float px, py; data_out(data_[i], px, py);
        if (std::hypot(x - px, y - py) < 13.0) { drag_mode_ = 3; wire_from_ = i; return true; }
    }
    // start a wire from an op output port
    int oo = nearest_op_out(x, y, 13.0);
    if (oo >= 0) { drag_mode_ = 4; wire_from_ = oo; return true; }

    // op-node x button / body drag
    for (int i = 0; i < n; ++i) {
        float ox, oy, ow, oh; op_node_rect(i, ox, oy, ow, oh);
        if (vg_->nodes()[i].op != VOp::Output && in_rect(ox + ow - 15.f, oy + 3.f, 12.f, 12.f, x, y)) {
            vg_->remove_node(i); sync_op_pos(); return true;
        }
        if (in_rect(ox, oy, ow, oh, x, y)) { drag_mode_ = 2; drag_idx_ = i; dx_ = x - ox; dy_ = y - oy; return true; }
    }
    // palette -> add an op
    int pj = palette_hit(x, y);
    if (pj >= 0 && vg_) { vg_->add_node(kPalette[pj]); sync_op_pos(); return true; }

    // data-node body drag
    for (int i = 0; i < int(data_.size()); ++i)
        if (in_rect(data_[i].x, data_[i].y, data_[i].w, data_[i].h, x, y)) {
            drag_mode_ = 1; drag_idx_ = i; dx_ = x - data_[i].x; dy_ = y - data_[i].y; return true;
        }
    return false;
}

void NodeGraph::on_move(double x, double y) {
    cx_ = x; cy_ = y;
    if (drag_mode_ == 1 && drag_idx_ >= 0 && drag_idx_ < int(data_.size())) {
        data_[drag_idx_].x = float(x - dx_); data_[drag_idx_].y = float(y - dy_);
    } else if (drag_mode_ == 2 && drag_idx_ >= 0 && drag_idx_ < int(op_pos_.size())) {
        op_pos_[drag_idx_] = { float(x - dx_), float(y - dy_) };
    }
}

void NodeGraph::on_up(double x, double y) {
    if (drag_mode_ == 3 && wire_from_ >= 0 && wire_from_ < int(data_.size())) {
        int u = nearest_param(x, y, 18.0);
        if (u >= 0) reg_.connect(source_id_for(data_[wire_from_].char_id), port_dest(u));
    } else if (drag_mode_ == 4 && wire_from_ >= 0) {
        int target = nearest_op_in(x, y, 18.0);
        if (target >= 0 && vg_) vg_->set_input(target, wire_from_);
    }
    drag_mode_ = 0; drag_idx_ = -1; wire_from_ = -1;
}

}  // namespace vivid::ui
