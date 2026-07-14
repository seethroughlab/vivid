#include "gpu/visual_graph.h"

#include "operator_api/gpu_operator.h"
#include "gpu/asset_shader.h"   // AssetShader (CustomShader .glsl push)
#include "gpu/graph_topo.h"     // topo_order (shared, headless-testable DFS)
#include "gpu/gpu_util.h"   // kMsaaSamples (present blit draws into the frame MSAA target)

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <functional>

namespace vivid {

// Final-present blit (the node feeding Output -> a surface: the floating preview, or the pop-out
// window). This is the host's own present pass; all visual operators are auto-discovered package
// dylibs.
//
// p0..p3 carry the UV window (scale + offset) that letterboxes the output into a surface of a
// different aspect (ADR-0014 / blit_fit in gpu/output_format.h). Fit widens the window past the
// source, so samples outside [0,1] are painted black — the bars. (An explicit test is required:
// the sampler clamps to edge, which would smear the border pixels instead.)
static const char* kBlitGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float u_su; float u_sv; float u_ou; float u_ov; };
layout(set = 0, binding = 1) uniform texture2D u_tex;
layout(set = 0, binding = 2) uniform sampler   u_samp;
void main() {
    vec2 uv = v_uv * vec2(u_su, u_sv) + vec2(u_ou, u_ov);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) o_color = vec4(0.0, 0.0, 0.0, 1.0);
    else o_color = texture(sampler2D(u_tex, u_samp), uv);
}
)";

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
        // A rebuild (hot-reload) restores what the node was set to, matched BY NAME — the new
        // operator may have added, removed or reordered its params, so the old index means
        // nothing. Params the new version doesn't have are dropped; new ones keep their default.
        for (size_t i = 0; i < np && !n.stash.empty(); ++i) {
            const char* nm = n.inst.param_ptrs[i] ? n.inst.param_ptrs[i]->name : nullptr;
            if (!nm) continue;
            for (const auto& [k, v] : n.stash)
                if (k == nm) { n.base[i] = v; n.params[i] = v; break; }
        }
        n.stash.clear();
        return true;
    }
    return false;
}

int VisualGraph::release_op_instances(const std::string& type) {
    int n = 0;
    for (auto& nd : nodes_) if (nd.op_type == type) {
        nd.stash_params();          // the names live on the instance we are about to drop
        nd.inst = OpInstance{};
        ++n;
    }
    return n;
}

int VisualGraph::rebuild_op_instances(const std::string& type) {
    int n = 0;
    for (auto& nd : nodes_) if (nd.op_type == type) {
        if (nd.inst.op) nd.stash_params();   // still live (a shader reload doesn't release first)
        make_instance(nd, type);
        ++n;
    }
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

void VisualGraph::set_rt_size(uint32_t w, uint32_t h) {
    w = std::clamp(w, 16u, 7680u);
    h = std::clamp(h, 16u, 4320u);
    if (w == rtW_ && h == rtH_) return;   // steady state: no-op
    rtW_ = w; rtH_ = h;
    // Reallocate every node's target + the fallback. Safe here (run_chain calls this before it has
    // recorded anything into the encoder that references an RT): the previous frame's command
    // buffer was already submitted, and wgpu holds its own references to the resources it uses,
    // so releasing ours doesn't pull the rug out from under in-flight work.
    for (auto& r : rts_) { r.release(); r.init(dev_, rtW_, rtH_, fmt_); }
    fallback_.release(); fallback_.init(dev_, rtW_, rtH_, fmt_);
    fb_cleared_ = false;   // the new fallback texture is uninitialized -> re-clear it this frame
    // (Feedback/ping-pong ops rebuild their own history textures when the size changes — they key
    // on ctx->output_width/height — so a resize costs one frame of trails. That's correct.)
}

// Index of a named param on the active Output node (-1 if absent — e.g. an Output restored from a
// session saved before that param existed).
static int output_param_index(const VisualNode& n, const char* name) {
    for (int i = 0; i < static_cast<int>(n.inst.param_ptrs.size()); ++i)
        if (n.inst.param_ptrs[i]->name && std::strcmp(n.inst.param_ptrs[i]->name, name) == 0) return i;
    return -1;
}

float VisualGraph::output_param(const char* name, float def) const {
    const int oi = output_index();
    if (oi < 0) return def;
    const VisualNode& n = nodes_[oi];
    const int i = output_param_index(n, name);
    return (i >= 0 && i < static_cast<int>(n.base.size())) ? n.base[i] : def;
}

void VisualGraph::set_output_param(const char* name, float v) {
    const int oi = output_index();
    if (oi < 0) return;
    VisualNode& n = nodes_[oi];
    const int i = output_param_index(n, name);
    if (i >= 0 && i < static_cast<int>(n.base.size())) n.base[i] = v;
}

// The Output node owns the output's identity (ADR-0014). Read the BASE params, not the resolved
// ones: resolved = base + live modulation, so a transient wired to `aspect` would otherwise thrash
// the render-target size every frame. The output's format is a document setting, not a modulatable
// signal.
void VisualGraph::apply_output_settings() {
    if (output_index() < 0) return;
    const auto enum_of = [&](const char* nm, float def) {
        return static_cast<int>(std::lround(output_param(nm, def)));
    };
    fit_ = static_cast<FitMode>(std::clamp(enum_of("fit", 0.f), 0, kNumFits - 1));
    uint32_t w = 0, h = 0;
    output_size_for(enum_of("aspect", static_cast<float>(kDefaultAspect)),
                    enum_of("height", static_cast<float>(kDefaultHeight)), w, h);
    set_rt_size(w, h);
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
    if (nodes_[i].is_output()) {                    // keep at least one Output
        int outs = 0; for (auto& n : nodes_) if (n.is_output()) ++outs;
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
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) if (nodes_[i].is_output()) {
        if (first < 0) first = i;
        if (nodes_[i].id == active_output_id_) return i;
    }
    return first;
}
void VisualGraph::set_active_output(int idx) {
    if (idx >= 0 && idx < static_cast<int>(nodes_.size()) && nodes_[idx].is_output())
        active_output_id_ = nodes_[idx].id;
}
bool VisualGraph::type_is_source(const std::string& type) const {
    if (type == "Video") return true;              // fed by the host's decoded frame, not by an edge
    const VividOperatorDescriptor* d = reg_ ? reg_->descriptor_for(type) : nullptr;
    if (!d) return false;                          // unknown type: not instantiable, so not a source
    for (uint32_t i = 0; i < d->port_count; ++i)
        if (d->ports[i].direction != VIVID_PORT_OUTPUT) return false;
    return true;
}
bool VisualGraph::set_generator(const std::string& type) {
    if (!type_is_source(type)) return false;       // a filter at the head would starve the chain
    for (auto& n : nodes_) if (n.is_source()) {
        return (n.op_type == type) ? true : make_instance(n, type);   // swap the operator instance
    }
    return false;
}
std::string VisualGraph::generator() const {
    for (const auto& n : nodes_) if (n.is_source()) return n.op_type;
    return {};
}

void VisualGraph::run_chain(WGPUCommandEncoder enc, float time, WGPUTextureView video_tex) {
    apply_output_settings();   // FIRST: may resize every RT, before the encoder references any
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
            if (n.is_video() && p == 0) v = video_tex;          // external source feeds the generator's port 0
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
}

void VisualGraph::present_to(WGPUCommandEncoder enc, WGPUTextureView view,
                             float vx, float vy, float vw, float vh,
                             float surf_w, float surf_h, float time, bool clear) {
    const int outIdx = output_index();
    if (outIdx < 0) return;
    // Clamp the destination rect into the target. A rect that escapes the render target is a
    // process abort in wgpu (a validation panic in the submit), not a dropped draw — so this is a
    // hard backstop under whatever geometry the caller computed.
    if (surf_w > 0.f && surf_h > 0.f) {
        const float x0 = std::clamp(vx, 0.f, surf_w), y0 = std::clamp(vy, 0.f, surf_h);
        const float x1 = std::clamp(vx + vw, 0.f, surf_w), y1 = std::clamp(vy + vh, 0.f, surf_h);
        vx = x0; vy = y0; vw = x1 - x0; vh = y1 - y0;
    }
    if (vw < 1.f || vh < 1.f) return;   // fully off-surface / degenerate: nothing to present
    const int feed = nodes_[outIdx].in(0);
    if (feed >= 0 && feed < static_cast<int>(nodes_.size())) {
        // Letterbox/crop/stretch the output into this surface per the Output node's fit mode. Both
        // surfaces (the floating preview + the pop-out window) go through here, so they agree.
        const float dst_a = (vh > 0.f) ? vw / vh : 1.f;
        const BlitFit f = blit_fit(rt_aspect(), dst_a, fit_);
        const float p[4] = { f.su, f.sv, f.ou, f.ov };
        WGPUTextureView t[1] = { rts_[feed].view };
        blit_.render(enc, view, vx, vy, vw, vh, clear, t, 1, time, p, 4);
    }
}

void VisualGraph::shutdown() {
    blit_.shutdown();
    for (auto& r : rts_) r.release();
    rts_.clear();
    fallback_.release();
}

}  // namespace vivid
