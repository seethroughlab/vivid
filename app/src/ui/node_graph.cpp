#include "ui/node_graph.h"
#include "app/edit_gateway.h"   // ADR-0017: capture UI graph edits
#include "ui/node_canvas.h"   // shared card/wire/port/grid + NodeView
#include "ui/ui_style.h"   // design tokens (region colours, borders, type ramp)
#include "ui/chooser_rank.h"   // ADR-0046: demote recipe ops below primitives
#include "gpu/visual_graph.h"    // VisualNode / nodes()
#include "gpu/loaded_operator.h" // UI-4b: reach an op's dylib editor via dynamic_cast
#include "cli/image_analysis_tools.h"  // ADR-0050: load_image (PNG -> RGBA8) for chooser previews
#include "platform/platform.h"         // executable_path() -> Resources/reference
#include "app/bridge_source.h"         // ADR-0053 A2: master/track/transport source-id grammar
#include "operator_api/reactive_signals.h"  // ADR-0053 B4/B5: canonical reactive signal grammar (migration parser)
#include <cmath>
#include <algorithm>
#include <cctype>
#include <filesystem>
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

// ADR-0053 A2: source-card layout — a header plus one thin row per named output (with its own port).
namespace { constexpr float kSrcHeaderH = 24.f; constexpr float kSrcRowH = 18.f; constexpr float kSrcCardW = 176.f; }

NodeGraph::NodeGraph() {
    ensure_source_node("master.level");   // seed a "Master" entity node so a fresh session shows the bus
    if (!nodes_data_.empty()) { nodes_data_[0].x = 560.f; nodes_data_[0].y = 300.f; }
    // out-of-box reactivity is seeded in set_visual_graph (needs the default chain's ids).
}

NodeGraph::~NodeGraph() {
    for (auto& [slug, p] : preview_cache_) {
        if (p.view) wgpuTextureViewRelease(p.view);
        if (p.tex)  wgpuTextureRelease(p.tex);
    }
}

namespace {
// Match site/generate_reference.py::slugify (lower, [^a-z0-9]+ -> '-', trim '-') so a chooser row's op
// name maps to its bundled preview file name.
std::string preview_slug(const std::string& name) {
    std::string s; s.reserve(name.size());
    bool prev_dash = false;
    for (char c : name) {
        const char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if ((lc >= 'a' && lc <= 'z') || (lc >= '0' && lc <= '9')) { s += lc; prev_dash = false; }
        else if (!prev_dash) { s += '-'; prev_dash = true; }
    }
    while (!s.empty() && s.front() == '-') s.erase(s.begin());
    while (!s.empty() && s.back()  == '-') s.pop_back();
    return s;
}

// The bundled preview directory: Resources/reference (app bundle) or <exe_dir>/reference (non-bundle),
// mirroring the shader-library resolution (gpu/shader_library.cpp).
std::string preview_dir() {
    namespace fs = std::filesystem;
    const std::string exe = vivid::platform::executable_path();
    if (exe.empty()) return {};
    const fs::path exe_dir = fs::path(exe).parent_path();
    const fs::path bundled = (exe_dir / ".." / "Resources" / "reference").lexically_normal();
    if (fs::exists(bundled)) return bundled.string();
    return (exe_dir / "reference").lexically_normal().string();   // non-bundle fallback
}
}  // namespace

WGPUTextureView NodeGraph::preview_view(const std::string& slug) {
    if (auto it = preview_cache_.find(slug); it != preview_cache_.end())
        return it->second.view;   // may be null (cached "no preview")
    PreviewTex out{};             // cache the result either way so we never re-attempt a missing/failed load
    if (vg_ && vg_->device() && !slug.empty()) {
        namespace fs = std::filesystem;
        const std::string path = (fs::path(preview_dir()) / (slug + ".png")).string();
        std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
        if (!path.empty() && vivid::load_image(path, rgba, w, h) && w > 0 && h > 0) {
            WGPUTextureDescriptor td{};
            td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            td.dimension = WGPUTextureDimension_2D;
            td.size = { w, h, 1 };
            td.format = WGPUTextureFormat_RGBA8Unorm;
            td.mipLevelCount = 1; td.sampleCount = 1;
            if (WGPUTexture t = wgpuDeviceCreateTexture(vg_->device(), &td)) {
                WGPUTexelCopyTextureInfo dst{}; dst.texture = t; dst.mipLevel = 0; dst.origin = {0,0,0}; dst.aspect = WGPUTextureAspect_All;
                WGPUTexelCopyBufferLayout lay{}; lay.offset = 0; lay.bytesPerRow = w * 4; lay.rowsPerImage = h;
                WGPUExtent3D ext{ w, h, 1 };
                wgpuQueueWriteTexture(vg_->queue(), &dst, rgba.data(), static_cast<size_t>(w) * h * 4, &lay, &ext);
                out.tex  = t;
                out.view = wgpuTextureCreateView(t, nullptr);
            }
        }
    }
    preview_cache_[slug] = out;
    return out.view;
}

void NodeGraph::set_visual_graph(vivid::VisualGraph* vg) {
    vg_ = vg;
    if (vg_ && reg_.mappings().empty()) {  // seed glow <- master level on the first Plasma
        const int ni = first_node_of("Plasma");
        if (ni >= 0) add_mapping("master.level", node_param_dest(vg_->nodes()[ni].id, "glow"), 1.f);  // via chokepoint → visible node
    }
}

int NodeGraph::find_source_node(const std::string& src) const {
    int ni, oi; return find_source_output(src, ni, oi) ? ni : -1;
}
bool NodeGraph::find_source_output(const std::string& src, int& ni, int& oi) const {
    for (int i = 0; i < int(nodes_data_.size()); ++i)
        for (int j = 0; j < int(nodes_data_[i].outs.size()); ++j)
            if (nodes_data_[i].outs[j].source == src) { ni = i; oi = j; return true; }
    return false;
}
void NodeGraph::size_source_node(SourceNode& n) const {
    n.w = kSrcCardW;
    n.h = kSrcHeaderH + std::max<size_t>(1, n.outs.size()) * kSrcRowH + 8.f;
}
void NodeGraph::source_out_port(int ni, int oi, float& px, float& py) const {
    const SourceNode& n = nodes_data_[ni];
    px = n.x + n.w;
    py = n.y + kSrcHeaderH + (static_cast<float>(oi) + 0.5f) * kSrcRowH;
}
bool NodeGraph::source_consumed(const std::string& prefix) const {
    for (const auto& m : reg_.mappings()) if (m.source.rfind(prefix, 0) == 0) return true;   // wired to a param
    for (const auto& nd : nodes_data_)                                                        // spawned as a node
        for (const auto& o : nd.outs) if (o.source.rfind(prefix, 0) == 0) return true;
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

    // 4. Park source (audio-entity) nodes in a left gutter column.
    float dy = by0_ + 30.f;
    for (auto& d : nodes_data_) { d.x = bx0_ + 8.f; d.y = dy; dy += d.h + 12.f; }
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
// ADR-0053 Phase B: the VALUE-LANE ordinal of op i's OUTPUT port `port` (an all-outputs ordinal, as
// op_out_port uses), or -1 if that output isn't a value lane (SCALAR+MANY). A control edge reads a
// value lane, so this both gates the drag (only value-lane outputs start a control wire) and converts
// the dragged output port into the ParamControlEdge::src_lane.
int NodeGraph::op_out_value_lane(int i, int port) const {
    if (!vg_ || i < 0 || i >= int(vg_->nodes().size())) return -1;
    int lane = 0, ord = 0;
    for (const auto& pd : vg_->nodes()[i].inst.ports) {
        if (pd.direction != VIVID_PORT_OUTPUT) continue;
        const bool vlane = pd.type == VIVID_PORT_SCALAR && pd.multiplicity == VIVID_MULTIPLICITY_MANY;
        if (ord == port) return vlane ? lane : -1;
        if (vlane) ++lane;
        ++ord;
    }
    return -1;
}
// The inverse: the screen position of op i's OUTPUT port carrying value lane `lane`. Used to draw a
// control-edge wire back to its source stub. False if the node has no such value lane.
bool NodeGraph::op_out_port_of_lane(int i, int lane, float& px, float& py) const {
    if (!vg_ || i < 0 || i >= int(vg_->nodes().size())) return false;
    int vlane = 0, ord = 0;
    for (const auto& pd : vg_->nodes()[i].inst.ports) {
        if (pd.direction != VIVID_PORT_OUTPUT) continue;
        const bool v = pd.type == VIVID_PORT_SCALAR && pd.multiplicity == VIVID_MULTIPLICITY_MANY;
        if (v) { if (vlane == lane) return op_out_port(i, ord, px, py); ++vlane; }
        ++ord;
    }
    return false;
}
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
int NodeGraph::nearest_op_out(double x, double y, double maxd, int& port) const {
    int best = -1; double bd = maxd; port = 0;
    if (vg_) for (int i = 0; i < int(vg_->nodes().size()); ++i)
        for (int p = 0, no = op_out_count(vg_, i); p < no; ++p) {   // hit-test EVERY output stub (multi-output)
            float px, py; if (!op_out_port(i, p, px, py)) continue;
            double d = std::hypot(x - px, y - py); if (d < bd) { bd = d; best = i; port = p; }
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
    int ni, oi;
    if (find_source_output(source, ni, oi)) {
        SourceOutput& o = nodes_data_[ni].outs[oi];
        o.value = v;
        o.hist[o.hist_head] = v;                        // push into the rolling history
        o.hist_head = (o.hist_head + 1) % kHistN;
    }
    reg_.set_source(source, v);
}
// ADR-0028: intern `source` to a stable publish handle (build the string once). Idempotent — the same
// id always maps to the same handle. Cold path (called on the first frame a source appears).
int NodeGraph::source_handle(const std::string& source) {
    auto it = handle_by_id_.find(source);
    if (it != handle_by_id_.end()) return it->second;
    const int h = static_cast<int>(pubs_.size());
    pubs_.push_back({ reg_.intern_source(source), -1, -1, data_gen_ - 1, source });  // stale gen -> resolve on 1st publish
    handle_by_id_.emplace(source, h);
    return h;
}
// Hot path: write a source's value by handle. Updates the registry cell directly (no string hash) and
// the matching data node's sparkline (index cached, re-resolved only when the data-node set changed).
void NodeGraph::publish(int handle, float v) {
    if (handle < 0 || handle >= static_cast<int>(pubs_.size())) return;
    Pub& p = pubs_[handle];
    *p.cell = v;
    if (p.data_gen != data_gen_) {
        if (!find_source_output(p.id, p.node_idx, p.out_idx)) { p.node_idx = p.out_idx = -1; }
        p.data_gen = data_gen_;
    }
    if (p.node_idx >= 0 && p.node_idx < int(nodes_data_.size()) &&
        p.out_idx >= 0 && p.out_idx < int(nodes_data_[p.node_idx].outs.size())) {
        SourceOutput& o = nodes_data_[p.node_idx].outs[p.out_idx];
        o.value = v;
        o.hist[o.hist_head] = v;
        o.hist_head = (o.hist_head + 1) % kHistN;
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
            // ADR-0053 Phase B single-owner invariant: a live control edge OWNS this param — leave it at
            // base here (run_chain resolves the edge on top), so the registry and edges never fight over
            // the same slot while both models coexist (until the B4 cutover).
            if (n.control_edge_for(l)) { n.params[l] = std::clamp(n.base[l], lo, hi); continue; }
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
// Interactive spawn (Tab chooser / dock): ensure the entity node for `source` exists, then flash it +
// note the edit. The `title` hint is ignored — the entity title is derived (Master / Track <id>).
void NodeGraph::add_data_node(const std::string&, const std::string& source) {
    const bool existed = (find_source_node(source) >= 0);
    ensure_source_node(source);
    if (!existed) { int ni, oi; if (find_source_output(source, ni, oi)) nodes_data_[ni].flash = 90; }
    note_edit_("Add Data Node");   // covers both the Tab chooser and the inspector menu
}
void NodeGraph::add_data_node(const std::string& title, int char_id) { add_data_node(title, source_id_for(char_id)); }
// ADR-0053 Phase B4: parse a bridge audio source id -> the Reactive op that emits it + the value-lane
// ordinal of its signal, using the canonical signal grammar (operator_api/reactive_signals.h — the ONE
// table the ops emit and this parser reads). False for non-audio sources (viz.*, node_*, *.fft.*), which
// keep the registry reverse path. master.* maps to lanes [0,5); transport.* to [5,9); track_N.* to [0,8).
static int reactive_find_signal(const std::string& suf, const char* const* tbl, int lo, int hi) {
    for (int k = lo; k < hi; ++k) if (suf == tbl[k]) return k;
    return -1;
}
static bool parse_reactive_source(const std::string& src, std::string& op_type, int& track_id, int& lane) {
    using namespace vivid::reactive;
    if (src.rfind("master.", 0) == 0) {
        const int k = reactive_find_signal(src.substr(7), kMasterSignals, 0, kMasterScalarCount);
        if (k < 0) return false;   // e.g. master.fft.3 — not a migratable scalar signal
        op_type = "ReactiveMaster"; track_id = -1; lane = k; return true;
    }
    if (src.rfind("transport.", 0) == 0) {
        const int k = reactive_find_signal(src.substr(10), kMasterSignals, kMasterScalarCount, VIVID_REACTIVE_MASTER_SIGNALS);
        if (k < 0) return false;
        op_type = "ReactiveMaster"; track_id = -1; lane = k; return true;
    }
    int tid; std::string rest;
    if (vivid::parse_track_source(src, tid, rest) && rest.size() > 1 && rest[0] == '.') {
        const int k = reactive_find_signal(rest.substr(1), kTrackSignals, 0, VIVID_REACTIVE_TRACK_SIGNALS);
        if (k < 0) return false;   // e.g. track_2.fft.3
        op_type = "ReactiveTrack"; track_id = tid; lane = k; return true;
    }
    return false;
}

int NodeGraph::ensure_reactive_source_node(const std::string& op_type, int track_id) {
    if (!vg_) return -1;
    // Dedup: reuse an existing Reactive op of this type (matching the bound track_id for ReactiveTrack).
    for (int i = 0; i < int(vg_->nodes().size()); ++i) {
        const vivid::VisualNode& n = vg_->nodes()[i];
        if (n.op_type != op_type) continue;
        if (op_type != "ReactiveTrack") return n.id;
        for (int l = 0; l < node_pcount(vg_, i); ++l)
            if (std::string("track_id") == node_plabel(vg_, i, l)) {
                if (int(std::lround(op_param_base_at(i, l))) == track_id) return n.id;
                break;
            }
    }
    // Create it. A single atomic add (safe inside the load rebuild; on a huge LIVE graph this shares the
    // known incremental-add-node fragility — authoring typically happens on small graphs). Parked in a
    // left gutter column so a Reactive source doesn't stack on the visual chain.
    const int idx = vg_->add_node(op_type);
    if (idx < 0) return -1;
    if (op_type == "ReactiveTrack")
        for (int l = 0; l < node_pcount(vg_, idx); ++l)
            if (std::string("track_id") == node_plabel(vg_, idx, l)) { set_op_param_base_at(idx, l, float(track_id)); break; }
    sync_op_pos();
    if (idx >= 0 && idx < int(op_pos_.size())) {
        int rank = 0;
        for (int i = 0; i < idx; ++i)
            if (vg_->nodes()[i].op_type == "ReactiveMaster" || vg_->nodes()[i].op_type == "ReactiveTrack") ++rank;
        op_pos_[idx] = { bx0_ + 8.f, by0_ + 30.f + rank * 96.f };
    }
    return vg_->nodes()[idx].id;
}

bool NodeGraph::add_audio_control_edge(const std::string& src, const std::string& dst,
                                       float amount, float curve, bool invert, float lo, float hi,
                                       float attack, float release) {
    if (!vg_) return false;
    std::string op_type; int track_id = -1, lane = -1;
    if (!parse_reactive_source(src, op_type, track_id, lane)) return false;   // not a migratable audio source
    if (dst.rfind("node:", 0) != 0) return false;
    const size_t dot = dst.find('.', 5);
    if (dot == std::string::npos) return false;   // dst must be node:<id>.<paramName> (not node:<id>:<index>)
    const int consumer_id = std::atoi(dst.substr(5, dot - 5).c_str());
    const std::string param = dst.substr(dot + 1);
    const int cidx = op_index_of_id(consumer_id);
    if (cidx < 0) return false;
    int local = -1;
    for (int l = 0; l < node_pcount(vg_, cidx); ++l)
        if (param == node_plabel(vg_, cidx, l)) { local = l; break; }
    if (local < 0) return false;   // param gone — caller keeps the registry wire (still resolvable)
    const int src_id = ensure_reactive_source_node(op_type, track_id);
    if (src_id < 0) return false;
    vivid::VisualControlShape sh;
    sh.amount = amount; sh.curve = curve; sh.invert = invert; sh.out_lo = lo; sh.out_hi = hi;
    sh.attack = attack; sh.release = release;
    vg_->set_param_control(cidx, local, src_id, lane, sh);
    return true;
}

int NodeGraph::drop_track_sources(int id) {
    const int dropped = reg_.drop_track_sources(id);   // registry reverse-path mappings sourced from this track
    for (size_t i = 0; i < nodes_data_.size(); ++i)    // legacy Phase-A Track source card, if any
        if (nodes_data_[i].kind == SourceKind::Track && nodes_data_[i].track_id == id) {
            nodes_data_.erase(nodes_data_.begin() + i); ++data_gen_; break;
        }
    // ADR-0053 B4/B5: the track's reactivity is now a ReactiveTrack VisualGraph node bound to its stable
    // id. Remove it — VisualGraph::remove_node cascades to drop every control edge reading its lanes, so a
    // deleted track leaves no dangling edges.
    if (vg_)
        for (int i = 0; i < int(vg_->nodes().size()); ++i) {
            if (vg_->nodes()[i].op_type != "ReactiveTrack") continue;
            bool match = false;
            for (int l = 0; l < node_pcount(vg_, i); ++l)
                if (std::string("track_id") == node_plabel(vg_, i, l)) {
                    match = (int(std::lround(op_param_base_at(i, l))) == id);
                    break;
                }
            if (match) { vg_->remove_node(i); sync_op_pos(); break; }
        }
    return dropped;
}

void NodeGraph::prune_orphan_audio_source_nodes() {
    bool changed = false;
    for (size_t i = 0; i < nodes_data_.size(); ) {
        const SourceNode& n = nodes_data_[i];
        const bool audio = (n.kind == SourceKind::Master || n.kind == SourceKind::Track);
        bool referenced = false;
        if (audio)
            for (const auto& o : n.outs) {
                for (const auto& m : reg_.mappings()) if (m.source == o.source) { referenced = true; break; }
                if (referenced) break;
            }
        if (audio && !referenced) { nodes_data_.erase(nodes_data_.begin() + i); changed = true; }
        else ++i;
    }
    if (changed) ++data_gen_;   // ADR-0028: source set shrank — invalidate cached publish->output indices
}

void NodeGraph::ensure_source_node(const std::string& src) {
    if (src.empty() || find_source_node(src) >= 0) return;   // idempotent — the source already has an output

    // Which ENTITY does this source belong to? master.* + transport.* -> the one Master node; track_<id>.*
    // -> that track's node; anything else (viz.*, node_*) -> a single-output "Other" node.
    SourceKind kind = SourceKind::Other; int track_id = -1; std::string title = src;
    std::vector<std::string> defaults;   // full source ids this entity exposes by default
    int trest_idx; std::string trest;
    if (src.rfind("master.", 0) == 0 || src.rfind("transport.", 0) == 0) {
        kind = SourceKind::Master; title = "Master";
        for (int k = 0; k < 5; ++k) defaults.push_back(bridge::master_source(bridge::kTrackKindSuffixes[k]));  // level..high
        for (int k = 0; k < bridge::kNumTransportKinds; ++k) defaults.push_back(bridge::transport_source(bridge::kTransportKindSuffixes[k]));
    } else if (vivid::parse_track_source(src, trest_idx, trest)) {
        kind = SourceKind::Track; track_id = trest_idx; title = "Track " + std::to_string(track_id);
        for (int k = 0; k < bridge::kNumTrackKinds; ++k) defaults.push_back(bridge::track_source(track_id, bridge::kTrackKindSuffixes[k]));
    } else {
        defaults.push_back(src);   // Other: a single output verbatim (e.g. viz.warp)
    }

    // Reuse the entity node if it already exists (a sibling output was wired first); else create it.
    int ni = -1;
    for (int i = 0; i < int(nodes_data_.size()); ++i)
        if (nodes_data_[i].kind == kind && (kind != SourceKind::Track || nodes_data_[i].track_id == track_id) &&
            (kind != SourceKind::Other || (!nodes_data_[i].outs.empty() && nodes_data_[i].outs[0].source == src))) { ni = i; break; }
    if (ni < 0) {
        float y = by0_ + 30.f + nodes_data_.size() * 96.f;
        if (y > by1_ - 40.f) y = by1_ - 40.f;
        SourceNode n; n.kind = kind; n.track_id = track_id; n.title = title; n.x = bx0_ + 8.f; n.y = y;
        nodes_data_.push_back(std::move(n));
        ni = int(nodes_data_.size()) - 1;
    }
    SourceNode& node = nodes_data_[ni];
    // Populate default outputs (once), then guarantee `src` is present (e.g. a non-default master.fft.3).
    if (node.outs.empty())
        for (const auto& s : defaults) {
            const auto dot = s.find('.');
            node.outs.push_back({ dot == std::string::npos ? s : s.substr(dot + 1), s, 0.f, {}, 0 });
        }
    bool has = false; for (const auto& o : node.outs) if (o.source == src) { has = true; break; }
    if (!has) {
        const auto dot = src.find('.');
        node.outs.push_back({ dot == std::string::npos ? src : src.substr(dot + 1), src, 0.f, {}, 0 });
    }
    size_source_node(node);
    ++data_gen_;   // ADR-0028: invalidate cached publish->output indices (a source may now have an output)
}
void NodeGraph::get_node(int i, float& x, float& y, std::string& source, std::string& title) const {
    if (i < 0 || i >= int(nodes_data_.size())) return;
    x = nodes_data_[i].x; y = nodes_data_[i].y; title = nodes_data_[i].title;
    source = nodes_data_[i].outs.empty() ? std::string() : nodes_data_[i].outs[0].source;  // first output (A3 persists all)
}
void NodeGraph::get_source_node_meta(int i, std::string& kind, int& track_id) const {
    kind.clear(); track_id = -1;
    if (i < 0 || i >= int(nodes_data_.size())) return;
    switch (nodes_data_[i].kind) {
        case SourceKind::Master: kind = "master"; break;
        case SourceKind::Track:  kind = "track"; track_id = nodes_data_[i].track_id; break;
        default:                 kind = "other"; break;
    }
}
int NodeGraph::source_node_output_count(int i) const {
    return (i < 0 || i >= int(nodes_data_.size())) ? 0 : int(nodes_data_[i].outs.size());
}
void NodeGraph::get_source_node_output(int i, int o, std::string& suffix, std::string& source) const {
    suffix.clear(); source.clear();
    if (i < 0 || i >= int(nodes_data_.size())) return;
    const auto& outs = nodes_data_[i].outs;
    if (o < 0 || o >= int(outs.size())) return;
    suffix = outs[o].suffix; source = outs[o].source;
}
void NodeGraph::reset_nodes() {
    nodes_data_.clear(); reg_.clear_mappings(); ++data_gen_;   // ADR-0028: drop cached indices
    annos_.clear(); next_anno_id_ = 0;                         // ADR-0033 P5: notes reload from the session
}

// ADR-0033 P5: per-node label (proxies to the VisualNode's persisted `label`).
std::string NodeGraph::op_name_at(int i) const {
    return (vg_ && i >= 0 && i < int(vg_->nodes().size())) ? vg_->nodes()[i].label : std::string();
}
void NodeGraph::set_op_name_at(int i, const std::string& name) {
    if (vg_ && i >= 0 && i < int(vg_->nodes().size())) vg_->nodes()[i].label = name;
}

// ADR-0033 P5: sticky-note annotations.
int NodeGraph::anno_index_of_(int id) const {
    for (int i = 0; i < int(annos_.size()); ++i) if (annos_[i].id == id) return i;
    return -1;
}
int NodeGraph::add_annotation(float x, float y) {
    const int id = next_anno_id_++;
    annos_.push_back({ id, x, y, 180.f, 96.f, std::string() });   // default note size
    return id;
}
void NodeGraph::add_annotation_raw(int id, const std::string& text, float x, float y, float w, float h) {
    annos_.push_back({ id, x, y, w, h, text });
    if (id >= next_anno_id_) next_anno_id_ = id + 1;   // keep ids monotonic across a load
}
bool NodeGraph::remove_annotation(int id) {
    const int i = anno_index_of_(id);
    if (i < 0) return false;
    annos_.erase(annos_.begin() + i);
    return true;
}
bool NodeGraph::set_annotation_text(int id, const std::string& text) {
    const int i = anno_index_of_(id);
    if (i < 0) return false;
    annos_[i].text = text;
    return true;
}
bool NodeGraph::move_annotation(int id, float x, float y) {
    const int i = anno_index_of_(id);
    if (i < 0) return false;
    annos_[i].x = x; annos_[i].y = y;
    return true;
}
bool NodeGraph::get_annotation(int i, int& id, std::string& text, float& x, float& y, float& w, float& h) const {
    if (i < 0 || i >= int(annos_.size())) return false;
    const Annotation& a = annos_[i];
    id = a.id; text = a.text; x = a.x; y = a.y; w = a.w; h = a.h;
    return true;
}
std::string NodeGraph::annotation_text_of(int id) const {
    const int i = anno_index_of_(id);
    return (i >= 0) ? annos_[i].text : std::string();
}
int NodeGraph::annotation_at_world(double wx, double wy) const {
    // Top-most first (later notes draw on top), so a click grabs the visible one.
    for (int i = int(annos_.size()) - 1; i >= 0; --i) {
        const Annotation& a = annos_[i];
        if (wx >= a.x && wx < a.x + a.w && wy >= a.y && wy < a.y + a.h) return a.id;
    }
    return -1;
}
int NodeGraph::annotation_at_screen(double sx, double sy) const {
    double wx, wy; to_world(sx, sy, wx, wy);
    return annotation_at_world(wx, wy);
}
int NodeGraph::add_note_centered() {
    // Place the note near the viewport centre (in world space) so it lands where the user is looking.
    double wx, wy; to_world((bx0_ + bx1_) * 0.5, (by0_ + by1_) * 0.5, wx, wy);
    const int id = add_annotation(static_cast<float>(wx) - 90.f, static_cast<float>(wy) - 48.f);
    note_edit_("Add Note");
    return id;
}

int NodeGraph::op_count() const { return vg_ ? int(vg_->nodes().size()) : 0; }

// ADR-0033 P1: the single id<->index choke point (VisualGraph is index-addressed; VisualNode.id is the
// stable key). O(n) scan — fine at these node counts, and the ONLY place ids become indices so the
// selection (kept as stable ids) can drive index-based APIs safely across deletions.
int NodeGraph::op_index_of_id(int id) const {
    if (!vg_ || id < 0) return -1;
    const auto& ns = vg_->nodes();
    for (int i = 0; i < int(ns.size()); ++i) if (ns[i].id == id) return i;
    return -1;
}
// Keep sel_op_ (the inspector target index) equal to the selection's primary; -1 when empty. Called
// after any mutation of sel_ so the dock + pane-focus routing (active_graph reads selected_op()) agree.
void NodeGraph::resync_sel_op_() { sel_op_ = op_index_of_id(sel_.primary()); }

// Single-select node index i, replacing the multi-selection (the plain-click / keyboard-nav case).
void NodeGraph::select_op(int i) {
    const int n = vg_ ? int(vg_->nodes().size()) : 0;
    sel_.replace((i >= 0 && i < n) ? vg_->nodes()[i].id : -1);
    sel_op_ = (i >= 0 && i < n) ? i : -1;
}

// UX Ph4 F3: keyboard delete — mirrors the mouse ×-button path (node_graph.cpp on_down) but keeps a
// selection on a neighbour so the keyboard flow can continue. Output is never removable.
bool NodeGraph::delete_op(int i) {
    if (!vg_ || i < 0 || i >= int(vg_->nodes().size()) || vg_->nodes()[i].is_output()) return false;
    vg_->remove_node(i);
    sync_op_pos();
    const int n = int(vg_->nodes().size());
    sel_op_ = (n > 0) ? std::min(i, n - 1) : -1;   // stay in the visual graph on a neighbour (or deselect)
    sel_.replace(sel_op_ >= 0 ? vg_->nodes()[sel_op_].id : -1);   // ADR-0033 P1: keep the set in sync
    note_edit_("Delete Node");
    return true;
}

// ADR-0033 P1: remove by stable id (keyboard multi-delete). Structural only — the caller re-syncs the
// selection + notes one undo entry after the whole batch, since ids stay valid across sibling removals.
bool NodeGraph::delete_op_by_id(int id) {
    const int i = op_index_of_id(id);
    if (i < 0 || vg_->nodes()[i].is_output()) return false;
    vg_->remove_node(i);
    sync_op_pos();
    return true;
}

// ADR-0033 P2 — copy/paste/duplicate. capture_ids snapshots a set of nodes + the edges strictly
// between them + their incoming audio→param mappings, all id-free (positions/params/asset), so it can
// be spawned later with fresh ids.
GraphClip NodeGraph::capture_ids(const std::set<int>& ids) const {
    GraphClip clip;
    if (!vg_) return clip;
    // Ordered list of (graph index, stable id) for capturable nodes; Output + missing ops are skipped.
    std::vector<std::pair<int, int>> picked;              // (index, old id)
    std::unordered_map<int, int> cap_of;                  // old id -> capture index
    for (int id : ids) {
        const int i = op_index_of_id(id);
        if (i < 0 || vg_->nodes()[i].is_output() || op_missing_at(i)) continue;
        cap_of[id] = static_cast<int>(picked.size());
        picked.push_back({ i, id });
    }
    for (const auto& [i, oid] : picked) {
        NodeCapture c;
        c.op_type = op_type_at(i);
        int in = -1, id = 0; get_op(i, in, id, c.x, c.y);
        const int pc = op_param_count_at(i);
        for (int l = 0; l < pc; ++l) {
            c.base.push_back(op_param_base_at(i, l));
            const char* fv = op_file_param_at(i, l);
            c.file_params.push_back(fv ? fv : "");
            if (is_param_pinned(i, l)) c.pinned.push_back(l);
        }
        c.asset = op_asset_at(i);
        // Incoming audio→param mappings for this node. The dest is "node:<oid><sep><suffix>" where sep
        // is '.' (UI drop path, param NAME) or ':' (connect_mapping, param INDEX) — match both, and
        // require the char after the id to be a separator so "node:2" doesn't swallow "node:20".
        const std::string tag = "node:" + std::to_string(oid);
        for (const auto& m : reg_.mappings()) {
            if (m.dest.rfind(tag, 0) != 0 || m.dest.size() <= tag.size()) continue;
            const char sep = m.dest[tag.size()];
            if (sep == '.' || sep == ':') c.maps.push_back(m);
        }
        clip.nodes.push_back(std::move(c));
    }
    // Internal edges only: for each captured node, keep input edges whose source is ALSO captured.
    for (const auto& [ti, to_oid] : picked) {
        const std::vector<int> ins = op_inputs_at(ti);
        const std::vector<int> sps = op_in_src_ports_at(ti);
        for (int p = 0; p < static_cast<int>(ins.size()); ++p) {
            const int src_idx = ins[p];
            if (src_idx < 0 || src_idx >= int(vg_->nodes().size())) continue;
            const int src_id = vg_->nodes()[src_idx].id;
            auto it = cap_of.find(src_id);
            if (it == cap_of.end()) continue;               // edge to a non-copied node → dropped
            const int src_port = (p < int(sps.size())) ? sps[p] : 0;
            clip.edges.push_back({ it->second, cap_of[to_oid], p, src_port });
        }
    }
    return clip;
}

std::vector<int> NodeGraph::spawn_clip(const GraphClip& clip, float dx, float dy, const char* label) {
    std::vector<int> new_ids;
    if (!vg_ || clip.nodes.empty()) return new_ids;
    std::vector<int> new_idx(clip.nodes.size(), -1);
    // Pass 1: create each node with a fresh id + offset position, copy its authored state.
    for (int k = 0; k < static_cast<int>(clip.nodes.size()); ++k) {
        const NodeCapture& c = clip.nodes[k];
        const int ni = vg_->add_node(c.op_type);            // mints a fresh stable id
        sync_op_pos();
        if (ni >= 0 && ni < int(op_pos_.size())) op_pos_[ni] = { c.x + dx, c.y + dy };
        const int pc = op_param_count_at(ni);               // same op_type ⇒ same param layout
        for (int l = 0; l < pc && l < int(c.base.size()); ++l) set_op_param_base_at(ni, l, c.base[l]);
        for (int l = 0; l < pc && l < int(c.file_params.size()); ++l)
            if (!c.file_params[l].empty()) set_op_file_param_at(ni, l, c.file_params[l]);
        for (int l : c.pinned) if (l >= 0 && l < pc) pin_param(ni, l);
        if (!c.asset.empty()) set_op_asset_at(ni, c.asset);
        new_idx[k] = ni;
        new_ids.push_back(vg_->nodes()[ni].id);
    }
    // Pass 2: re-wire the internal edges onto the copies.
    for (const auto& e : clip.edges) {
        const int from_k = e[0], to_k = e[1], dst_port = e[2], src_port = e[3];
        if (from_k < 0 || to_k < 0 || from_k >= int(new_idx.size()) || to_k >= int(new_idx.size())) continue;
        set_op_input_at(new_idx[to_k], dst_port, new_idx[from_k], src_port);
    }
    // Pass 3: replicate each node's audio→param mappings onto its copy's fresh id, so a duplicated
    // reactive node keeps reacting. Rewrite only the dest node id; the param suffix + shaping carry over.
    for (int k = 0; k < static_cast<int>(clip.nodes.size()); ++k)
        for (const auto& m : clip.nodes[k].maps) {
            // dest = "node:<oldid><sep><suffix>" (sep '.'|':'); swap the id, keep sep+suffix verbatim.
            size_t sep = 5;                                 // 5 = strlen("node:")
            while (sep < m.dest.size() && m.dest[sep] != '.' && m.dest[sep] != ':') ++sep;
            if (sep >= m.dest.size()) continue;
            const std::string dst = "node:" + std::to_string(new_ids[k]) + m.dest.substr(sep);
            add_mapping(m.source, dst, m.amount, m.curve, m.invert, m.out_lo, m.out_hi, m.attack, m.release);
        }
    if (label && *label) note_edit_(label);
    return new_ids;
}

int NodeGraph::duplicate_selection(float dx, float dy) {
    if (sel_.empty()) return 0;
    const std::vector<int> ids = spawn_clip(capture_ids(sel_.ids()), dx, dy, "Duplicate Nodes");
    if (ids.empty()) return 0;
    sel_.clear();
    for (int id : ids) sel_.add(id);   // re-select the copies so they're ready to group-drag
    resync_sel_op_();
    return static_cast<int>(ids.size());
}
void NodeGraph::copy_selection() { if (!sel_.empty()) clipboard_ = capture_ids(sel_.ids()); }
std::vector<int> NodeGraph::paste_clipboard(float dx, float dy) {
    const std::vector<int> ids = spawn_clip(clipboard_, dx, dy, "Paste Nodes");
    if (!ids.empty()) { sel_.clear(); for (int id : ids) sel_.add(id); resync_sel_op_(); }
    return ids;
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
    if (reg_.source_of(node_param_dest(vg_->nodes()[i].id, node_plabel(vg_, i, local))) != nullptr) return true;
    // ADR-0053 Phase B: a typed control edge also drives (and therefore exposes) its param — so its row,
    // port, and wire render, and the param stays a visible drop target after the edge is made.
    return vg_->nodes()[i].control_edge_for(local) != nullptr;
}

// ADR-0053 Phase B: control-edge persistence accessors. Save exports the target param by NAME (resolved
// back to an index on load, robust to a param reorder across versions) + the source's stable id + the
// value-lane ordinal + the shape. The reactive SOURCE nodes themselves persist as ordinary chain nodes.
int NodeGraph::op_control_edge_count(int i) const {
    return op_node_valid(vg_, i) ? int(vg_->nodes()[i].control_edges.size()) : 0;
}
bool NodeGraph::get_op_control_edge(int i, int e, std::string& param, int& src_node, int& src_lane,
                                    vivid::VisualControlShape& sh) const {
    if (!op_node_valid(vg_, i)) return false;
    const auto& ces = vg_->nodes()[i].control_edges;
    if (e < 0 || e >= int(ces.size())) return false;
    const auto& ce = ces[e];
    const char* pn = (ce.param_index >= 0 && ce.param_index < node_pcount(vg_, i))
                   ? node_plabel(vg_, i, ce.param_index) : "";
    param = pn ? pn : ""; src_node = ce.src_node; src_lane = ce.src_lane; sh = ce.shape;
    return true;
}
void NodeGraph::load_op_control_edge(int i, const std::string& param, int src_node, int src_lane,
                                     const vivid::VisualControlShape& sh) {
    if (!vg_ || !op_node_valid(vg_, i) || param.empty()) return;
    int local = -1;
    for (int l = 0; l < node_pcount(vg_, i); ++l)
        if (param == node_plabel(vg_, i, l)) { local = l; break; }
    if (local < 0) return;   // the target param is gone (op type changed) — drop the edge, like a dead texture edge
    vg_->set_param_control(i, local, src_node, src_lane, sh);
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
bool NodeGraph::connect_data_to_param(int data_idx, int op_idx, int local, int out_idx) {
    if (!vg_ || data_idx < 0 || data_idx >= int(nodes_data_.size())) return false;
    const auto& outs = nodes_data_[data_idx].outs;
    if (out_idx < 0 || out_idx >= int(outs.size())) return false;
    if (!op_node_valid(vg_, op_idx) || local < 0 || local >= node_pcount(vg_, op_idx)) return false;
    add_mapping(outs[out_idx].source, node_param_dest(vg_->nodes()[op_idx].id, node_plabel(vg_, op_idx, local)), 1.f);
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
void NodeGraph::add_node_raw(const std::string&, const std::string& source, float x, float y) {
    if (source.empty()) return;
    ensure_source_node(source);   // materialize the entity node (all default outputs)
    int ni, oi; if (find_source_output(source, ni, oi)) { nodes_data_[ni].x = x; nodes_data_[ni].y = y; }
}
// Legacy load path: a saved session that stored the packed integer char_id (pre string-source migration).
void NodeGraph::add_node_raw(const std::string& title, int char_id, float x, float y) {
    add_node_raw(title, source_id_for(char_id), x, y);
}


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
        a.selected = sel_.contains(node.id) || i == sel_op_;   // ADR-0033 P1: ring every selected card
        // ADR-0016/0020: a node's live error is its own compile error, else the registry's last
        // hot-reload error for its op type (a compiled op keeps its old dylib running).
        std::string err = node.error();
        if (err.empty() && vg_->registry())
            err = vg_->registry()->reload_error(node.op_type);
        a.broken = !err.empty();
        // ADR-0033 P5: show the user rename over op_type; while THIS node is being renamed, show the
        // live edit buffer + a caret so typing is visible on the card.
        if (i == edit_node_ && edit_buf_) a.title = *edit_buf_ + "|";
        else a.title = node.label.empty() ? node.op_type : node.label;
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
    // param wires (source-node OUTPUT -> per-node param port)
    for (int i = 0; i < n; ++i) {
        const int pc = node_pcount(vg_, i);
        for (int l = 0; l < pc; ++l) {
            const std::string* src = reg_.source_of(node_param_dest(vg_->nodes()[i].id, node_plabel(vg_, i, l)));
            if (!src) continue;
            int sni, soi; if (!find_source_output(*src, sni, soi)) continue;
            float px, py; if (!param_port(i, l, px, py)) continue;
            float ox, oy; source_out_port(sni, soi, ox, oy);
            node_wire(r, ox, oy, px, py, 0.45f, 0.78f, 0.85f);
        }
    }
    // ADR-0053 Phase B: typed control-edge wires (a source op's value-lane OUTPUT -> a consumer param
    // port), drawn in a distinct WARM GOLD so they read apart from texture edges (grey-blue) and the
    // legacy registry param-wires (teal). The source is a real graph node addressed by stable id.
    for (int i = 0; i < n; ++i)
        for (const auto& ce : vg_->nodes()[i].control_edges) {
            const int si = op_index_of_id(ce.src_node);
            if (si < 0) continue;
            float ox, oy, px, py;
            if (op_out_port_of_lane(si, ce.src_lane, ox, oy) && param_port(i, ce.param_index, px, py))
                node_wire(r, ox, oy, px, py, 0.95f, 0.72f, 0.30f);
        }
    // drag preview (ADR-0023 Layer 2: the ghost wire comes from the canvas)
    if (drag_mode_ == 3 && wire_from_ >= 0 && wire_from_ < int(nodes_data_.size()) && wire_from_out_ >= 0) {
        float ox, oy; source_out_port(wire_from_, wire_from_out_, ox, oy);
        const float c[3] = { 0.55f, 0.85f, 0.80f }; canvas_.ghost_wire(r, ox, oy, float(cx_), float(cy_), c);
    }
    if (drag_mode_ == 4 && wire_from_ >= 0) {
        float ox, oy; if (op_out_port(wire_from_, wire_from_out_ >= 0 ? wire_from_out_ : 0, ox, oy)) {
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
            const bool on = reg_.source_of(node_param_dest(vg_->nodes()[i].id, name)) != nullptr
                            || vg_->nodes()[i].control_edge_for(l) != nullptr;   // ADR-0053 B2: edge lights the port too
            node_port(r, px, py, 4.f, on ? 0.31f : 0.34f, on ? 0.80f : 0.40f, on ? 0.75f : 0.45f);
            if (a_param > 0.01f)
                r.draw_text(px + 10.f, py - 5.f, name,
                            on ? 0.72f : 0.48f, on ? 0.82f : 0.5f, on ? 0.78f : 0.55f, a_param, 0.68f);
        }
    }

    // ADR-0053 A2: audio-source ENTITY nodes — a header + one row per named value output, each with a
    // compact sparkline and its own right-edge port. Wiring a row -> a param is a MappingRegistry wire.
    const float a_out = canvas_.text_alpha(0.66f);
    for (auto& nd : nodes_data_) {
        if (nd.flash > 0) { r.draw_rect(nd.x - 3.f, nd.y - 3.f, nd.w + 6.f, nd.h + 6.f, 0.31f, 0.80f, 0.75f, 1.0f); nd.flash--; }
        canvas_.card(r, { nd.x, nd.y, nd.w, nd.h }, sty.teal, false, false);   // data source (teal, never broken)
        r.draw_text(nd.x + 12.f, nd.y + 5.f, nd.title.c_str(), sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_body);
        for (int oi = 0; oi < int(nd.outs.size()); ++oi) {
            SourceOutput& o = nd.outs[oi];
            const float rowY = nd.y + kSrcHeaderH + oi * kSrcRowH;
            bool wired = false;
            for (const auto& m : reg_.mappings()) if (m.source == o.source) { wired = true; break; }
            if (a_out > 0.01f)
                r.draw_text(nd.x + 12.f, rowY + 3.f, o.suffix.c_str(),
                            wired ? 0.72f : 0.52f, wired ? 0.82f : 0.56f, wired ? 0.78f : 0.6f, a_out, sty.fs_label);
            // compact sparkline on the row's right half
            const float gx = nd.x + nd.w * 0.46f, gy = rowY + 2.f, gw = nd.w * 0.46f, gh = kSrcRowH - 5.f;
            const float colw = gw / kHistN;
            for (int j = 0; j < kHistN; ++j) {
                const float v = std::clamp(o.hist[(o.hist_head + j) % kHistN], 0.f, 1.f);  // oldest..newest
                const float bh = v * gh;
                r.draw_rect(gx + colw * j, gy + gh - bh, std::max(1.f, colw - 0.3f), bh, 0.28f, 0.74f, 0.70f, 0.9f);
            }
            float px, py; source_out_port(int(&nd - &nodes_data_[0]), oi, px, py);
            node_port(r, px, py, 4.f, 0.31f, 0.80f, 0.75f);
        }
    }

    // ADR-0033 P1: the marquee rubber-band, drawn in world space so it tracks the cards under zoom/pan.
    if (drag_mode_ == 6)
        node_marquee(r, { float(marq_x0_), float(marq_y0_),
                          float(marq_x1_ - marq_x0_), float(marq_y1_ - marq_y0_) });
    // ADR-0033 P5: sticky notes — drawn last so they float on top of the graph. The note being typed
    // shows the live edit buffer + a caret; others show their committed text (word-wrapped).
    for (const auto& a : annos_) {
        const bool editing = (a.id == edit_anno_);
        node_sticky(r, { a.x, a.y, a.w, a.h }, editing);
        const std::string shown = (editing && edit_buf_) ? (*edit_buf_ + "|") : a.text;
        r.draw_text_wrapped(a.x + 8.f, a.y + 9.f, shown.c_str(), a.w - 16.f,
                            sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_body);
        // delete × (top-right); hit-tested in on_down.
        r.draw_text(a.x + a.w - 13.f, a.y + 3.f, "\xC3\x97", sty.dim[0], sty.dim[1], sty.dim[2], 0.9f, sty.fs_label);
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
            const auto* d = vg_->registry()->descriptor_for(nm);   // v3+ metadata (both optional)
            // VISUAL ops only. The registry is shared with the built-in AUDIO ops (Bitcrush/LFO/…, added
            // first in registration order), so filter to GPU ops the way list_operator_catalog does —
            // otherwise audio DSP nodes leak into the visuals add-menu.
            if (!d || !d->has_process_gpu) continue;
            Chooser::Entry e;
            e.label = nm;
            e.id = nm;
            // A shader row says SHADER: same catalog, same spawn, but it is a FILE you can open,
            // edit and fork — the one distinction worth drawing (ADR-0016).
            e.badge = (shaders_ && shaders_->is_shader(nm)) ? "shader" : "op";
            e.spawn = { Domain::Visual, SpawnKind::VisualOp, nm };   // ADR-0023 step 5
            e.accent = style().gpu;                   // both are visual ops: one zone, one accent
            if (d->summary) e.summary = d->summary;
            for (uint32_t k = 0; k < d->keyword_count; ++k)
                if (d->keywords && d->keywords[k]) { e.hay += d->keywords[k]; e.hay += ' '; }
            e.role = d->role;   // ADR-0046: role chip + recipe demotion
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
    // ADR-0050: draw each visual op's bundled preview thumbnail in its row. Rows with no committed
    // preview (+ bridge/data-source rows) return false and fall back to the accent dot.
    chooser_.set_preview_drawer([this](Renderer2D& r, const Chooser::Entry& e,
                                       float x, float y, float w, float h) -> bool {
        if (e.spawn.kind != SpawnKind::VisualOp) return false;
        WGPUTextureView v = preview_view(preview_slug(e.spawn.type));
        if (!v) return false;
        r.draw_texture(x, y, w, h, v);
        return true;
    });
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
    } else if (e.spawn.kind == SpawnKind::BridgeNode) {   // a bridge source node (add_data_node notes the edit)
        const std::string src = e.spawn.source.empty() ? source_id_for(e.spawn.char_id) : e.spawn.source;
        const bool existed = (find_source_node(src) >= 0);
        add_data_node(e.label, src);
        int ni, oi;   // only reposition a NEWLY-created entity (don't move an existing Master/Track node)
        if (!existed && find_source_output(src, ni, oi)) {
            nodes_data_[ni].x = chooser_.spawn_x() - 84.f; nodes_data_[ni].y = chooser_.spawn_y() - 36.f;
        }
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

bool NodeGraph::on_down(double x, double y, bool shift, bool super) {
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

    // ADR-0033 P5: sticky notes float on top — hit-test them before nodes/ports. Top-most first.
    for (int i = int(annos_.size()) - 1; i >= 0; --i) {
        const Annotation& a = annos_[i];
        if (hit({ a.x + a.w - 15.f, a.y + 2.f, 14.f, 14.f }, wx, wy)) {   // delete ×
            remove_annotation(a.id); note_edit_("Delete Note"); return true;
        }
        if (hit({ a.x, a.y, a.w, a.h }, wx, wy)) {                        // body → drag
            drag_mode_ = 7; anno_drag_ = a.id; dx_ = wx - a.x; dy_ = wy - a.y; return true;
        }
    }

    // disconnect an op input or a param port
    int oiPort = 0; int oi = nearest_op_in(wx, wy, hr, oiPort);
    if (oi >= 0) { set_op_input_port(oi, oiPort, -1); note_edit_("Disconnect"); return true; }
    int pni, pl;
    if (nearest_param(wx, wy, pr, pni, pl)) {
        reg_.disconnect(node_param_dest(vg_->nodes()[pni].id, node_plabel(vg_, pni, pl)));
        if (vg_) vg_->clear_param_control(pni, pl);   // ADR-0053 B2: also drop a typed control edge into this param
        note_edit_("Disconnect Mapping");
        return true;
    }

    // start a wire from a source-node OUTPUT ROW port (ADR-0053 A2: one port per named output)
    for (int i = 0; i < int(nodes_data_.size()); ++i)
        for (int o = 0; o < int(nodes_data_[i].outs.size()); ++o) {
            float px, py; source_out_port(i, o, px, py);
            if (std::hypot(wx - px, wy - py) < hr) { drag_mode_ = 3; wire_from_ = i; wire_from_out_ = o; return true; }
        }
    // start a wire from an op output port (capture WHICH output — a multi-lane producer like
    // ReactiveMaster / LanePalette has several; the drop target decides texture edge vs control edge)
    int ooPort = 0; int oo = nearest_op_out(wx, wy, hr, ooPort);
    if (oo >= 0) { drag_mode_ = 4; wire_from_ = oo; wire_from_out_ = ooPort; return true; }

    // op-node x button / body drag
    for (int i = 0; i < n; ++i) {
        float ox, oy, ow, oh; op_node_rect(i, ox, oy, ow, oh);
        if (!vg_->nodes()[i].is_output() && hit({ ox + ow - 15.f, oy + 3.f, 12.f, 12.f }, wx, wy)) {
            vg_->remove_node(i); sel_.clear(); sel_op_ = -1; sync_op_pos(); note_edit_("Delete Node"); return true;
        }
        if (hit({ ox, oy, ow, oh }, wx, wy)) {
            const int id = vg_->nodes()[i].id;
            // ADR-0033 P1: ⇧/⌘-click toggles this card's membership (no drag). The blue ring follows.
            if (shift || super) { sel_.toggle(id); resync_sel_op_(); return true; }
            if (vg_->nodes()[i].is_output()) { vg_->set_active_output(i); note_edit_("Set Output"); }  // clicking selects the viewer source
            // Plain click on an UNselected card replaces the selection; clicking one already in the set
            // keeps the set (so the whole group drags together) but re-anchors the primary to it.
            if (!sel_.contains(id)) sel_.replace(id); else sel_.set_primary(id);
            sel_op_ = i;  // the inspector / primary target
            // Snapshot every selected op's world position so on_move can shift them by one shared delta.
            grp_start_.clear();
            for (int sid : sel_.ids()) {
                const int si = op_index_of_id(sid);
                if (si >= 0 && si < int(op_pos_.size())) grp_start_.push_back({ sid, op_pos_[si] });
            }
            drag_mode_ = 2; drag_idx_ = i; dx_ = wx - ox; dy_ = wy - oy; return true;
        }
    }
    // (Adding an op is Tab-only now — the registry-driven chooser, spawned at the cursor. The old
    // hard-coded 4-item "ADD OP" strip couldn't even reach the newer ops. Re-layout moved to the
    // visuals column's corner chrome, handled in app/input.cpp.)

    // source-node body drag
    for (int i = 0; i < int(nodes_data_.size()); ++i)
        if (hit({ nodes_data_[i].x, nodes_data_[i].y, nodes_data_[i].w, nodes_data_[i].h }, wx, wy)) {
            drag_mode_ = 1; drag_idx_ = i; dx_ = wx - nodes_data_[i].x; dy_ = wy - nodes_data_[i].y; return true;
        }
    // empty canvas within the network pane. ADR-0033 P1: ⇧-drag rubber-bands a marquee (⌘ makes it
    // additive); a plain drag pans the view (screen coords), preserving the existing gesture.
    if (x >= bx0_ && x < bx1_ && y >= by0_ && y < by1_) {
        if (shift) {
            drag_mode_ = 6; marq_add_ = super;
            marq_x0_ = marq_x1_ = wx; marq_y0_ = marq_y1_ = wy;   // world corners
            return true;
        }
        drag_mode_ = 5; pan_last_x_ = float(x); pan_last_y_ = float(y); return true;
    }
    return false;
}

void NodeGraph::on_move(double x, double y) {
    double wx, wy; to_world(x, y, wx, wy);
    cx_ = wx; cy_ = wy;  // world cursor (drag-preview wires draw under the transform)
    if (drag_mode_ == 1 && drag_idx_ >= 0 && drag_idx_ < int(nodes_data_.size())) {
        nodes_data_[drag_idx_].x = float(wx - dx_); nodes_data_[drag_idx_].y = float(wy - dy_);
        note_edit_("Move Node", "move-node");
    } else if (drag_mode_ == 2 && drag_idx_ >= 0 && drag_idx_ < int(op_pos_.size())) {
        // ADR-0033 P1 group-drag: the grabbed node follows the cursor; every other selected node
        // shifts by the same world delta (measured from the grabbed node's grab-time position).
        const float tgt_x = float(wx - dx_), tgt_y = float(wy - dy_);
        const int drag_id = (drag_idx_ < int(vg_->nodes().size())) ? vg_->nodes()[drag_idx_].id : -1;
        float start_x = tgt_x, start_y = tgt_y;
        for (const auto& e : grp_start_) if (e.first == drag_id) { start_x = e.second.first; start_y = e.second.second; break; }
        const float ddx = tgt_x - start_x, ddy = tgt_y - start_y;
        if (grp_start_.empty()) {
            op_pos_[drag_idx_] = { tgt_x, tgt_y };
        } else {
            for (const auto& e : grp_start_) {
                const int si = op_index_of_id(e.first);
                if (si >= 0 && si < int(op_pos_.size())) op_pos_[si] = { e.second.first + ddx, e.second.second + ddy };
            }
        }
        note_edit_("Move Node", "move-node");
    } else if (drag_mode_ == 6) {   // ADR-0033 P1: extend the marquee's far corner (world coords)
        marq_x1_ = wx; marq_y1_ = wy;
    } else if (drag_mode_ == 7 && anno_drag_ >= 0) {   // ADR-0033 P5: drag a sticky note
        move_annotation(anno_drag_, float(wx - dx_), float(wy - dy_));
        note_edit_("Move Note", "move-note");
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
    if (drag_mode_ == 3 && wire_from_ >= 0 && wire_from_ < int(nodes_data_.size()) && wire_from_out_ >= 0 && vg_) {
        int pni, pl;
        if (nearest_param(wx, wy, 18.0 / canvas_.view().scale, pni, pl)) {
            connect_data_to_param(wire_from_, pni, pl, wire_from_out_);
        } else {
            // Gesture B: no VISIBLE param row under the drop — but if it landed on a node body, park a
            // request so the app opens the reveal+connect menu (reach a hidden/collapsed param).
            // NOTE (A2): the reveal-menu path currently wires output 0 of the source node; the direct
            // drag-to-visible-param path above carries the exact output row.
            const int tgt = op_at_world(wx, wy);
            if (tgt >= 0 && !vg_->nodes()[tgt].is_output() && node_pcount(vg_, tgt) > 0) {
                pmreq_node_ = tgt; pmreq_src_ = wire_from_; pmreq_sx_ = x; pmreq_sy_ = y;
            }
        }
    } else if (drag_mode_ == 4 && wire_from_ >= 0) {
        const double R = 18.0 / canvas_.view().scale;
        int tport = 0; int target = nearest_op_in(wx, wy, R, tport);
        // ADR-0047: refuse a wire whose stream types don't match (the drag sources output port 0, as
        // set_op_input_port does) — the wire just doesn't form, mirroring a rejected audio note edge.
        if (target >= 0 && vg_ && vg_->can_connect(target, tport, wire_from_, 0)) {
            set_op_input_port(target, tport, wire_from_); note_edit_("Connect");
        } else {
            // ADR-0053 B2: dropped on a PARAM port instead of a texture input → a typed CONTROL EDGE, if
            // the dragged output is a value lane (only value lanes can drive a param; a texture output
            // resolves to lane -1 and forms nothing). The source is referenced by stable id so the edge
            // survives reorder/removal.
            int pni, pl;
            if (vg_ && nearest_param(wx, wy, R, pni, pl)) {
                const int lane = op_out_value_lane(wire_from_, wire_from_out_ >= 0 ? wire_from_out_ : 0);
                if (lane >= 0 && pni != wire_from_) {
                    vivid::VisualControlShape sh;   // default: full-range unipolar, no smoothing (tune in the M panel)
                    vg_->set_param_control(pni, pl, vg_->nodes()[wire_from_].id, lane, sh);
                    note_edit_("Connect Control");
                }
            }
        }
    } else if (drag_mode_ == 6) {   // ADR-0033 P1: resolve the marquee against every op card
        std::vector<SelItem> items;
        const int n = vg_ ? int(vg_->nodes().size()) : 0;
        items.reserve(n);
        for (int i = 0; i < n; ++i) {
            float ox, oy, ow, oh; op_node_rect(i, ox, oy, ow, oh);
            items.push_back({ vg_->nodes()[i].id, { ox, oy, ow, oh } });
        }
        sel_.resolve_marquee(items,
            { float(marq_x0_), float(marq_y0_), float(marq_x1_ - marq_x0_), float(marq_y1_ - marq_y0_) },
            marq_add_);
        resync_sel_op_();
    }
    drag_mode_ = 0; drag_idx_ = -1; wire_from_ = -1; wire_from_out_ = -1; grp_start_.clear(); anno_drag_ = -1;
}

}  // namespace vivid::ui
