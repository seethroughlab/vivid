#include "gpu/visual_graph.h"

#include "operator_api/gpu_operator.h"
#include "gpu/asset_shader.h"   // AssetShader (CustomShader .glsl push)
#include "gpu/gpu_util.h"   // kMsaaSamples (present blit draws into the frame MSAA target)

#include <algorithm>
#include <filesystem>
#include <functional>

namespace vivid {

// Final-present blit (the node feeding Output -> the on-screen viewer). The op
// shaders (plasma/feedback/blur/blit) now live in gpu/builtin_ops.cpp.
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
        n.params.resize(np); n.base.resize(np);
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
    nodes_[1].input = 0; nodes_[2].input = 1; nodes_[3].input = 2;
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
    n.id = next_id_++; n.input = -1;
    make_instance(n, type);
    ensure_resources(nodes_.size());
    return static_cast<int>(nodes_.size()) - 1;
}
void VisualGraph::load_node(const std::string& type, int id) {
    nodes_.emplace_back();
    VisualNode& n = nodes_.back();
    n.id = id; n.input = -1;
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
    for (auto& n : nodes_) {
        if (n.input == i) n.input = -1;           else if (n.input   > i) --n.input;
        if (n.input_b == i) n.input_b = -1;       else if (n.input_b > i) --n.input_b;
    }
    ensure_resources(nodes_.size());
}
void VisualGraph::set_input(int node, int input) {
    if (node < 0 || node >= static_cast<int>(nodes_.size())) return;
    if (input == node) return;                       // no self-loops
    nodes_[node].input = (input >= 0 && input < static_cast<int>(nodes_.size())) ? input : -1;
}
void VisualGraph::set_input_b(int node, int input) {
    if (node < 0 || node >= static_cast<int>(nodes_.size())) return;
    if (input == node) return;                       // no self-loops
    nodes_[node].input_b = (input >= 0 && input < static_cast<int>(nodes_.size())) ? input : -1;
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
    const int feed = nodes_[outIdx].input;

    // Topological order via a post-order DFS from the node feeding Output, over BOTH inputs
    // (port A + port B). Each node lands after its inputs; a shared node renders once (both
    // consumers read its RT). This subsumes the old linear back-walk (a single-input chain
    // yields the identical order) and supports 2-in ops (Composite). `state`: 0 unvisited,
    // 1 in-progress (a cycle edge is skipped), 2 done.
    std::vector<int> order; std::vector<char> state(nodes_.size(), 0);
    const int nnodes = static_cast<int>(nodes_.size());
    std::function<void(int)> visit = [&](int i) {
        if (i < 0 || i >= nnodes || state[i]) return;
        state[i] = 1;
        visit(nodes_[i].input);
        visit(nodes_[i].input_b);
        state[i] = 2;
        order.push_back(i);
    };
    visit(feed);

    for (int idx : order) {
        VisualNode& n = nodes_[idx];
        if (!n.inst.op) continue;
        const int inA = n.input, inB = n.input_b;
        WGPUTextureView va = (inA >= 0 && inA < nnodes) ? rts_[inA].view : fallback_.view;
        if (n.op == VOp::Video) va = video_tex;             // external source feeds the generator
        if (!va) va = fallback_.view;
        WGPUTextureView vb = (inB >= 0 && inB < nnodes) ? rts_[inB].view : fallback_.view;
        WGPUTextureView inputs[2] = { va, vb };
        const uint32_t nin = static_cast<uint32_t>(std::min(n.inst.input_port_count, 2));

        VividGpuContext ctx{};
        ctx.time = time; ctx.delta_time = 0.0; ctx.frame = frame_;
        ctx.param_values = n.params.empty() ? nullptr : n.params.data();
        ctx.device = dev_; ctx.queue = q_; ctx.command_encoder = enc;
        ctx.output_texture = rts_[idx].tex;
        ctx.output_texture_view = rts_[idx].view;
        ctx.output_width = rtW_; ctx.output_height = rtH_; ctx.output_format = fmt_;
        ctx.input_texture_views = (nin > 0) ? inputs : nullptr;
        ctx.input_texture_count = nin;

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
    const int feed = nodes_[outIdx].input;
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
