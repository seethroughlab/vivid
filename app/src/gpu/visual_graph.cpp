#include "gpu/visual_graph.h"

#include "operator_api/gpu_operator.h"
#include "gpu/asset_shader.h"   // AssetShader (CustomShader .glsl push)
#include "gpu/graph_topo.h"     // topo_order (shared, headless-testable DFS)
#include "gpu/gpu_util.h"   // kMsaaSamples (present blit draws into the frame MSAA target)

#include <algorithm>
#include <filesystem>
#include <functional>

namespace vivid {

// Final-present blit (the node feeding Output -> the on-screen viewer). This is the
// host's own present pass; all visual operators are auto-discovered package dylibs.
static const char* kBlitGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float p0; float p1; float p2; float p3; };
layout(set = 0, binding = 1) uniform texture2D u_tex;
layout(set = 0, binding = 2) uniform sampler   u_samp;
void main() { o_color = texture(sampler2D(u_tex, u_samp), v_uv); }
)";

const char* vop_name(VOp op) {
    switch (op) {
        case VOp::Plasma:   return "Plasma";
        case VOp::Video:    return "Video";
        case VOp::Feedback: return "Feedback";
        case VOp::Blur:     return "Blur";
        default:            return "Output";
    }
}
VOp vop_from_name(const std::string& n) {
    if (n == "Video")    return VOp::Video;
    if (n == "Feedback") return VOp::Feedback;
    if (n == "Blur")     return VOp::Blur;
    if (n == "Output")   return VOp::Output;
    return VOp::Plasma;
}

// Clear `view` to opaque black (one render pass).
static void clear_target(WGPUCommandEncoder enc, WGPUTextureView view) {
    WGPURenderPassColorAttachment color{};
    color.view = view;
    color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = WGPUColor{ 0, 0, 0, 1 };
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

bool VisualGraph::make_instance(VisualNode& n, const std::string& type) {
    n.op_type = type;
    n.op = vop_from_name(type);
    if (!reg_) return false;
    std::vector<DescriptorValidationIssue> issues;
    if (auto inst = reg_->create(type, issues)) {
        n.inst = std::move(*inst);
        // Seed base + resolved params from the operator's DECLARED defaults (not 0), so a freshly
        // added node looks the way its author intended (e.g. Shape's size/colour). Saved sessions
        // still restore their own explicit base values, so old projects are unaffected.
        const size_t np = n.inst.param_ptrs.size();
        n.params.resize(np); n.base.resize(np); n.file_params.resize(np);
        for (size_t i = 0; i < np; ++i) {
            const float d = n.inst.param_ptrs[i] ? n.inst.param_ptrs[i]->default_value : 0.f;
            n.base[i] = d; n.params[i] = d;
        }
        return true;
    }
    return false;
}

int VisualGraph::release_op_instances(const std::string& type) {
    int n = 0;
    for (auto& nd : nodes_) if (nd.op_type == type) { nd.inst = OpInstance{}; ++n; }
    return n;
}

int VisualGraph::rebuild_op_instances(const std::string& type) {
    int n = 0;
    for (auto& nd : nodes_) if (nd.op_type == type) { make_instance(nd, type); ++n; }
    return n;
}

bool VisualGraph::init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat fmt,
                       uint32_t rtW, uint32_t rtH, OpRegistry* registry) {
    dev_ = device; q_ = queue; fmt_ = fmt; rtW_ = rtW; rtH_ = rtH; reg_ = registry;
    // Present blit draws into the frame's 4x MSAA color target (op RTs stay 1x).
    if (!blit_.init(device, queue, fmt, kBlitGLSL, 1, kMsaaSamples)) return false;
    fallback_.init(device, rtW, rtH, fmt);
    reset_to_default();   // Plasma -> Feedback -> Blur -> Output (ids 0..3)
    return true;
}

void VisualGraph::reset_to_default() {
    nodes_.clear(); next_id_ = 0;
    add_node("Plasma"); add_node("Feedback"); add_node("Blur"); add_node("Output");
    nodes_[1].set_in(0, 0); nodes_[2].set_in(0, 1); nodes_[3].set_in(0, 2);
    active_output_id_ = nodes_[3].id;
    ensure_resources(nodes_.size());
}

void VisualGraph::ensure_resources(size_t n) {
    while (rts_.size() > n) { rts_.back().release(); rts_.pop_back(); }
    while (rts_.size() < n) { rts_.emplace_back(); rts_.back().init(dev_, rtW_, rtH_, fmt_); }
}

int VisualGraph::add_node(const std::string& type) {
    nodes_.emplace_back();
    VisualNode& n = nodes_.back();
    n.id = next_id_++;
    make_instance(n, type);
    ensure_resources(nodes_.size());
    return static_cast<int>(nodes_.size()) - 1;
}
void VisualGraph::load_node(const std::string& type, int id) {
    nodes_.emplace_back();
    VisualNode& n = nodes_.back();
    n.id = id;
    if (id >= next_id_) next_id_ = id + 1;
    make_instance(n, type);
    ensure_resources(nodes_.size());
}
void VisualGraph::remove_node(int i) {
    if (i < 0 || i >= static_cast<int>(nodes_.size())) return;
    if (nodes_[i].op == VOp::Output) {               // keep at least one Output
        int outs = 0; for (auto& n : nodes_) if (n.op == VOp::Output) ++outs;
        if (outs <= 1) return;
    }
    nodes_.erase(nodes_.begin() + i);
    for (auto& n : nodes_)
        for (int& e : n.inputs) {   // drop edges to the removed node; shift indices above it down
            if (e == i) e = -1; else if (e > i) --e;
        }
    ensure_resources(nodes_.size());
}
void VisualGraph::set_input(int node, int port, int src) {
    if (node < 0 || node >= static_cast<int>(nodes_.size()) || port < 0) return;
    if (src == node) return;                          // no self-loops
    nodes_[node].set_in(port, (src >= 0 && src < static_cast<int>(nodes_.size())) ? src : -1);
}
int VisualGraph::output_index() const {
    int first = -1;
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) if (nodes_[i].op == VOp::Output) {
        if (first < 0) first = i;
        if (nodes_[i].id == active_output_id_) return i;
    }
    return first;
}
void VisualGraph::set_active_output(int idx) {
    if (idx >= 0 && idx < static_cast<int>(nodes_.size()) && nodes_[idx].op == VOp::Output)
        active_output_id_ = nodes_[idx].id;
}
void VisualGraph::set_generator(VOp g) {
    for (auto& n : nodes_) if (n.op == VOp::Plasma || n.op == VOp::Video) {
        if (n.op != g) make_instance(n, vop_name(g));   // swap the operator instance
        return;
    }
}
VOp VisualGraph::generator() const {
    for (const auto& n : nodes_) if (n.op == VOp::Plasma || n.op == VOp::Video) return n.op;
    return VOp::Plasma;
}

void VisualGraph::render(WGPUCommandEncoder enc, WGPUTextureView screen,
                         float vx, float vy, float vw, float vh, float time,
                         WGPUTextureView video_tex) {
    ensure_resources(nodes_.size());
    if (!fb_cleared_) { clear_target(enc, fallback_.view); fb_cleared_ = true; }
    const int outIdx = output_index();
    if (outIdx < 0) return;
    const int feed = nodes_[outIdx].in(0);

    // Topological order (post-order DFS over ALL input ports, cycle-safe) — the shared,
    // headless-testable helper. Subsumes the old linear back-walk and supports N-input ops.
    const int nnodes = static_cast<int>(nodes_.size());
    std::vector<std::vector<int>> adj(nnodes);
    for (int i = 0; i < nnodes; ++i) adj[i] = nodes_[i].inputs;
    const std::vector<int> order = topo_order(adj, feed);

    for (int idx : order) {
        VisualNode& n = nodes_[idx];
        if (!n.inst.op) continue;
        // One texture view per declared input port (fallback = black), resolved from the node's
        // edges. Storage lives for the process_gpu call below.
        const int nin = n.inst.input_port_count;
        std::vector<WGPUTextureView> inview(nin > 0 ? nin : 0, fallback_.view);
        for (int p = 0; p < nin; ++p) {
            const int e = n.in(p);
            WGPUTextureView v = (e >= 0 && e < nnodes) ? rts_[e].view : fallback_.view;
            if (n.op == VOp::Video && p == 0) v = video_tex;    // external source feeds the generator's port 0
            inview[p] = v ? v : fallback_.view;
        }

        VividGpuContext ctx{};
        ctx.time = time; ctx.delta_time = 0.0; ctx.frame = frame_;
        ctx.param_values = n.params.empty() ? nullptr : n.params.data();
        ctx.device = dev_; ctx.queue = q_; ctx.command_encoder = enc;
        ctx.output_texture = rts_[idx].tex;
        ctx.output_texture_view = rts_[idx].view;
        ctx.output_width = rtW_; ctx.output_height = rtH_; ctx.output_format = fmt_;
        ctx.input_texture_views = inview.empty() ? nullptr : inview.data();
        ctx.input_texture_count = static_cast<uint32_t>(nin);

        // FILE/TEXT params: hand the op a dense array of its file-param strings (in param
        // order), project-resolved for FILE params. Storage lives for the process_gpu call.
        std::vector<std::string> fpv_storage;
        std::vector<const char*> fpv;
        for (size_t l = 0; l < n.inst.param_ptrs.size(); ++l) {
            const auto* pb = n.inst.param_ptrs[l];
            if (!pb || (pb->type != VIVID_PARAM_FILE && pb->type != VIVID_PARAM_TEXT)) continue;
            std::string v = (l < n.file_params.size()) ? n.file_params[l] : std::string();
            if (pb->type == VIVID_PARAM_FILE && !v.empty()) {
                std::filesystem::path ap(v);
                if (ap.is_relative() && !asset_dir_.empty()) ap = std::filesystem::path(asset_dir_) / ap;
                v = ap.string();
            }
            fpv_storage.push_back(std::move(v));
        }
        for (auto& s : fpv_storage) fpv.push_back(s.c_str());
        ctx.file_param_values = fpv.empty() ? nullptr : fpv.data();
        ctx.file_param_count  = static_cast<uint32_t>(fpv.size());

        sync_params(n.inst, n.params.empty() ? nullptr : n.params.data(),
                    static_cast<int>(n.params.size()));
        // Data-driven shader nodes: resolve the node's relative asset against the project
        // dir and hand the absolute path to the operator (it (re)loads on change).
        if (!n.asset.empty())
            if (auto* as = dynamic_cast<AssetShader*>(n.inst.op.get())) {
                std::filesystem::path ap(n.asset);
                if (ap.is_relative() && !asset_dir_.empty()) ap = std::filesystem::path(asset_dir_) / ap;
                as->set_asset_path(ap.string());
            }
        if (auto* g = dynamic_cast<GpuProcessable*>(n.inst.op.get())) g->process_gpu(&ctx);
    }
    ++frame_;

    if (feed >= 0 && feed < static_cast<int>(nodes_.size())) {
        WGPUTextureView f[1] = { rts_[feed].view };
        blit_.render(enc, screen, vx, vy, vw, vh, /*clear*/false, f, 1, time, nullptr, 0);
    }
}

void VisualGraph::present_to(WGPUCommandEncoder enc, WGPUTextureView view,
                             float vx, float vy, float vw, float vh, float time) {
    const int outIdx = output_index();
    if (outIdx < 0) return;
    const int feed = nodes_[outIdx].in(0);
    if (feed >= 0 && feed < static_cast<int>(nodes_.size())) {
        WGPUTextureView f[1] = { rts_[feed].view };
        blit_.render(enc, view, vx, vy, vw, vh, /*clear*/true, f, 1, time, nullptr, 0);
    }
}

void VisualGraph::shutdown() {
    blit_.shutdown();
    for (auto& r : rts_) r.release();
    rts_.clear();
    fallback_.release();
}

}  // namespace vivid
