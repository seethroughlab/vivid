#include "ui/node_graph.h"
#include <cmath>
#include <algorithm>
#include <cctype>

namespace vivid::ui {

using vivid::VOp;

// ---- data-source identity + uniform routing ----
static const char* kKindName[5] = { "level", "transient", "low", "mid", "high" };
static std::string source_id_for(int char_id) {  // master=kind, track t = 100+t*8+kind
    if (char_id < 100) return std::string("master.") + (char_id >= 0 && char_id < 5 ? kKindName[char_id] : "level");
    const int t = (char_id - 100) / 8, kind = (char_id - 100) % 8;
    return "track_" + std::to_string(t) + "." + (kind >= 0 && kind < 5 ? kKindName[kind] : "level");
}

// ---- Tab chooser: the op entries come from the operator registry (so new ops
// appear automatically); these are the static master audio-source entries. ----
struct SourceEntry { const char* label; int char_id; };
static const SourceEntry kSources[] = {
    { "Master Level", 0 }, { "Master Transient", 1 }, { "Master Low", 2 }, { "Master Mid", 3 }, { "Master High", 4 },
};
static std::string lower_str(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
// Canonical op-type / local-index for the 6 named uniforms (used only to publish
// the back-compat viz.* return-path sources from the first node of each type).
static VOp  uniform_owner(int u) { return u <= 3 ? VOp::Plasma : (u == 4 ? VOp::Feedback : VOp::Blur); }
static int  uniform_local(int u) { return u <= 3 ? u : 0; }
static bool op_has_input(VOp op)  { return op == VOp::Feedback || op == VOp::Blur || op == VOp::Output; }
static bool op_has_output(VOp op) { return op != VOp::Output; }
static const char* op_name(VOp op) {
    switch (op) { case VOp::Plasma: return "Plasma"; case VOp::Video: return "Video";
        case VOp::Feedback: return "Feedback"; case VOp::Blur: return "Blur"; default: return "Output"; }
}
// Per-node param identity, operator-driven: param count + names come from the
// node's hosted operator descriptor (inst.param_ptrs), so new ops' params appear
// automatically.
static int node_pcount(const vivid::VisualGraph* vg, int i) {
    if (!vg || i < 0 || i >= int(vg->nodes().size())) return 0;
    return int(vg->nodes()[i].inst.param_ptrs.size());
}
static const char* node_plabel(const vivid::VisualGraph* vg, int i, int local) {
    if (!vg || i < 0 || i >= int(vg->nodes().size())) return "";
    const auto& pp = vg->nodes()[i].inst.param_ptrs;
    return (local >= 0 && local < int(pp.size())) ? pp[local]->name : "";
}
static std::string node_param_dest(int id, const char* name) {
    return "node:" + std::to_string(id) + "." + name;
}

NodeGraph::NodeGraph() {
    data_.push_back({ 560.f, 540.f, 168.f, 72.f, "Output \xC2\xB7 Level", 0, 0.f, 0 });
    // out-of-box reactivity is seeded in set_visual_graph (needs the default chain's ids).
}

void NodeGraph::set_visual_graph(vivid::VisualGraph* vg) {
    vg_ = vg;
    if (vg_ && reg_.mappings().empty()) {  // seed glow <- master level on the first Plasma
        const int ni = first_node_of(VOp::Plasma);
        if (ni >= 0) reg_.connect("master.level", node_param_dest(vg_->nodes()[ni].id, "glow"));
    }
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
    // No per-node clamp: nodes live in a pannable canvas and the pane clip-rect
    // keeps drawing contained. Panning would otherwise be undone every frame.
}
// Total left-edge input rows: the texture input (if any) + one per param.
static int op_input_rows_at(const vivid::VisualGraph* vg, int i) {
    if (!vg || i < 0 || i >= int(vg->nodes().size())) return 0;
    return (op_has_input(vg->nodes()[i].op) ? 1 : 0) + node_pcount(vg, i);
}
static float op_row_y(float y, int row) { return y + 30.f + row * 18.f; }  // center of input row

static constexpr float kCardW  = 156.f;
static constexpr float kThumbH = 46.f;                 // live-output thumbnail strip
static bool op_has_thumb(VOp op) { return op != VOp::Output; }

void NodeGraph::op_node_rect(int i, float& x, float& y, float& w, float& h) const {
    x = (i >= 0 && i < int(op_pos_.size())) ? op_pos_[i].first : bx0_;
    y = (i >= 0 && i < int(op_pos_.size())) ? op_pos_[i].second : by0_;
    w = kCardW;
    const VOp op = (vg_ && i >= 0 && i < int(vg_->nodes().size())) ? vg_->nodes()[i].op : VOp::Plasma;
    h = 30.f + std::max(1, op_input_rows_at(vg_, i)) * 18.f + (op_has_thumb(op) ? kThumbH + 8.f : 6.f);
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
bool NodeGraph::param_port(int node_idx, int local, float& px, float& py) const {  // left edge, per node
    if (!vg_ || node_idx < 0 || node_idx >= int(vg_->nodes().size())) return false;
    const VOp op = vg_->nodes()[node_idx].op;
    if (local < 0 || local >= node_pcount(vg_, node_idx)) return false;
    float x, y, w, h; op_node_rect(node_idx, x, y, w, h);
    const int row = (op_has_input(op) ? 1 : 0) + local;  // after the texture input
    px = x; py = op_row_y(y, row);
    return true;
}
bool NodeGraph::nearest_param(double x, double y, double maxd, int& node_idx, int& local) const {
    bool found = false; double bd = maxd;
    if (!vg_) return false;
    for (int i = 0; i < int(vg_->nodes().size()); ++i) {
        const int pc = node_pcount(vg_, i);
        for (int l = 0; l < pc; ++l) {
            float px, py; if (!param_port(i, l, px, py)) continue;
            double d = std::hypot(x - px, y - py);
            if (d < bd) { bd = d; node_idx = i; local = l; found = true; }
        }
    }
    return found;
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
    // No per-node clamp (pannable canvas); the splitter shift above keeps nodes
    // following the pane's left edge, and the clip-rect contains the drawing.
    sync_op_pos();
}

void NodeGraph::set_value(int char_id, float v) {
    for (auto& n : data_) if (n.char_id == char_id) {
        n.value = v;
        n.hist[n.hist_head] = v;                       // push into the rolling history
        n.hist_head = (n.hist_head + 1) % kHistN;
    }
    reg_.set_source(source_id_for(char_id), v);
}
void NodeGraph::apply_params() {
    if (!vg_) return;
    auto& nodes = vg_->nodes();
    for (auto& n : nodes) {
        const int pc = int(n.inst.param_ptrs.size());  // operator-declared params
        n.params.resize(pc, 0.f);
        n.base.resize(pc, 0.f);
        for (int l = 0; l < pc; ++l) {
            const float mod = reg_.dest_value(node_param_dest(n.id, n.inst.param_ptrs[l]->name));
            n.params[l] = std::clamp(n.base[l] + mod, 0.f, 1.f);  // manual base + live modulation
        }
    }
    // Back-compat return-path sources: the canonical six viz.<name>, taken from the
    // first node of each op-type (keeps P27's static map-source catalog working).
    for (int u = 0; u < kNumShaderUniforms; ++u) {
        const int ni = first_node_of(uniform_owner(u));
        const int local = uniform_local(u);
        const float v = (ni >= 0 && local < int(nodes[ni].params.size())) ? nodes[ni].params[local] : 0.f;
        reg_.set_source(std::string("viz.") + kShaderUniformNames[u], v);
    }
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
void NodeGraph::get_op(int i, int& op, int& input, int& id, float& x, float& y) const {
    if (!vg_ || i < 0 || i >= int(vg_->nodes().size())) return;
    op = static_cast<int>(vg_->nodes()[i].op);
    input = vg_->nodes()[i].input;
    id = vg_->nodes()[i].id;
    x = (i < int(op_pos_.size())) ? op_pos_[i].first : 0.f;
    y = (i < int(op_pos_.size())) ? op_pos_[i].second : 0.f;
}
std::string NodeGraph::op_type_at(int i) const {
    return (vg_ && i >= 0 && i < int(vg_->nodes().size())) ? vg_->nodes()[i].op_type : std::string();
}
void NodeGraph::get_op_base(int i, float out[4]) const {
    for (int l = 0; l < 4; ++l)
        out[l] = (vg_ && i >= 0 && i < int(vg_->nodes().size()) && l < int(vg_->nodes()[i].base.size()))
            ? vg_->nodes()[i].base[l] : 0.f;
}

// ---- visual-node inspector accessors ----
static bool op_node_valid(const vivid::VisualGraph* vg, int i) {
    return vg && i >= 0 && i < int(vg->nodes().size());
}
int NodeGraph::op_kind(int i) const { return op_node_valid(vg_, i) ? int(vg_->nodes()[i].op) : -1; }
const char* NodeGraph::op_kind_name(int i) const { return op_node_valid(vg_, i) ? vg_->nodes()[i].op_type.c_str() : ""; }
int NodeGraph::op_param_count_at(int i) const { return node_pcount(vg_, i); }
const char* NodeGraph::op_param_label_at(int i, int local) const { return node_plabel(vg_, i, local); }
float NodeGraph::op_param_base_at(int i, int local) const {
    return (op_node_valid(vg_, i) && local >= 0 && local < int(vg_->nodes()[i].base.size())) ? vg_->nodes()[i].base[local] : 0.f;
}
void NodeGraph::set_op_param_base_at(int i, int local, float v) {
    if (op_node_valid(vg_, i) && local >= 0) {
        auto& base = vg_->nodes()[i].base;
        if (local >= int(base.size())) base.resize(local + 1, 0.f);
        base[local] = std::clamp(v, 0.f, 1.f);
    }
}
float NodeGraph::op_param_value_at(int i, int local) const {
    return (op_node_valid(vg_, i) && local >= 0 && local < int(vg_->nodes()[i].params.size())) ? vg_->nodes()[i].params[local] : 0.f;
}
bool NodeGraph::op_param_wired_at(int i, int local) const {
    if (!op_node_valid(vg_, i)) return false;
    return reg_.source_of(node_param_dest(vg_->nodes()[i].id, node_plabel(vg_, i, local))) != nullptr;
}

void NodeGraph::chain_load_begin() { if (vg_) vg_->clear_nodes(); op_pos_.clear(); op_pos_init_ = true; sel_op_ = -1; }
void NodeGraph::chain_load_add(const std::string& op_type, int id, float x, float y) {
    if (!vg_) return;
    vg_->load_node(op_type, id);
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
    // NETWORK header is chrome — drawn at identity, before the view transform.
    r.draw_text(bx0_, by0_ - 16.f,
                "NETWORK — wire op outputs (right) into inputs (left), ending in Output. Drag a data port onto an op param.",
                0.45f, 0.48f, 0.53f, 1.0f, 0.86f);
    // Everything below is graph content: drawn in WORLD space through the view
    // transform (pan + zoom). Chrome (palette) resets the transform first.
    r.set_transform(view_ox_, view_oy_, view_scale_);
    // grid: cover the visible world region; 1px-on-screen line width = 1/scale world
    const float gs = 38.f;
    const float gx0 = bx0_ - 6.f, gy0 = by0_ - 6.f, gx1 = bx1_ + 6.f, gy1 = by1_ + 6.f;
    double wl, wt, wr, wb; to_world(gx0, gy0, wl, wt); to_world(gx1, gy1, wr, wb);
    const float lw = 1.f / view_scale_;
    for (float gx = std::floor(float(wl) / gs) * gs; gx < float(wr); gx += gs)
        r.draw_rect(gx, float(wt), lw, float(wb - wt), 0.105f, 0.115f, 0.14f, 1.0f);
    for (float gy = std::floor(float(wt) / gs) * gs; gy < float(wb); gy += gs)
        r.draw_rect(float(wl), gy, float(wr - wl), lw, 0.105f, 0.115f, 0.14f, 1.0f);

    const int n = vg_ ? int(vg_->nodes().size()) : 0;
    // chain wires (op output -> op input)
    for (int i = 0; i < n; ++i) {
        const int in = vg_->nodes()[i].input;
        float ox, oy, ix, iy;
        if (in >= 0 && in < n && op_out_port(in, ox, oy) && op_in_port(i, ix, iy))
            draw_wire(r, ox, oy, ix, iy, 0.50f, 0.60f, 0.68f);  // classic grayish-blue
    }
    // param wires (data node -> per-node param port)
    for (int i = 0; i < n; ++i) {
        const int pc = node_pcount(vg_, i);
        for (int l = 0; l < pc; ++l) {
            const std::string* src = reg_.source_of(node_param_dest(vg_->nodes()[i].id, node_plabel(vg_, i, l)));
            if (!src) continue;
            const int dn = find_source_node(*src);
            float px, py; if (dn < 0 || !param_port(i, l, px, py)) continue;
            float ox, oy; data_out(data_[dn], ox, oy);
            draw_wire(r, ox, oy, px, py, 0.45f, 0.78f, 0.85f);
        }
    }
    // drag preview
    if (drag_mode_ == 3 && wire_from_ >= 0 && wire_from_ < n + int(data_.size())) {
        float ox, oy; if (wire_from_ < int(data_.size())) { data_out(data_[wire_from_], ox, oy); draw_wire(r, ox, oy, float(cx_), float(cy_), 0.55f, 0.85f, 0.80f); }
    }
    if (drag_mode_ == 4 && wire_from_ >= 0) {
        float ox, oy; if (op_out_port(wire_from_, ox, oy)) draw_wire(r, ox, oy, float(cx_), float(cy_), 0.5f, 0.65f, 0.9f);
    }

    // op-nodes (classic-style cards)
    const int active_out_idx = vg_ ? vg_->output_index() : -1;
    if (sel_op_ >= n) sel_op_ = -1;   // drop a stale selection (node removed/reloaded)
    for (int i = 0; i < n; ++i) {
        const VOp op = vg_->nodes()[i].op;
        float x, y, w, h; op_node_rect(i, x, y, w, h);
        const bool out = (op == VOp::Output);
        const bool active_out = out && i == active_out_idx;   // drives the viewer
        float ar, ag, ab; op_accent(op, ar, ag, ab);
        if (i == sel_op_)  // selection ring (inspector target) — bright outline
            r.draw_rounded_rect(x - 3.f, y - 3.f, w + 6.f, h + 6.f, 7.f, 0.95f, 0.82f, 0.38f, 1.0f);
        else if (active_out)  // highlight ring on the active output
            r.draw_rounded_rect(x - 2.f, y - 2.f, w + 4.f, h + 4.f, 6.f, ar, ag, ab, 1.0f);
        r.draw_rounded_rect(x, y, w, h, 5.f, 0.12f, 0.13f, 0.155f, 1.0f);          // body
        r.draw_rect(x + 1.f, y + 3.f, w - 2.f, 19.f, 0.17f, 0.18f, 0.21f, 1.0f);   // header strip
        r.draw_rect(x, y, w, 3.f, ar, ag, ab, 1.0f);                               // accent bar
        r.draw_text(x + 10.f, y + 6.f, vg_->nodes()[i].op_type.c_str(), 0.90f, 0.92f, 0.95f, 1.0f, 0.95f);
        if (out) r.draw_text(x + w - 56.f, y + 6.f, active_out ? "\xE2\x86\x92 viewer" : "output",
                             active_out ? 0.7f : 0.45f, active_out ? 0.6f : 0.47f, active_out ? 0.4f : 0.5f, 1.0f, 0.72f);
        float px, py;
        if (op_in_port(i, px, py)) {  // texture input
            port_dot(r, px, py, 5.f, 0.55f, 0.62f, 0.72f);
            r.draw_text(px + 10.f, py - 5.f, "in", 0.55f, 0.58f, 0.62f, 1.0f, 0.7f);
        }
        if (op_out_port(i, px, py)) port_dot(r, px, py, 5.f, 0.55f, 0.62f, 0.72f);  // output (right)
        if (!out) r.draw_text(x + w - 14.f, y + 5.f, "\xC3\x97", 0.7f, 0.45f, 0.45f, 1.0f, 0.95f);
        // thumbnail: a recessed panel with the node's live output drawn on top via
        // Renderer2D's textured-quad path (scales/pans with the view, clipped to the
        // pane by the active clip-rect, letterboxed to the source aspect).
        if (op_has_thumb(op)) {
            const int rows = std::max(1, op_input_rows_at(vg_, i));
            const float tx = x + 6.f, ty = y + 30.f + rows * 18.f + 2.f, tw = w - 12.f, th = kThumbH;
            r.draw_rect(tx - 1.f, ty - 1.f, tw + 2.f, th + 2.f, 0.07f, 0.08f, 0.10f, 1.0f);  // frame
            r.draw_rect(tx, ty, tw, th, 0.03f, 0.035f, 0.045f, 1.0f);                          // panel
            if (WGPUTextureView v = vg_->node_view(i)) {
                const float srcA = vg_->rt_aspect(), dstA = tw / th;
                float fw = tw, fh = th;
                if (srcA > dstA) fh = tw / srcA; else fw = th * srcA;   // letterbox
                r.draw_texture(tx + (tw - fw) * 0.5f, ty + (th - fh) * 0.5f, fw, fh, v);
            }
        }
    }
    // param input ports + labels (down each node's left edge)
    for (int i = 0; i < n; ++i) {
        const int pc = node_pcount(vg_, i);
        for (int l = 0; l < pc; ++l) {
            float px, py; if (!param_port(i, l, px, py)) continue;
            const char* name = node_plabel(vg_, i, l);
            const bool on = reg_.source_of(node_param_dest(vg_->nodes()[i].id, name)) != nullptr;
            port_dot(r, px, py, 4.f, on ? 0.31f : 0.34f, on ? 0.80f : 0.40f, on ? 0.75f : 0.45f);
            r.draw_text(px + 10.f, py - 5.f, name,
                        on ? 0.72f : 0.48f, on ? 0.82f : 0.5f, on ? 0.78f : 0.55f, 1.0f, 0.68f);
        }
    }

    // data nodes (matching card style)
    for (auto& nd : data_) {
        if (nd.flash > 0) { r.draw_rounded_rect(nd.x - 3.f, nd.y - 3.f, nd.w + 6.f, nd.h + 6.f, 6.f, 0.31f, 0.80f, 0.75f, 1.0f); nd.flash--; }
        r.draw_rounded_rect(nd.x, nd.y, nd.w, nd.h, 5.f, 0.12f, 0.13f, 0.155f, 1.0f);
        r.draw_rect(nd.x + 1.f, nd.y + 3.f, nd.w - 2.f, 20.f, 0.15f, 0.18f, 0.18f, 1.0f);  // header strip
        r.draw_rect(nd.x, nd.y, nd.w, 3.f, 0.31f, 0.80f, 0.75f, 1.0f);                     // accent
        r.draw_text(nd.x + 12.f, nd.y + 6.f, nd.title.c_str(), 0.90f, 0.92f, 0.95f, 1.0f, 0.92f);
        // live value history (rolling bar sparkline) in a recessed panel
        const float gx = nd.x + 12.f, gy = nd.y + 30.f, gw = nd.w - 24.f, gh = 26.f;
        r.draw_rect(gx - 1.f, gy - 1.f, gw + 2.f, gh + 2.f, 0.07f, 0.08f, 0.10f, 1.0f);  // frame
        r.draw_rect(gx, gy, gw, gh, 0.03f, 0.035f, 0.045f, 1.0f);                          // panel
        const float colw = gw / kHistN;
        for (int j = 0; j < kHistN; ++j) {
            const float v = std::clamp(nd.hist[(nd.hist_head + j) % kHistN], 0.f, 1.f);  // oldest..newest
            const float bh = v * (gh - 2.f);
            r.draw_rect(gx + colw * j, gy + gh - bh - 1.f, std::max(1.f, colw - 0.4f), bh, 0.28f, 0.74f, 0.70f, 0.95f);
        }
        // current-value readout bar under the panel
        r.draw_rect(gx, nd.y + 62.f, gw * std::clamp(nd.value, 0.f, 1.f), 4.f, 0.31f, 0.80f, 0.75f, 1.0f);
        float px, py; data_out(nd, px, py);
        port_dot(r, px, py, 5.f, 0.31f, 0.80f, 0.75f);
    }

    r.set_transform(0.f, 0.f, 1.f);   // back to identity for chrome
    draw_op_palette(r);
    r.pop_clip_rect();
}

// Floating overlays that must sit ABOVE the thumbnail blit pass — drawn by main
// in a second UI flush after thumbnails.
void NodeGraph::draw_overlays(Renderer2D& r) { draw_chooser(r); }

bool NodeGraph::on_down(double x, double y) {
    cx_ = x; cy_ = y;
    if (chooser_open_) {  // click a row to spawn it, click anywhere else to dismiss
        const float w = 264.f, rowh = 20.f, hdr = 26.f;
        const int total = int(chooser_hits_.size());
        const int vis = std::max(1, std::min(total, 9));
        const int first = chooser_sel_ >= vis ? chooser_sel_ - vis + 1 : 0;
        const float px = (bx0_ + bx1_) * 0.5f - w * 0.5f, py = by0_ + 22.f;
        if (x >= px && x < px + w && y >= py + hdr && y < py + hdr + vis * rowh) {
            const int hi = first + int((y - (py + hdr)) / rowh);
            if (hi >= 0 && hi < total) { chooser_sel_ = hi; chooser_confirm(); return true; }
        }
        chooser_hide(); return true;
    }
    sync_op_pos();
    const int n = vg_ ? int(vg_->nodes().size()) : 0;
    // Graph content lives in WORLD space; convert the cursor and keep hit radii
    // constant on screen by dividing by the zoom.
    double wx, wy; to_world(x, y, wx, wy);
    cx_ = wx; cy_ = wy;  // world cursor for drag-preview wires
    const double hr = 13.0 / view_scale_, pr = 12.0 / view_scale_;

    // disconnect an op input or a param port
    int oi = nearest_op_in(wx, wy, hr);
    if (oi >= 0) { vg_->set_input(oi, -1); return true; }
    int pni, pl;
    if (nearest_param(wx, wy, pr, pni, pl)) {
        reg_.disconnect(node_param_dest(vg_->nodes()[pni].id, node_plabel(vg_, pni, pl)));
        return true;
    }

    // start a wire from a data-node output port
    for (int i = 0; i < int(data_.size()); ++i) {
        float px, py; data_out(data_[i], px, py);
        if (std::hypot(wx - px, wy - py) < hr) { drag_mode_ = 3; wire_from_ = i; return true; }
    }
    // start a wire from an op output port
    int oo = nearest_op_out(wx, wy, hr);
    if (oo >= 0) { drag_mode_ = 4; wire_from_ = oo; return true; }

    // op-node x button / body drag
    for (int i = 0; i < n; ++i) {
        float ox, oy, ow, oh; op_node_rect(i, ox, oy, ow, oh);
        if (vg_->nodes()[i].op != VOp::Output && in_rect(ox + ow - 15.f, oy + 3.f, 12.f, 12.f, wx, wy)) {
            vg_->remove_node(i); sel_op_ = -1; sync_op_pos(); return true;
        }
        if (in_rect(ox, oy, ow, oh, wx, wy)) {
            if (vg_->nodes()[i].op == VOp::Output) vg_->set_active_output(i);  // clicking selects the viewer source
            sel_op_ = i;  // select for the inspector (dock)
            drag_mode_ = 2; drag_idx_ = i; dx_ = wx - ox; dy_ = wy - oy; return true;
        }
    }
    // palette -> add an op (chrome: screen coords)
    int pj = palette_hit(x, y);
    if (pj >= 0 && vg_) { vg_->add_node(kPalette[pj]); sync_op_pos(); return true; }

    // data-node body drag
    for (int i = 0; i < int(data_.size()); ++i)
        if (in_rect(data_[i].x, data_[i].y, data_[i].w, data_[i].h, wx, wy)) {
            drag_mode_ = 1; drag_idx_ = i; dx_ = wx - data_[i].x; dy_ = wy - data_[i].y; return true;
        }
    // empty canvas within the network pane -> pan the view (screen coords)
    if (x >= bx0_ && x < bx1_ && y >= by0_ && y < by1_) {
        drag_mode_ = 5; pan_last_x_ = float(x); pan_last_y_ = float(y); return true;
    }
    return false;
}

void NodeGraph::on_move(double x, double y) {
    double wx, wy; to_world(x, y, wx, wy);
    cx_ = wx; cy_ = wy;  // world cursor (drag-preview wires draw under the transform)
    if (drag_mode_ == 1 && drag_idx_ >= 0 && drag_idx_ < int(data_.size())) {
        data_[drag_idx_].x = float(wx - dx_); data_[drag_idx_].y = float(wy - dy_);
    } else if (drag_mode_ == 2 && drag_idx_ >= 0 && drag_idx_ < int(op_pos_.size())) {
        op_pos_[drag_idx_] = { float(wx - dx_), float(wy - dy_) };
    } else if (drag_mode_ == 5) {  // pan: move the view offset (screen-space delta)
        view_ox_ += float(x) - pan_last_x_; view_oy_ += float(y) - pan_last_y_;
        pan_last_x_ = float(x); pan_last_y_ = float(y);
    }
}

void NodeGraph::zoom_at(double sx, double sy, float factor) {
    double wx, wy; to_world(sx, sy, wx, wy);              // world point under the cursor
    view_scale_ = std::clamp(view_scale_ * factor, 0.35f, 3.0f);
    view_ox_ = float(sx) - float(wx) * view_scale_;       // keep that point under the cursor
    view_oy_ = float(sy) - float(wy) * view_scale_;
}

void NodeGraph::on_up(double x, double y) {
    double wx, wy; to_world(x, y, wx, wy);
    if (drag_mode_ == 3 && wire_from_ >= 0 && wire_from_ < int(data_.size()) && vg_) {
        int pni, pl;
        if (nearest_param(wx, wy, 18.0 / view_scale_, pni, pl))
            reg_.connect(source_id_for(data_[wire_from_].char_id),
                         node_param_dest(vg_->nodes()[pni].id, node_plabel(vg_, pni, pl)));
    } else if (drag_mode_ == 4 && wire_from_ >= 0) {
        int target = nearest_op_in(wx, wy, 18.0 / view_scale_);
        if (target >= 0 && vg_) vg_->set_input(target, wire_from_);
    }
    drag_mode_ = 0; drag_idx_ = -1; wire_from_ = -1;
}

// ---- Tab chooser ----
void NodeGraph::chooser_build_catalog() {
    chooser_catalog_.clear();
    if (vg_ && vg_->registry())                       // spawnable ops, straight from the registry
        for (const auto& nm : vg_->registry()->type_names())
            chooser_catalog_.push_back({ nm, true, nm, -1, 0 });
    for (const auto& s : kSources)                     // audio data sources
        chooser_catalog_.push_back({ s.label, false, std::string(), s.char_id, 1 });
}
void NodeGraph::chooser_rebuild() {
    chooser_hits_.clear();
    const std::string f = lower_str(chooser_filter_);
    for (int i = 0; i < int(chooser_catalog_.size()); ++i)
        if (f.empty() || lower_str(chooser_catalog_[i].label).find(f) != std::string::npos)
            chooser_hits_.push_back(i);
    chooser_sel_ = std::clamp(chooser_sel_, 0, std::max(0, int(chooser_hits_.size()) - 1));
}
void NodeGraph::chooser_show(double sx, double sy) {
    chooser_open_ = true; chooser_filter_.clear(); chooser_sel_ = 0;
    chooser_sx_ = float(sx); chooser_sy_ = float(sy);
    chooser_build_catalog();
    chooser_rebuild();
}
void NodeGraph::chooser_move(int dir) {
    if (chooser_hits_.empty()) return;
    const int n = int(chooser_hits_.size());
    chooser_sel_ = (chooser_sel_ + dir % n + n) % n;
}
void NodeGraph::chooser_backspace() {
    if (!chooser_filter_.empty()) { chooser_filter_.pop_back(); chooser_rebuild(); }
}
void NodeGraph::chooser_char(unsigned int c) {
    if (c >= 32 && c < 127) { chooser_filter_.push_back(char(c)); chooser_rebuild(); }
}
void NodeGraph::chooser_confirm() {
    if (chooser_sel_ < 0 || chooser_sel_ >= int(chooser_hits_.size())) { chooser_hide(); return; }
    const ChooserEntry& e = chooser_catalog_[chooser_hits_[chooser_sel_]];
    if (e.is_op && vg_) {
        const int ni = vg_->add_node(e.op_type);   // spawn by name (registry)
        sync_op_pos();  // grow op_pos_ for the new node, then place it at the cursor
        if (ni >= 0 && ni < int(op_pos_.size())) op_pos_[ni] = { chooser_sx_ - kCardW * 0.5f, chooser_sy_ - 15.f };
    } else if (!e.is_op) {
        add_data_node(e.label, e.char_id);
        if (!data_.empty()) { data_.back().x = chooser_sx_ - 84.f; data_.back().y = chooser_sy_ - 36.f; }
    }
    chooser_hide();
}

// Centred drop-down panel; layout shared with the click hit-test in on_down().
void NodeGraph::draw_chooser(Renderer2D& r) {
    if (!chooser_open_) return;
    const float w = 264.f, rowh = 20.f, hdr = 26.f;
    const int total = int(chooser_hits_.size());
    const int vis = std::max(1, std::min(total, 9));
    const int first = chooser_sel_ >= vis ? chooser_sel_ - vis + 1 : 0;
    const float px = (bx0_ + bx1_) * 0.5f - w * 0.5f, py = by0_ + 22.f;
    const float h = hdr + vis * rowh + 6.f;
    r.draw_rounded_rect(px, py, w, h, 5.f, 0.11f, 0.12f, 0.145f, 0.98f);
    r.draw_rect(px, py, w, 2.f, 0.35f, 0.62f, 0.95f, 1.0f);  // accent bar
    const bool empty = chooser_filter_.empty();
    const std::string f = empty ? std::string("type to filter\xE2\x80\xA6") : (chooser_filter_ + "_");
    r.draw_text(px + 10.f, py + 7.f, f.c_str(), empty ? 0.45f : 0.9f, empty ? 0.47f : 0.92f, empty ? 0.5f : 0.95f, 1.0f, 0.92f);
    if (total == 0) { r.draw_text(px + 10.f, py + hdr + 4.f, "no match", 0.55f, 0.45f, 0.45f, 1.0f, 0.86f); return; }
    for (int vi = 0; vi < vis; ++vi) {
        const int hi = first + vi; if (hi >= total) break;
        const ChooserEntry& e = chooser_catalog_[chooser_hits_[hi]];
        const float iy = py + hdr + vi * rowh;
        if (hi == chooser_sel_) r.draw_rect(px + 2.f, iy, w - 4.f, rowh, 0.20f, 0.28f, 0.40f, 0.9f);
        const float dr = e.env == 0 ? 0.55f : 0.35f, dg = e.env == 0 ? 0.55f : 0.78f, db = e.env == 0 ? 0.95f : 0.55f;
        r.draw_rounded_rect(px + 12.f, iy + rowh * 0.5f - 3.f, 6.f, 6.f, 3.f, dr, dg, db, 1.0f);
        r.draw_text(px + 26.f, iy + 4.f, e.label.c_str(), 0.88f, 0.90f, 0.93f, 1.0f, 0.9f);
        r.draw_text(px + w - 42.f, iy + 4.f, e.is_op ? "op" : "src", 0.45f, 0.48f, 0.52f, 1.0f, 0.7f);
    }
}

}  // namespace vivid::ui
