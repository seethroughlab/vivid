#include "ui/node_graph.h"
#include "app/edit_gateway.h"   // ADR-0017: capture UI graph edits
#include "ui/node_canvas.h"   // shared card/wire/port/grid + NodeView
#include "ui/ui_style.h"   // design tokens (region colours, borders, type ramp)
#include "ui/chooser_rank.h"   // ADR-0046: demote recipe ops below primitives
#include "gpu/visual_graph.h"    // VisualNode / nodes()
#include "gpu/loaded_operator.h" // UI-4b: reach an op's dylib editor via dynamic_cast
#include <cmath>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace vivid::ui {


// ---- data-source identity + uniform routing ----
// Kinds 0-4 are audio analysis (master + track); 5-7 are per-track NOTE sources (the char_id stride
// is 8, so they slot into the reserved gap). Master has no notes, so only tracks ever use 5-7.
// ADR-0028: the LIVE publisher no longer uses char_id — every source is emitted as a string id via
// bridge_source.h. This decoder survives ONLY as a load-time persistence shim for saved sessions +
// catalog entries that still carry the packed integer (add_data_node(int) / add_node_raw(int)).
static const char* kKindName[8] = { "level", "transient", "low", "mid", "high", "note", "velocity", "gate" };
static std::string source_id_for(int char_id) {  // master=kind, track t = 100+t*8+kind
    if (char_id < 100) return std::string("master.") + (char_id >= 0 && char_id < 5 ? kKindName[char_id] : "level");
    const int t = (char_id - 100) / 8, kind = (char_id - 100) % 8;
    return "track_" + std::to_string(t) + "." + (kind >= 0 && kind < 8 ? kKindName[kind] : "level");
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
static const char* uniform_owner(int u) { return u <= 3 ? "Plasma" : (u == 4 ? "Feedback" : "Blur"); }
static int  uniform_local(int u) { return u <= 3 ? u : 0; }
// Texture-port counts come from the operator DESCRIPTOR (inst.*_port_count), not a
// hardcoded op table — so package ops and 2-input ops (Composite/Displace) expose the
// right number of independently-wireable ports.
static int op_in_count(const vivid::VisualGraph* vg, int i) {
    return (vg && i >= 0 && i < int(vg->nodes().size())) ? vg->nodes()[i].inst.input_port_count : 0;
}
// The p-th INPUT port descriptor (nullptr if out of range).
static const VividPortDescriptor* op_in_desc(const vivid::VisualGraph* vg, int i, int p) {
    if (!vg || i < 0 || i >= int(vg->nodes().size())) return nullptr;
    int in = 0;
    for (const auto& pd : vg->nodes()[i].inst.ports) {
        if (pd.direction != VIVID_PORT_INPUT) continue;
        if (in == p) return &pd;
        ++in;
    }
    return nullptr;
}
// The declared NAME of the p-th INPUT port (e.g. "scene", "pos_x", "scale_y"), or nullptr — so the card
// labels ports by what they DO instead of a meaningless "A"/"B" (InstancesFromLanes has 11 lane inputs).
static const char* op_in_name(const vivid::VisualGraph* vg, int i, int p) {
    const VividPortDescriptor* pd = op_in_desc(vg, i, p);
    return pd ? pd->name : nullptr;
}
// Curation for INPUT ports (parallels exposed_params): a proliferating optional lane input (MANY
// multiplicity — InstancesFromLanes' 11 pos/scale/colour lanes) shows ONLY when it's wired; primary
// single inputs (a texture, scene/instances) always show so every node stays wireable. Keeps a
// lane-heavy node from drowning in empty stubs, the same way params collapse to the curated subset.
static bool op_in_exposed(const vivid::VisualGraph* vg, int i, int p) {
    const VividPortDescriptor* pd = op_in_desc(vg, i, p);
    if (!pd) return false;
    if (pd->multiplicity != VIVID_MULTIPLICITY_MANY) return true;   // primary inputs always visible
    return vg->nodes()[i].in(p) >= 0;                               // optional lane: only if wired
}
static int op_in_exposed_count(const vivid::VisualGraph* vg, int i) {
    int n = 0;
    for (int p = 0, np = op_in_count(vg, i); p < np; ++p) if (op_in_exposed(vg, i, p)) ++n;
    return n;
}
static bool op_has_out(const vivid::VisualGraph* vg, int i) {
    return vg && i >= 0 && i < int(vg->nodes().size()) && vg->nodes()[i].inst.output_port_count > 0;
}
static int op_out_count(const vivid::VisualGraph* vg, int i) {
    return (vg && i >= 0 && i < int(vg->nodes().size())) ? vg->nodes()[i].inst.output_port_count : 0;
}
// The declared NAME of the p-th OUTPUT port (e.g. "color_r"), or nullptr — labels a multi-output node's
// stubs (a LanePalette emitting r/g/b) so you can see which output each wire leaves from.
static const char* op_out_name(const vivid::VisualGraph* vg, int i, int p) {
    if (!vg || i < 0 || i >= int(vg->nodes().size())) return nullptr;
    int o = 0;
    for (const auto& pd : vg->nodes()[i].inst.ports) {
        if (pd.direction != VIVID_PORT_OUTPUT) continue;
        if (o == p) return pd.name;
        ++o;
    }
    return nullptr;
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
// The operator ParamBase behind a node param (its type/range/choices/display hint).
static const vivid::ParamBase* node_pb(const vivid::VisualGraph* vg, int i, int local) {
    if (!vg || i < 0 || i >= int(vg->nodes().size())) return nullptr;
    const auto& pp = vg->nodes()[i].inst.param_ptrs;
    return (local >= 0 && local < int(pp.size())) ? pp[local] : nullptr;
}

NodeGraph::NodeGraph() {
    data_.push_back({ 560.f, 540.f, 168.f, 72.f, "Output \xC2\xB7 Level", "master.level", 0.f, 0, {}, 0 });
    // out-of-box reactivity is seeded in set_visual_graph (needs the default chain's ids).
}

void NodeGraph::set_visual_graph(vivid::VisualGraph* vg) {
    vg_ = vg;
    if (vg_ && reg_.mappings().empty()) {  // seed glow <- master level on the first Plasma
        const int ni = first_node_of("Plasma");
        if (ni >= 0) reg_.connect("master.level", node_param_dest(vg_->nodes()[ni].id, "glow"));
    }
}

int NodeGraph::find_source_node(const std::string& src) const {
    for (int i = 0; i < int(data_.size()); ++i)
        if (data_[i].source == src) return i;
    return -1;
}
bool NodeGraph::source_consumed(const std::string& prefix) const {
    for (const auto& m : reg_.mappings()) if (m.source.rfind(prefix, 0) == 0) return true;   // wired to a param
    for (const auto& d : data_)           if (d.source.rfind(prefix, 0) == 0) return true;   // spawned as a data node
    return false;
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

// Auto-arrange the op nodes into a tidy layered left->right layout: each node is
// ranked by the longest path along its input edge (a generator = rank 0, the Output
// sink = the deepest), columns are spaced horizontally, and within a column nodes are
// ordered + stacked by their input's vertical position (a light barycenter pass). Data
// (audio-source) nodes are parked in a left gutter. Mirrors vivid-classic's Sugiyama
// layout, simplified for this single-input-per-node graph.
void NodeGraph::layout_nodes() {
    if (!vg_) return;
    sync_op_pos();
    const int n = int(vg_->nodes().size());
    if (n == 0) return;

    // 1. Longest-path rank via bounded relaxation (cycle-safe: at most n passes).
    std::vector<int> rank(n, 0);
    for (int pass = 0; pass < n; ++pass) {
        bool changed = false;
        for (int i = 0; i < n; ++i)
            for (int in : vg_->nodes()[i].inputs)   // rank past the deepest of ALL input ports
                if (in >= 0 && in < n && in != i && rank[in] + 1 > rank[i]) { rank[i] = rank[in] + 1; changed = true; }
        if (!changed) break;
    }
    int maxRank = 0;
    for (int i = 0; i < n; ++i) maxRank = std::max(maxRank, rank[i]);

    // 2. Group nodes into columns by rank.
    std::vector<std::vector<int>> cols(maxRank + 1);
    for (int i = 0; i < n; ++i) cols[rank[i]].push_back(i);

    const float left = bx0_ + 200.f, colSp = 200.f, gap = 16.f;   // room for data nodes on the left
    const float mid  = (by0_ + by1_) * 0.5f;

    // 3. Coordinate assignment: place each node's CENTER at the barycenter of its inputs, so a chain
    //    runs in a straight horizontal line (nodes align with the thing feeding them), then resolve
    //    overlaps within a column by pushing the lower node down. Earlier this RE-CENTERED every
    //    column on `mid` independently, which left cards ragged (a tall node's top sat higher) and
    //    pushed a column of tall source nodes off BOTH the top and the bottom of the view.
    std::vector<float> cy(n, mid);   // assigned CENTER y (a node's inputs have lower rank => placed first)
    std::vector<float> ch(n, 0.f);   // card height, cached
    for (int i = 0; i < n; ++i) { float x, y, w, h; op_node_rect(i, x, y, w, h); ch[i] = h; }
    for (int c = 0; c <= maxRank; ++c) {
        auto& col = cols[c];
        auto bary = [&](int node) {   // barycenter over all connected input ports (mid for a source)
            float sum = 0.f; int cnt = 0;
            for (int in : vg_->nodes()[node].inputs) if (in >= 0 && in < n) { sum += cy[in]; ++cnt; }
            return cnt ? sum / cnt : mid;
        };
        std::stable_sort(col.begin(), col.end(), [&](int a, int b) { return bary(a) < bary(b); });
        float cursor = -1e9f;                         // bottom edge of the previously placed card
        for (int node : col) {
            const float half = ch[node] * 0.5f;
            float center = bary(node);
            if (center - half < cursor + gap) center = cursor + gap + half;   // don't overlap the card above
            cy[node] = center;
            cursor = center + half;
        }
    }
    // 3b. Normalize vertically so the topmost card sits just below the region top — never above it.
    float min_top = 1e9f;
    for (int i = 0; i < n; ++i) min_top = std::min(min_top, cy[i] - ch[i] * 0.5f);
    const float shift = (by0_ + 24.f) - min_top;
    for (int c = 0; c <= maxRank; ++c)
        for (int node : cols[c])
            op_pos_[node] = { left + c * colSp, cy[node] - ch[node] * 0.5f + shift };

    // 4. Park data (audio-source) nodes in a left gutter column.
    float dy = by0_ + 30.f;
    for (auto& d : data_) { d.x = bx0_ + 8.f; d.y = dy; dy += d.h + 12.f; }
}
// Total left-edge input rows: one per texture input port + one per param.
static constexpr float kCardW  = 156.f;
static constexpr float kThumbH = 46.f;                 // live-output thumbnail strip
// Every node but the sink renders an image, so every node but the sink gets a thumbnail.
static bool op_has_thumb(const vivid::VisualGraph* vg, int i) {
    return vg && i >= 0 && i < int(vg->nodes().size()) && !vg->nodes()[i].is_output();
}

// The visuals card port-row layout, in the shared CardPorts vocabulary (ADR-0023): a 30px header,
// 18px rows (texture inputs then params), and a fixed 46px thumbnail tail (none on the sink). One
// source of truth for the card height + the ports' row centres, shared with the audio graph.
CardPorts NodeGraph::card_ports(int i) const {
    CardPorts cp;
    cp.header_h = 30.f; cp.row_h = 18.f;
    const bool thumb = op_has_thumb(vg_, i);
    // Size the thumbnail panel to the OUTPUT aspect (full card width / aspect), so the preview fills it
    // exactly — no letterbox gaps, no crop. Clamped so an extreme aspect can't make a giant/tiny strip.
    const float tw = kCardW - 12.f;
    cp.tail_h   = thumb ? std::clamp(tw / std::max(0.5f, vg_ ? vg_->rt_aspect() : 1.78f), 44.f, 92.f) : 0.f;
    cp.tail_pad = thumb ? 8.f : 6.f;
    const int nout = op_out_count(vg_, i);   // reserve rows for multi-output stubs on the right, too
    cp.lead_rows = std::max(op_in_exposed_count(vg_, i), nout > 1 ? nout : 0);
    cp.params    = int(exposed_params(i).size());   // curated subset (pinned ∪ wired), not every param
    return cp;
}

void NodeGraph::op_node_rect(int i, float& x, float& y, float& w, float& h) const {
    x = (i >= 0 && i < int(op_pos_.size())) ? op_pos_[i].first : bx0_;
    y = (i >= 0 && i < int(op_pos_.size())) ? op_pos_[i].second : by0_;
    w = kCardW;
    h = card_ports(i).height();
}


// classic-style accent (r,g,b) for a node, from what the node IS — the sink, an op that
// makes an image, or an op that transforms one. No enum, so a shader file or any other
// new op gets the right colour without being named anywhere.
static void op_accent(const vivid::VisualNode& n, float& r, float& g, float& b) {
    if (n.is_output())      { r = 0.93f; g = 0.78f; b = 0.38f; }   // the sink: amber
    else if (n.is_source()) { r = 0.35f; g = 0.55f; b = 0.95f; }   // makes an image: blue
    else                    { r = 0.60f; g = 0.45f; b = 0.85f; }   // transforms one: violet
}
bool NodeGraph::op_in_port(int i, int port, float& px, float& py) const {  // input `port`: left edge, one row per EXPOSED port
    if (!vg_ || i < 0 || i >= int(vg_->nodes().size()) || port < 0 || port >= op_in_count(vg_, i)) return false;
    if (!op_in_exposed(vg_, i, port)) return false;                 // hidden (unwired optional lane): no row/stub
    int slot = 0; for (int q = 0; q < port; ++q) if (op_in_exposed(vg_, i, q)) ++slot;   // row = position among exposed
    float x, y, w, h; op_node_rect(i, x, y, w, h); px = x; py = card_ports(i).row_cy(y, slot); return true;
}
bool NodeGraph::op_out_port(int i, int port, float& px, float& py) const {
    const int no = op_out_count(vg_, i);
    if (port < 0 || port >= no) return false;
    float x, y, w, h; op_node_rect(i, x, y, w, h);
    px = x + w;
    py = (no <= 1) ? (y + h * 0.5f)                  // single output: right-centre (unchanged)
                   : card_ports(i).row_cy(y, port);  // multi-output: one stub per output row on the right
    return true;
}
bool NodeGraph::op_out_port(int i, float& px, float& py) const { return op_out_port(i, 0, px, py); }
int NodeGraph::first_node_of(const std::string& op_type) const {
    if (!vg_) return -1;
    for (int i = 0; i < int(vg_->nodes().size()); ++i) if (vg_->nodes()[i].op_type == op_type) return i;
    return -1;
}
bool NodeGraph::param_port(int node_idx, int local, float& px, float& py) const {  // left edge, per node
    if (!vg_ || node_idx < 0 || node_idx >= int(vg_->nodes().size())) return false;
    if (local < 0 || local >= node_pcount(vg_, node_idx)) return false;
    // `local` is the REAL param index; a param has a row only if it is exposed (pinned ∪ wired). Its ROW
    // SLOT is its position within the exposed list, so callers can keep iterating 0..pcount and hidden
    // params simply return false (skip). Draw + hit-test share this single mapping.
    const std::vector<int> ex = exposed_params(node_idx);
    int slot = -1;
    for (int s = 0; s < int(ex.size()); ++s) if (ex[s] == local) { slot = s; break; }
    if (slot < 0) return false;
    float x, y, w, h; op_node_rect(node_idx, x, y, w, h);
    const CardPorts cp = card_ports(node_idx);
    px = x; py = cp.row_cy(y, cp.param_row(slot));   // param rows follow the texture-input rows
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
int NodeGraph::nearest_op_in(double x, double y, double maxd, int& port) const {
    int best = -1; double bd = maxd; port = 0;
    if (vg_) for (int i = 0; i < int(vg_->nodes().size()); ++i) {
        for (int p = 0, np = op_in_count(vg_, i); p < np; ++p) {
            float px, py; if (!op_in_port(i, p, px, py)) continue;
            double d = std::hypot(x - px, y - py); if (d < bd) { bd = d; best = i; port = p; }
        }
    }
    return best;
}
// Set texture input `port` of `node` to source node `src` (-1 clears). Ports beyond
// the current 2-edge model (port>=2) are ignored until N-input support lands (Phase 5).
void NodeGraph::set_op_input_port(int node, int port, int src) {
    if (vg_) vg_->set_input(node, port, src);   // any port (N-input)
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
    bx0_ = x0; by0_ = y0; bx1_ = x1; by1_ = y1; bounds_init_ = true;
    // Bounds only record the node-layout / hit-test region — they never move nodes.
    // Nodes live in world space (pannable canvas) and the clip-rect contains the drawing,
    // so resizing the splitter changes the visible pane without dragging the graph content.
    sync_op_pos();
}

void NodeGraph::set_frame(float x0, float y0, float x1, float y1) {
    fx0_ = x0; fy0_ = y0; fx1_ = x1; fy1_ = y1;   // the full visuals-column rect (grid + clip reach the edges)
}

void NodeGraph::set_source_by_id(const std::string& source, float v) {
    for (auto& n : data_) if (n.source == source) {
        n.value = v;
        n.hist[n.hist_head] = v;                       // push into the rolling history
        n.hist_head = (n.hist_head + 1) % kHistN;
    }
    reg_.set_source(source, v);
}
// ADR-0028: intern `source` to a stable publish handle (build the string once). Idempotent — the same
// id always maps to the same handle. Cold path (called on the first frame a source appears).
int NodeGraph::source_handle(const std::string& source) {
    auto it = handle_by_id_.find(source);
    if (it != handle_by_id_.end()) return it->second;
    const int h = static_cast<int>(pubs_.size());
    pubs_.push_back({ reg_.intern_source(source), -1, data_gen_ - 1, source });  // stale gen -> resolve on 1st publish
    handle_by_id_.emplace(source, h);
    return h;
}
// Hot path: write a source's value by handle. Updates the registry cell directly (no string hash) and
// the matching data node's sparkline (index cached, re-resolved only when the data-node set changed).
void NodeGraph::publish(int handle, float v) {
    if (handle < 0 || handle >= static_cast<int>(pubs_.size())) return;
    Pub& p = pubs_[handle];
    *p.cell = v;
    if (p.data_gen != data_gen_) { p.data_idx = find_source_node(p.id); p.data_gen = data_gen_; }
    if (p.data_idx >= 0) {
        DataNode& n = data_[p.data_idx];
        n.value = v;
        n.hist[n.hist_head] = v;
        n.hist_head = (n.hist_head + 1) % kHistN;
    }
}
// Consumption gate by handle: does any mapping/data-node reference this (interned) source's id as a
// prefix? Lets the frame publisher gate per-node FFT capture without rebuilding the prefix string.
bool NodeGraph::consumed(int handle) const {
    if (handle < 0 || handle >= static_cast<int>(pubs_.size())) return false;
    return source_consumed(pubs_[handle].id);
}
void NodeGraph::apply_params() {
    if (!vg_) return;
    auto& nodes = vg_->nodes();
    for (auto& n : nodes) {
        const int pc = int(n.inst.param_ptrs.size());  // operator-declared params
        const int had = int(n.base.size());
        n.params.resize(pc, 0.f);
        n.base.resize(pc, 0.f);
        for (int l = had; l < pc; ++l) n.base[l] = n.inst.param_ptrs[l]->default_value;   // seed new slots
        for (int l = 0; l < pc; ++l) {
            const vivid::ParamBase* pb = n.inst.param_ptrs[l];
            // Resolve against the param's DECLARED range, not a hard-coded [0,1]. Modulation is a
            // normalized 0..1 signal, so it scales by the range — for a 0..1 param that is exactly
            // the old behavior, but an int/enum param (an aspect preset, an octave count) is no
            // longer silently pinned to 1.
            const float lo = pb->min_value, hi = pb->max_value;
            const float mod = reg_.dest_value(node_param_dest(n.id, pb->name));
            n.params[l] = std::clamp(n.base[l] + mod * (hi - lo), lo, hi);   // manual base + live modulation
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
void NodeGraph::add_data_node(const std::string& title, const std::string& source) {
    float y = by0_ + 150.f + data_.size() * 84.f;
    if (y > by1_ - 72.f) y = by1_ - 72.f;
    data_.push_back({ bx0_ + 20.f, y, 168.f, 72.f, title, source, 0.f, 90, {}, 0 });
    ++data_gen_;   // ADR-0028: invalidate cached publish->data-node indices (a source may now have a node)
    note_edit_("Add Data Node");   // covers both the Tab chooser and the inspector menu
}
void NodeGraph::add_data_node(const std::string& title, int char_id) { add_data_node(title, source_id_for(char_id)); }
void NodeGraph::get_node(int i, float& x, float& y, std::string& source, std::string& title) const {
    if (i < 0 || i >= int(data_.size())) return;
    x = data_[i].x; y = data_[i].y; source = data_[i].source; title = data_[i].title;
}
void NodeGraph::reset_nodes() { data_.clear(); reg_.clear_mappings(); ++data_gen_; }   // ADR-0028: drop cached indices

int NodeGraph::op_count() const { return vg_ ? int(vg_->nodes().size()) : 0; }
// UX Ph4 F3: keyboard delete — mirrors the mouse ×-button path (node_graph.cpp on_down) but keeps a
// selection on a neighbour so the keyboard flow can continue. Output is never removable.
bool NodeGraph::delete_op(int i) {
    if (!vg_ || i < 0 || i >= int(vg_->nodes().size()) || vg_->nodes()[i].is_output()) return false;
    vg_->remove_node(i);
    sync_op_pos();
    const int n = int(vg_->nodes().size());
    sel_op_ = (n > 0) ? std::min(i, n - 1) : -1;   // stay in the visual graph on a neighbour (or deselect)
    note_edit_("Delete Node");
    return true;
}
int NodeGraph::op_input_port_count(int i) const { return vg_ ? op_in_count(vg_, i) : 0; }
void NodeGraph::get_op(int i, int& input, int& id, float& x, float& y) const {
    if (!vg_ || i < 0 || i >= int(vg_->nodes().size())) return;
    input = vg_->nodes()[i].in(0);
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
        // Clamp to the param's DECLARED range (both the inspector and the control server's
        // set_node_param land here). A hard [0,1] used to pin every int/enum param to 1.
        const vivid::ParamBase* pb = node_pb(vg_, i, local);
        base[local] = pb ? std::clamp(v, pb->min_value, pb->max_value) : std::clamp(v, 0.f, 1.f);
    }
}
int NodeGraph::op_param_type_at(int i, int local) const {
    const vivid::ParamBase* pb = node_pb(vg_, i, local);
    return pb ? static_cast<int>(pb->type) : VIVID_PARAM_FLOAT;
}
int NodeGraph::op_param_hint_at(int i, int local) const {
    const vivid::ParamBase* pb = node_pb(vg_, i, local);
    return pb ? static_cast<int>(pb->display_hint) : VIVID_DISPLAY_DEFAULT;
}
float NodeGraph::op_param_min_at(int i, int local) const {
    const vivid::ParamBase* pb = node_pb(vg_, i, local);
    return pb ? pb->min_value : 0.f;
}
float NodeGraph::op_param_max_at(int i, int local) const {
    const vivid::ParamBase* pb = node_pb(vg_, i, local);
    return pb ? pb->max_value : 1.f;
}
int NodeGraph::op_param_choice_count_at(int i, int local) const {
    const vivid::ParamBase* pb = node_pb(vg_, i, local);
    return pb ? static_cast<int>(pb->choice_count) : 0;
}
const char* NodeGraph::op_param_choice_label_at(int i, int local, int choice) const {
    const vivid::ParamBase* pb = node_pb(vg_, i, local);
    if (!pb || !pb->choice_labels || choice < 0 || choice >= int(pb->choice_count)) return "";
    return pb->choice_labels[choice] ? pb->choice_labels[choice] : "";
}
float NodeGraph::op_param_value_at(int i, int local) const {
    return (op_node_valid(vg_, i) && local >= 0 && local < int(vg_->nodes()[i].params.size())) ? vg_->nodes()[i].params[local] : 0.f;
}
const char* NodeGraph::op_file_param_at(int i, int local) const {
    if (!op_node_valid(vg_, i) || local < 0 || local >= int(vg_->nodes()[i].file_params.size())) return "";
    return vg_->nodes()[i].file_params[local].c_str();
}
void NodeGraph::set_op_file_param_at(int i, int local, const std::string& v) {
    if (!op_node_valid(vg_, i) || local < 0) return;
    auto& fp = vg_->nodes()[i].file_params;
    if (local >= int(fp.size())) fp.resize(local + 1);
    fp[local] = v;
}
// UI-4b: an op's custom editor is reachable only if its instance is a loaded dylib (LoadedOperator)
// that exported the editor ABI. Built-in ops never have one.
static vivid::LoadedOperator* node_loaded_op(const vivid::VisualGraph* vg, int i) {
    if (!vg || i < 0 || i >= int(vg->nodes().size())) return nullptr;
    return dynamic_cast<vivid::LoadedOperator*>(vg->nodes()[i].inst.op.get());
}
bool NodeGraph::op_has_editor(int i) const {
    const vivid::LoadedOperator* lo = node_loaded_op(vg_, i);
    return lo && lo->has_editor();
}
VividEditorMetadata NodeGraph::op_editor_metadata(int i) const {
    const vivid::LoadedOperator* lo = node_loaded_op(vg_, i);
    return lo ? lo->editor_metadata() : VividEditorMetadata{};
}
void NodeGraph::op_draw_editor(int i, VividEditorContext* ctx) const {
    if (vivid::LoadedOperator* lo = node_loaded_op(vg_, i)) lo->draw_editor(ctx);
}
bool NodeGraph::op_param_wired_at(int i, int local) const {
    if (!op_node_valid(vg_, i)) return false;
    return reg_.source_of(node_param_dest(vg_->nodes()[i].id, node_plabel(vg_, i, local))) != nullptr;
}

// Curated body params: the param indices shown as rows on node i, in index order. A param appears iff it
// is pinned OR wired — a connection is always visible so its wire has an endpoint (see the param-wire draw
// pass). Empty for a fresh/uncurated node => the card is collapsed. One source of truth for card_ports()
// (row count) and param_port() (row slot), so draw + hit-test can't drift.
std::vector<int> NodeGraph::exposed_params(int i) const {
    std::vector<int> out;
    const int pc = node_pcount(vg_, i);
    for (int l = 0; l < pc; ++l)
        if (is_param_pinned(i, l) || op_param_wired_at(i, l)) out.push_back(l);
    return out;
}
bool NodeGraph::is_param_pinned(int i, int local) const {
    if (!op_node_valid(vg_, i)) return false;
    const auto& v = vg_->nodes()[i].pinned_params;
    return std::find(v.begin(), v.end(), local) != v.end();
}
void NodeGraph::pin_param(int i, int local) {
    if (!op_node_valid(vg_, i) || local < 0 || local >= node_pcount(vg_, i)) return;
    auto& v = vg_->nodes()[i].pinned_params;
    if (std::find(v.begin(), v.end(), local) == v.end()) v.push_back(local);   // idempotent, add order
}
void NodeGraph::unpin_param(int i, int local) {
    if (!op_node_valid(vg_, i)) return;
    auto& v = vg_->nodes()[i].pinned_params;
    v.erase(std::remove(v.begin(), v.end(), local), v.end());
}
void NodeGraph::toggle_param_pin(int i, int local) {
    if (is_param_pinned(i, local)) unpin_param(i, local); else pin_param(i, local);
}
// Gesture A affordance: a small chevron box at the LEFT of the header opens the curate menu. Kept inside
// the header (30px tall) and clear of the param/input port dots, which live on the ROWS below it.
int NodeGraph::param_curate_hit(double sx, double sy) const {
    if (!vg_) return -1;
    double wx, wy; to_world(sx, sy, wx, wy);
    for (int i = 0; i < int(vg_->nodes().size()); ++i) {
        if (vg_->nodes()[i].is_output()) continue;          // the sink has no params to curate
        if (node_pcount(vg_, i) <= 0) continue;
        float x, y, w, h; op_node_rect(i, x, y, w, h);
        if (hit({ x, y, 18.f, 26.f }, wx, wy)) return i;    // left ~18px of the header row
    }
    return -1;
}
bool NodeGraph::take_param_menu_request(int& node_idx, int& src_data_node, double& sx, double& sy) {
    if (pmreq_node_ < 0) return false;
    node_idx = pmreq_node_; src_data_node = pmreq_src_; sx = pmreq_sx_; sy = pmreq_sy_;
    pmreq_node_ = pmreq_src_ = -1;
    return true;
}
bool NodeGraph::connect_data_to_param(int data_idx, int op_idx, int local) {
    if (!vg_ || data_idx < 0 || data_idx >= int(data_.size())) return false;
    if (!op_node_valid(vg_, op_idx) || local < 0 || local >= node_pcount(vg_, op_idx)) return false;
    reg_.connect(data_[data_idx].source,
                 node_param_dest(vg_->nodes()[op_idx].id, node_plabel(vg_, op_idx, local)));
    note_edit_("Connect Mapping");
    return true;
}

void NodeGraph::chain_load_begin() { if (vg_) vg_->clear_nodes(); op_pos_.clear(); op_pos_init_ = true; sel_op_ = -1; }
void NodeGraph::chain_load_add(const std::string& op_type, int id, float x, float y) {
    if (!vg_) return;
    vg_->load_node(op_type, id);
    op_pos_.push_back({ x, y });
}
void NodeGraph::chain_load_set_input(int i, int input) { if (vg_) vg_->set_input(i, input); }
void NodeGraph::chain_load_set_input_b(int i, int input) { if (vg_) vg_->set_input_b(i, input); }
int  NodeGraph::op_input_b_at(int i) const {
    return op_node_valid(vg_, i) ? vg_->nodes()[i].in(1) : -1;
}
// N-input persistence: the full edge vector (trailing -1 trimmed) and a per-port setter.
std::vector<int> NodeGraph::op_inputs_at(int i) const {
    if (!op_node_valid(vg_, i)) return {};
    std::vector<int> e = vg_->nodes()[i].inputs;
    while (!e.empty() && e.back() < 0) e.pop_back();
    return e;
}
void NodeGraph::set_op_input_at(int i, int port, int src, int src_port) { if (vg_) vg_->set_input(i, port, src, src_port); }
// Source OUTPUT ports parallel to op_inputs_at (trailing 0s trimmed) — persist only when a multi-output
// producer feeds a specific lane; an all-zero list (every legacy edge) serializes to nothing.
std::vector<int> NodeGraph::op_in_src_ports_at(int i) const {
    if (!op_node_valid(vg_, i)) return {};
    std::vector<int> e = vg_->nodes()[i].in_ports;
    while (!e.empty() && e.back() == 0) e.pop_back();
    return e;
}
std::string NodeGraph::op_asset_at(int i) const {
    return op_node_valid(vg_, i) ? vg_->nodes()[i].asset : std::string();
}
void NodeGraph::set_op_asset_at(int i, const std::string& asset) {
    if (op_node_valid(vg_, i)) vg_->nodes()[i].asset = asset;
}
bool NodeGraph::op_missing_at(int i) const {
    return op_node_valid(vg_, i) && vg_->nodes()[i].op_missing();
}
std::string NodeGraph::op_orphan(int i) const {
    return op_node_valid(vg_, i) ? vg_->nodes()[i].orphan_params : std::string();
}
void NodeGraph::set_op_orphan(int i, const std::string& json) {
    if (op_node_valid(vg_, i)) vg_->nodes()[i].orphan_params = json;
}
int NodeGraph::op_at_world(double wx, double wy) const {
    if (!vg_) return -1;
    for (int i = 0; i < int(vg_->nodes().size()); ++i) {
        float x, y, w, h; op_node_rect(i, x, y, w, h);
        if (hit({ x, y, w, h }, wx, wy)) return i;
    }
    return -1;
}
int NodeGraph::op_at(double sx, double sy) const {
    double wx, wy; to_world(sx, sy, wx, wy);
    return op_at_world(wx, wy);
}
std::string NodeGraph::op_source_path(int i) const {
    const std::string asset = op_asset_at(i);   // only CustomShader-style nodes carry an editable asset today
    if (asset.empty()) return {};
    std::filesystem::path ap(asset);
    if (ap.is_relative()) {
        const std::string dir = vg_ ? vg_->asset_dir() : std::string();
        if (dir.empty()) return {};              // project-relative but no project dir -> unresolvable
        ap = std::filesystem::path(dir) / ap;
    }
    return ap.string();
}
bool NodeGraph::swap_op_type(int i, const std::string& type) {
    if (!vg_) return false;
    const bool ok = vg_->set_node_op_type(i, type);   // keeps id + input edge + position
    if (ok) { sel_op_ = i; note_edit_("Swap Operator"); }
    return ok;
}
void NodeGraph::add_node_raw(const std::string& title, const std::string& source, float x, float y) {
    data_.push_back({ x, y, 168.f, 72.f, title, source, 0.f, 0, {}, 0 });
    ++data_gen_;   // ADR-0028: invalidate cached publish->data-node indices
}
// Legacy load path: a saved session that stored the packed integer char_id (pre string-source migration).
void NodeGraph::add_node_raw(const std::string& title, int char_id, float x, float y) {
    add_node_raw(title, source_id_for(char_id), x, y);
}

void NodeGraph::data_out(const DataNode& n, float& px, float& py) { px = n.x + n.w; py = n.y + n.h * 0.5f; }

// ADR-0023 Layer 1: enumerate the operator nodes as the shared card-chrome shape. This is exactly the
// per-node data draw()'s op-card loop feeds to canvas_.card() (rect/accent/selection/health/title/error);
// factoring it here gives the contract a real consumer (draw() below reads it back) and lets the audio
// peer answer the same questions. The bridge data-nodes stay a visuals overlay (drawn separately).
void NodeGraph::collect_nodes(std::vector<AdapterNode>& out) const {
    out.clear();
    const int n = vg_ ? int(vg_->nodes().size()) : 0;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const vivid::VisualNode& node = vg_->nodes()[i];
        AdapterNode a;
        a.id = node.id;
        op_node_rect(i, a.rect.x, a.rect.y, a.rect.w, a.rect.h);
        float ar, ag, ab; op_accent(node, ar, ag, ab);
        a.accent[0] = ar; a.accent[1] = ag; a.accent[2] = ab;
        a.selected = (i == sel_op_);
        // ADR-0016/0020: a node's live error is its own compile error, else the registry's last
        // hot-reload error for its op type (a compiled op keeps its old dylib running).
        std::string err = node.error();
        if (err.empty() && vg_->registry())
            err = vg_->registry()->reload_error(node.op_type);
        a.broken = !err.empty();
        a.title = node.op_type;
        a.error = std::move(err);
        out.push_back(std::move(a));
    }
}

int NodeGraph::selected_node_id() const {
    const int n = vg_ ? int(vg_->nodes().size()) : 0;
    return (sel_op_ >= 0 && sel_op_ < n) ? vg_->nodes()[sel_op_].id : -1;
}

// ADR-0023 #3c: the active-output ring, drawn UNDER the card by the shared canvas card loop — a 2px
// accent frame on the node currently driving the viewer (unless it's the inspector selection, which
// already gets the blue ring from card()).
void NodeGraph::before_card(Renderer2D& r, const AdapterNode& a, int i) const {
    if (!vg_) return;
    const bool active_out = vg_->nodes()[i].is_output() && i == vg_->output_index();
    if (i != sel_op_ && active_out)
        r.draw_rect(a.rect.x - 2.f, a.rect.y - 2.f, a.rect.w + 4.f, a.rect.h + 4.f,
                    a.accent[0], a.accent[1], a.accent[2], 1.0f);
}

// ADR-0023 #3c: the visuals-domain overlay drawn OVER each card by the shared canvas card loop — the
// type label, the "output / -> viewer" tag, the texture-input port stubs, the output port, the ×, and
// the live thumbnail (with any error note drawn over it).
void NodeGraph::after_card(Renderer2D& r, const AdapterNode& a, int i) const {
    if (!vg_) return;
    const Style& sty = style();
    const vivid::VisualNode& node = vg_->nodes()[i];
    const float x = a.rect.x, y = a.rect.y, w = a.rect.w, h = a.rect.h;
    const bool out = node.is_output();
    const bool active_out = out && i == vg_->output_index();
    const std::string& node_err = a.error;
    // ADR-0023 LOD: fade small text out as the camera zooms out (it would be illegible noise). Cards,
    // accents, ports and thumbnails stay; the label text fades — port labels first (small fs), then the
    // title (larger fs). Below the fade floor the text is skipped entirely (draw nothing, not invisibly).
    // Gesture A affordance: a disclosure chevron at the header-left on curatable nodes (has params, not
    // the sink). Clicking its zone (param_curate_hit) opens the show/hide-params menu. The title shifts
    // right to clear it.
    const bool curatable = !out && node_pcount(vg_, i) > 0;
    const float chev_w = curatable ? 12.f : 0.f;
    const float label_x = x + 10.f + chev_w + (node_err.empty() ? 0.f : node_error_label_shift);
    if (curatable) if (const float ta = canvas_.text_alpha(sty.fs_body); ta > 0.01f)
        r.draw_text(x + 6.f, y + 6.f, "\xE2\x80\xBA", 0.55f, 0.6f, 0.68f, ta, sty.fs_body);   // ›
    if (const float ta = canvas_.text_alpha(sty.fs_body); ta > 0.01f)
        r.draw_text(label_x, y + 6.f, a.title.c_str(), sty.text[0], sty.text[1], sty.text[2], ta, sty.fs_body);
    if (const float ta = canvas_.text_alpha(0.72f); out && ta > 0.01f)
        r.draw_text(x + w - 56.f, y + 6.f, active_out ? "\xE2\x86\x92 viewer" : "output",
                    active_out ? 0.7f : 0.45f, active_out ? 0.6f : 0.47f, active_out ? 0.4f : 0.5f, ta, 0.72f);
    const float a_port = canvas_.text_alpha(0.7f);   // port-stub labels (the port's declared name)
    float px, py;
    for (int p = 0, np = op_in_count(vg_, i); p < np; ++p) {  // one stub per input port
        if (!op_in_port(i, p, px, py)) continue;
        node_port(r, px, py, 5.f, 0.55f, 0.62f, 0.72f);
        // Label by the port's declared name (scene / pos_x / scale_y / …); fall back to "in" for a lone
        // unnamed input. Beats the old meaningless "A"/"B" — a multi-lane op now reads what each port is.
        const char* nm = op_in_name(vg_, i, p);
        const char* lbl = (nm && nm[0]) ? nm : "in";
        if (a_port > 0.01f) r.draw_text(px + 10.f, py - 5.f, lbl, 0.55f, 0.58f, 0.62f, a_port, 0.7f);
    }
    // output stub(s) on the right — one per output port, labelled when there's more than one (so a
    // multi-lane producer like LanePalette shows a separate r/g/b stub instead of three wires stacked).
    for (int op2 = 0, no = op_out_count(vg_, i); op2 < no; ++op2) {
        if (!op_out_port(i, op2, px, py)) continue;
        node_port(r, px, py, 5.f, 0.55f, 0.62f, 0.72f);
        if (no > 1) if (const char* nm = op_out_name(vg_, i, op2); nm && nm[0] && a_port > 0.01f)
            r.draw_text(px - 10.f - r.text_width(nm, 0.7f), py - 5.f, nm, 0.55f, 0.58f, 0.62f, a_port, 0.7f);
    }
    if (const float ta = canvas_.text_alpha(0.95f); !out && ta > 0.01f)
        r.draw_text(x + w - 14.f, y + 5.f, "\xC3\x97", 0.7f, 0.45f, 0.45f, ta, 0.95f);
    // thumbnail: the node's live output, drawn to FILL its panel (the panel is sized to the source
    // aspect below, so the fill is exact) — part of the card, not a letterboxed rectangle floating in it.
    if (op_has_thumb(vg_, i)) {
        // Position the thumbnail from the SAME CardPorts layout the card is drawn with (which reflects the
        // collapsed/curated param count via exposed_params) — not the full param count, or it floats below
        // a collapsed card. The tail (thumbnail) sits right after the header + head + all reserved rows.
        const CardPorts cp = card_ports(i);
        const float tx = x + 6.f;
        const float ty = y + cp.header_h + cp.head_h + cp.rows_reserved() * cp.row_h + 2.f;
        const float tw = w - 12.f, th = cp.tail_h;
        node_preview_panel(r, tx, ty, tw, th);   // shared recessed well (same as the audio-node preview)
        if (WGPUTextureView v = vg_->node_view(i))
            r.draw_texture(tx, ty, tw, th, v);    // panel is sized to the source aspect (see card_ports), so
                                                  // filling it exactly = no letterbox, no distortion, no crop
        // The error goes OVER the thumbnail (the node is still rendering — the last-good pipeline —
        // so the picture alone would say nothing is wrong).
        if (!node_err.empty()) node_error_note(r, tx, ty, tw, th, node_err);
    }
}


void NodeGraph::draw(Renderer2D& r) {
    const Style& sty = style();
    sync_op_pos();
    r.push_clip_rect(fx0_, fy0_, fx1_ - fx0_, fy1_ - fy0_);   // clip to the full visuals column
    // (The region is labelled by the SIGNAL panel header; no in-graph title needed.)
    // Everything below is graph content: drawn in WORLD space through the view
    // transform (pan + zoom). Chrome (palette) resets the transform first.
    const NodeView& view = canvas_.view();   // the camera lives in the canvas (ADR-0023 #1)
    r.set_transform(view.ox, view.oy, view.scale);
    // grid: cover the full visuals column (shared substrate), edge to edge (ADR-0023 Layer 2)
    canvas_.set_region({ fx0_, fy0_, fx1_ - fx0_, fy1_ - fy0_ });
    canvas_.grid(r);

    const int n = vg_ ? int(vg_->nodes().size()) : 0;
    // chain wires (op output -> op input); one per connected texture input port.
    for (int i = 0; i < n; ++i) {
        const auto& ins = vg_->nodes()[i].inputs;
        for (int p = 0; p < int(ins.size()); ++p) {
            const int src = ins[p];
            const int sp  = vg_->nodes()[i].in_src_port(p);   // WHICH output of the source (multi-lane)
            float ox, oy, ix, iy;
            if (src >= 0 && src < n && op_out_port(src, sp, ox, oy) && op_in_port(i, p, ix, iy))
                node_wire(r, ox, oy, ix, iy, 0.50f, 0.60f, 0.68f);  // classic grayish-blue
        }
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
            node_wire(r, ox, oy, px, py, 0.45f, 0.78f, 0.85f);
        }
    }
    // drag preview (ADR-0023 Layer 2: the ghost wire comes from the canvas)
    if (drag_mode_ == 3 && wire_from_ >= 0 && wire_from_ < n + int(data_.size())) {
        float ox, oy; if (wire_from_ < int(data_.size())) { data_out(data_[wire_from_], ox, oy);
            const float c[3] = { 0.55f, 0.85f, 0.80f }; canvas_.ghost_wire(r, ox, oy, float(cx_), float(cy_), c); }
    }
    if (drag_mode_ == 4 && wire_from_ >= 0) {
        float ox, oy; if (op_out_port(wire_from_, ox, oy)) {
            const float c[3] = { 0.5f, 0.65f, 0.9f }; canvas_.ghost_wire(r, ox, oy, float(cx_), float(cy_), c); }
    }

    // op-nodes: the shared canvas card loop (ADR-0023 #3c) draws each card's chrome and calls
    // before_card (the active-output ring, under the card) + after_card (label, ports, ×, thumbnail)
    // for the visuals overlay. The param-input ports + bridge data-nodes are separate passes below.
    if (sel_op_ >= n) sel_op_ = -1;   // drop a stale selection (removed/reloaded) BEFORE the snapshot
    canvas_.draw_cards(r, *this, *this);
    // param input ports + labels (down each node's left edge). ADR-0023 LOD: the port dots stay; the
    // param-name text fades out when the camera is zoomed out too far to read it.
    const float a_param = canvas_.text_alpha(0.68f);
    for (int i = 0; i < n; ++i) {
        const int pc = node_pcount(vg_, i);
        for (int l = 0; l < pc; ++l) {
            float px, py; if (!param_port(i, l, px, py)) continue;
            const char* name = node_plabel(vg_, i, l);
            const bool on = reg_.source_of(node_param_dest(vg_->nodes()[i].id, name)) != nullptr;
            node_port(r, px, py, 4.f, on ? 0.31f : 0.34f, on ? 0.80f : 0.40f, on ? 0.75f : 0.45f);
            if (a_param > 0.01f)
                r.draw_text(px + 10.f, py - 5.f, name,
                            on ? 0.72f : 0.48f, on ? 0.82f : 0.5f, on ? 0.78f : 0.55f, a_param, 0.68f);
        }
    }

    // data nodes (matching card style)
    for (auto& nd : data_) {
        if (nd.flash > 0) { r.draw_rect(nd.x - 3.f, nd.y - 3.f, nd.w + 6.f, nd.h + 6.f, 0.31f, 0.80f, 0.75f, 1.0f); nd.flash--; }
        canvas_.card(r, { nd.x, nd.y, nd.w, nd.h }, sty.teal, false, false);   // data source (teal, never broken)
        r.draw_text(nd.x + 12.f, nd.y + 6.f, nd.title.c_str(), sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_body);
        // live value history (rolling bar sparkline) in a recessed panel
        const float gx = nd.x + 12.f, gy = nd.y + 30.f, gw = nd.w - 24.f, gh = 26.f;
        node_preview_panel(r, gx, gy, gw, gh);   // shared recessed well
        const float colw = gw / kHistN;
        for (int j = 0; j < kHistN; ++j) {
            const float v = std::clamp(nd.hist[(nd.hist_head + j) % kHistN], 0.f, 1.f);  // oldest..newest
            const float bh = v * (gh - 2.f);
            r.draw_rect(gx + colw * j, gy + gh - bh - 1.f, std::max(1.f, colw - 0.4f), bh, 0.28f, 0.74f, 0.70f, 0.95f);
        }
        // current-value readout bar under the panel
        r.draw_rect(gx, nd.y + 62.f, gw * std::clamp(nd.value, 0.f, 1.f), 4.f, 0.31f, 0.80f, 0.75f, 1.0f);
        float px, py; data_out(nd, px, py);
        node_port(r, px, py, 5.f, 0.31f, 0.80f, 0.75f);
    }

    r.set_transform(0.f, 0.f, 1.f);   // back to identity for chrome
    r.pop_clip_rect();
}

// Floating overlays that must sit ABOVE the thumbnail blit pass — drawn by main
// in a second UI flush after thumbnails.
// ---- Tab chooser (ADR-0014: the ONE way to add a node) ----
// The widget is shared with the audio graph (ui/chooser.{h,cpp}); this only builds the catalog and
// spawns whatever comes back.

void NodeGraph::chooser_show(double sx, double sy) {
    std::vector<Chooser::Entry> entries;
    if (vg_ && vg_->registry()) {                     // spawnable ops, straight from the registry
        for (const auto& nm : vg_->registry()->type_names()) {
            Chooser::Entry e;
            e.label = nm;
            e.id = nm;
            // A shader row says SHADER: same catalog, same spawn, but it is a FILE you can open,
            // edit and fork — the one distinction worth drawing (ADR-0016).
            e.badge = (shaders_ && shaders_->is_shader(nm)) ? "shader" : "op";
            e.spawn = { Domain::Visual, SpawnKind::VisualOp, nm };   // ADR-0023 step 5
            e.accent = style().gpu;                   // both are visual ops: one zone, one accent
            if (const auto* d = vg_->registry()->descriptor_for(nm)) {   // v3+ metadata (both optional)
                if (d->summary) e.summary = d->summary;
                for (uint32_t k = 0; k < d->keyword_count; ++k)
                    if (d->keywords && d->keywords[k]) { e.hay += d->keywords[k]; e.hay += ' '; }
                e.role = d->role;   // ADR-0046: role chip + recipe demotion
            }
            entries.push_back(std::move(e));
        }
    }
    for (const auto& nm : quarantined_) {             // ADR-0018: repeat crashers — greyed, unspawnable, with a reason
        Chooser::Entry e;
        e.label = nm; e.id = nm; e.badge = "op"; e.accent = style().gpu;
        e.spawn = { Domain::Visual, SpawnKind::VisualOp, nm };   // (disabled below; kind set for consistency)
        e.enabled = false;
        e.disabled_note = "quarantined: repeat crashes \xC2\xB7 clear its crash history to re-enable";
        entries.push_back(std::move(e));
    }
    for (const auto& s : kSources) {                  // audio data sources (the bridge)
        Chooser::Entry e;
        e.label = s.label;
        e.summary = "audio characteristic";
        e.badge = "src";
        e.spawn = { Domain::Bridge, SpawnKind::BridgeNode, "", 0, s.char_id };   // ADR-0023 step 5
        e.accent = style().audio;
        entries.push_back(std::move(e));
    }
    for (const auto& [label, source] : bridge_catalog_) {   // per-track/note/fft/node sources (string ids)
        Chooser::Entry e;
        e.label = label;
        e.summary = "audio source \xC2\xB7 " + source;
        e.badge = "src";
        e.spawn = { Domain::Bridge, SpawnKind::BridgeNode, "", 0, 0, source };
        e.accent = style().audio;
        entries.push_back(std::move(e));
    }
    // ADR-0046: composable primitives first — sink bundled RECIPE ops (Instancer/Emitter/Solids) to
    // the bottom, preserving order otherwise. Each entry carries its own role now (bridge/data rows
    // stay DEFAULT), so the partition reads it directly.
    demote_recipes(entries, [](const Chooser::Entry& e) { return e.role; });
    chooser_.set_entries(std::move(entries));
    chooser_.show(sx, sy, bx0_, by0_, bx1_, by1_);
}

void NodeGraph::note_edit_(const char* label, const char* key) {
    if (edit_gateway_) edit_gateway_->note_edit(label, key ? key : "");
}

void NodeGraph::chooser_spawn(const Chooser::Entry& e) {
    if (e.spawn.kind == SpawnKind::VisualOp && vg_) {     // an operator
        const int ni = vg_->add_node(e.spawn.type);
        sync_op_pos();                                // grow op_pos_ for the new node...
        if (ni >= 0 && ni < int(op_pos_.size()))      // ...then place it where the chooser was opened
            op_pos_[ni] = { chooser_.spawn_x() - kCardW * 0.5f, chooser_.spawn_y() - 15.f };
        note_edit_("Add Node");
    } else if (e.spawn.kind == SpawnKind::BridgeNode) {   // a bridge data node (add_data_node notes the edit)
        add_data_node(e.label, e.spawn.source.empty() ? source_id_for(e.spawn.char_id) : e.spawn.source);
        if (!data_.empty()) { data_.back().x = chooser_.spawn_x() - 84.f; data_.back().y = chooser_.spawn_y() - 36.f; }
    }
}

void NodeGraph::chooser_confirm() {
    if (const Chooser::Entry* e = chooser_.confirm()) chooser_spawn(*e);
}

int NodeGraph::drop_spawn(const std::string& op_type, double sx, double sy,
                          const std::string& file_param, const std::string& file_value) {
    if (!vg_) return -1;
    const int ni = vg_->add_node(op_type);
    if (ni < 0) return -1;
    sync_op_pos();                                // grow op_pos_ for the new node...
    if (ni < int(op_pos_.size()))                 // ...then place it where the file was dropped
        op_pos_[ni] = { static_cast<float>(sx) - kCardW * 0.5f, static_cast<float>(sy) - 15.f };
    // Set the target FILE param by name; if none was named, use the node's first FILE param.
    int local = -1;
    for (int l = 0; l < op_param_count_at(ni); ++l) {
        const char* lbl = op_param_label_at(ni, l);
        if (!file_param.empty()) { if (lbl && file_param == lbl) { local = l; break; } }
        else if (op_param_type_at(ni, l) == VIVID_PARAM_FILE) { local = l; break; }
    }
    if (local >= 0) set_op_file_param_at(ni, local, file_value);
    return ni;
}

void NodeGraph::draw_overlays(Renderer2D& r) { chooser_.draw(r); }

bool NodeGraph::on_down(double x, double y) {
    cx_ = x; cy_ = y;
    if (chooser_.open()) {   // click a row to spawn it, click anywhere else to dismiss
        bool dismissed = false;
        if (const Chooser::Entry* e = chooser_.click(x, y, dismissed)) chooser_spawn(*e);
        return true;   // the chooser owns the click either way
    }
    sync_op_pos();
    const int n = vg_ ? int(vg_->nodes().size()) : 0;
    // Graph content lives in WORLD space; convert the cursor and keep hit radii
    // constant on screen by dividing by the zoom.
    double wx, wy; to_world(x, y, wx, wy);
    cx_ = wx; cy_ = wy;  // world cursor for drag-preview wires
    const double hr = 13.0 / canvas_.view().scale, pr = 12.0 / canvas_.view().scale;

    // disconnect an op input or a param port
    int oiPort = 0; int oi = nearest_op_in(wx, wy, hr, oiPort);
    if (oi >= 0) { set_op_input_port(oi, oiPort, -1); note_edit_("Disconnect"); return true; }
    int pni, pl;
    if (nearest_param(wx, wy, pr, pni, pl)) {
        reg_.disconnect(node_param_dest(vg_->nodes()[pni].id, node_plabel(vg_, pni, pl)));
        note_edit_("Disconnect Mapping");
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
        if (!vg_->nodes()[i].is_output() && hit({ ox + ow - 15.f, oy + 3.f, 12.f, 12.f }, wx, wy)) {
            vg_->remove_node(i); sel_op_ = -1; sync_op_pos(); note_edit_("Delete Node"); return true;
        }
        if (hit({ ox, oy, ow, oh }, wx, wy)) {
            if (vg_->nodes()[i].is_output()) { vg_->set_active_output(i); note_edit_("Set Output"); }  // clicking selects the viewer source
            sel_op_ = i;  // select for the inspector (dock)
            drag_mode_ = 2; drag_idx_ = i; dx_ = wx - ox; dy_ = wy - oy; return true;
        }
    }
    // (Adding an op is Tab-only now — the registry-driven chooser, spawned at the cursor. The old
    // hard-coded 4-item "ADD OP" strip couldn't even reach the newer ops. Re-layout moved to the
    // visuals column's corner chrome, handled in app/input.cpp.)

    // data-node body drag
    for (int i = 0; i < int(data_.size()); ++i)
        if (hit({ data_[i].x, data_[i].y, data_[i].w, data_[i].h }, wx, wy)) {
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
        note_edit_("Move Node", "move-node");
    } else if (drag_mode_ == 2 && drag_idx_ >= 0 && drag_idx_ < int(op_pos_.size())) {
        op_pos_[drag_idx_] = { float(wx - dx_), float(wy - dy_) };
        note_edit_("Move Node", "move-node");
    } else if (drag_mode_ == 5) {  // pan: move the view offset (screen-space delta)
        canvas_.pan(float(x) - pan_last_x_, float(y) - pan_last_y_);   // ADR-0023 #3d: shared camera gesture
        pan_last_x_ = float(x); pan_last_y_ = float(y);
    }
}

void NodeGraph::zoom_at(double sx, double sy, float factor) {
    canvas_.zoom_at(sx, sy, factor);   // ADR-0023 #3d: shared zoom-around-cursor (clamp on the canvas)
}

void NodeGraph::on_up(double x, double y) {
    double wx, wy; to_world(x, y, wx, wy);
    if (drag_mode_ == 3 && wire_from_ >= 0 && wire_from_ < int(data_.size()) && vg_) {
        int pni, pl;
        if (nearest_param(wx, wy, 18.0 / canvas_.view().scale, pni, pl)) {
            connect_data_to_param(wire_from_, pni, pl);
        } else {
            // Gesture B: no VISIBLE param row under the drop — but if it landed on a node body, park a
            // request so the app opens the reveal+connect menu (reach a hidden/collapsed param).
            const int tgt = op_at_world(wx, wy);
            if (tgt >= 0 && !vg_->nodes()[tgt].is_output() && node_pcount(vg_, tgt) > 0) {
                pmreq_node_ = tgt; pmreq_src_ = wire_from_; pmreq_sx_ = x; pmreq_sy_ = y;
            }
        }
    } else if (drag_mode_ == 4 && wire_from_ >= 0) {
        int tport = 0; int target = nearest_op_in(wx, wy, 18.0 / canvas_.view().scale, tport);
        if (target >= 0 && vg_) { set_op_input_port(target, tport, wire_from_); note_edit_("Connect"); }
    }
    drag_mode_ = 0; drag_idx_ = -1; wire_from_ = -1;
}

}  // namespace vivid::ui
