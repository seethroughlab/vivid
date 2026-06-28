#include "gpu/visual_graph.h"
#include <algorithm>

namespace vivid {

static const char* kPlasmaGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U {
    vec2 u_res; float u_time; float u_warp; float u_hue; float u_density; float u_glow; };
void main() {
    vec2 uv = v_uv;
    float t = u_time;
    float dens = 6.0 + u_density * 18.0;
    vec2 w = uv + u_warp * 0.3 * vec2(sin(uv.y * 8.0 + t), cos(uv.x * 8.0 + t));
    float v = sin(w.x * dens + t) + sin(w.y * dens + t * 1.3)
            + sin((w.x + w.y) * dens * 0.6 + t * 0.7)
            + sin(length(w - 0.5) * dens * 1.8 - t * 2.0);
    vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + v + u_hue * 6.2832);
    o_color = vec4(col * (0.6 + u_glow), 1.0);
}
)";

static const char* kFeedbackGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float u_decay; float p1; float p2; float p3; };
layout(set = 0, binding = 1) uniform texture2D u_gen;
layout(set = 0, binding = 2) uniform sampler   u_samp;
layout(set = 0, binding = 3) uniform texture2D u_prev;
void main() {
    vec2 c = v_uv - 0.5;
    vec2 puv = 0.5 + c * 0.985;
    vec4 gen  = texture(sampler2D(u_gen,  u_samp), v_uv);
    vec4 prev = texture(sampler2D(u_prev, u_samp), puv);
    o_color = max(gen, prev * u_decay);
}
)";

static const char* kBlurGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float u_radius; float p1; float p2; float p3; };
layout(set = 0, binding = 1) uniform texture2D u_tex;
layout(set = 0, binding = 2) uniform sampler   u_samp;
void main() {
    vec2 px = (1.0 / u_res) * (1.0 + u_radius * 8.0);
    vec4 s = texture(sampler2D(u_tex, u_samp), v_uv) * 0.36;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2( px.x, 0.0)) * 0.16;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2(-px.x, 0.0)) * 0.16;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2(0.0,  px.y)) * 0.16;
    s += texture(sampler2D(u_tex, u_samp), v_uv + vec2(0.0, -px.y)) * 0.16;
    o_color = s;
}
)";

static const char* kBlitGLSL = R"(#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U { vec2 u_res; float u_time; float p0; float p1; float p2; float p3; };
layout(set = 0, binding = 1) uniform texture2D u_tex;
layout(set = 0, binding = 2) uniform sampler   u_samp;
void main() { o_color = texture(sampler2D(u_tex, u_samp), v_uv); }
)";

bool VisualGraph::init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat fmt,
                       uint32_t rtW, uint32_t rtH) {
    dev_ = device; q_ = queue; fmt_ = fmt; rtW_ = rtW; rtH_ = rtH;
    if (!plasma_.init(device, queue, fmt, kPlasmaGLSL)) return false;
    if (!feedback_.init(device, queue, fmt, kFeedbackGLSL, 2)) return false;
    if (!blur_.init(device, queue, fmt, kBlurGLSL, 1)) return false;
    if (!blit_.init(device, queue, fmt, kBlitGLSL, 1)) return false;
    // Default chain: Plasma -> Feedback -> Blur -> Output (ids 0..3).
    nodes_ = { { VOp::Plasma, -1, 0 }, { VOp::Feedback, 0, 1 }, { VOp::Blur, 1, 2 }, { VOp::Output, 2, 3 } };
    next_id_ = 4;
    ensure_resources(nodes_.size());
    return true;
}

void VisualGraph::ensure_resources(size_t n) {
    auto fit = [&](std::vector<RenderTarget>& v) {
        while (v.size() > n) { v.back().release(); v.pop_back(); }
        while (v.size() < n) { v.emplace_back(); v.back().init(dev_, rtW_, rtH_, fmt_); }
    };
    fit(rts_); fit(histA_); fit(histB_);
    histCur_.resize(n, 0);
}

int VisualGraph::add_node(VOp op) {
    nodes_.push_back({ op, -1, next_id_++ });
    ensure_resources(nodes_.size());
    return static_cast<int>(nodes_.size()) - 1;
}
void VisualGraph::load_node(VOp op, int id) {
    nodes_.push_back({ op, -1, id });
    if (id >= next_id_) next_id_ = id + 1;
    ensure_resources(nodes_.size());
}
void VisualGraph::remove_node(int i) {
    if (i < 0 || i >= static_cast<int>(nodes_.size()) || nodes_[i].op == VOp::Output) return;
    nodes_.erase(nodes_.begin() + i);
    for (auto& n : nodes_) {
        if (n.input == i) n.input = -1;
        else if (n.input > i) --n.input;
    }
    ensure_resources(nodes_.size());
}
void VisualGraph::set_input(int node, int input) {
    if (node < 0 || node >= static_cast<int>(nodes_.size())) return;
    if (input == node) return;                       // no self-loops
    nodes_[node].input = (input >= 0 && input < static_cast<int>(nodes_.size())) ? input : -1;
}
int VisualGraph::output_index() const {
    for (int i = 0; i < static_cast<int>(nodes_.size()); ++i) if (nodes_[i].op == VOp::Output) return i;
    return -1;
}
void VisualGraph::set_generator(VOp g) {
    for (auto& n : nodes_) if (n.op == VOp::Plasma || n.op == VOp::Video) { n.op = g; return; }
}
VOp VisualGraph::generator() const {
    for (const auto& n : nodes_) if (n.op == VOp::Plasma || n.op == VOp::Video) return n.op;
    return VOp::Plasma;
}

void VisualGraph::render(WGPUCommandEncoder enc, WGPUTextureView screen,
                         float vx, float vy, float vw, float vh, float time,
                         WGPUTextureView video_tex) {
    ensure_resources(nodes_.size());
    const float rtw = static_cast<float>(rtW_), rth = static_cast<float>(rtH_);
    const int outIdx = output_index();
    if (outIdx < 0) return;
    const int feed = nodes_[outIdx].input;

    // Walk the input chain back from the node feeding Output, then reverse.
    std::vector<int> order; std::vector<char> seen(nodes_.size(), 0);
    for (int cur = feed; cur >= 0 && cur < static_cast<int>(nodes_.size()) && !seen[cur]; cur = nodes_[cur].input) {
        seen[cur] = 1; order.push_back(cur);
    }
    std::reverse(order.begin(), order.end());

    for (int idx : order) {
        VisualNode& n = nodes_[idx];
        const int in = n.input;
        const bool hasIn = (in >= 0 && in < static_cast<int>(nodes_.size()));
        switch (n.op) {
            case VOp::Plasma:
                plasma_.render(enc, rts_[idx].view, 0, 0, rtw, rth, time, n.params, /*clear*/true);
                break;
            case VOp::Video: {
                WGPUTextureView v[1] = { video_tex };
                blit_.render(enc, rts_[idx].view, 0, 0, rtw, rth, /*clear*/true, v, 1, time, nullptr, 0);
                break;
            }
            case VOp::Feedback: {
                RenderTarget& cur  = histCur_[idx] ? histB_[idx] : histA_[idx];
                RenderTarget& prev = histCur_[idx] ? histA_[idx] : histB_[idx];
                WGPUTextureView fin[2] = { hasIn ? rts_[in].view : rts_[idx].view, prev.view };
                const float decay = 0.82f + n.params[0] * 0.16f;   // normalized 0..1 -> 0.82..0.98
                feedback_.render(enc, cur.view, 0, 0, rtw, rth, /*clear*/true, fin, 2, time, &decay, 1);
                WGPUTextureView cv[1] = { cur.view };
                blit_.render(enc, rts_[idx].view, 0, 0, rtw, rth, /*clear*/true, cv, 1, time, nullptr, 0);
                histCur_[idx] ^= 1;
                break;
            }
            case VOp::Blur: {
                WGPUTextureView b[1] = { hasIn ? rts_[in].view : rts_[idx].view };
                const float radius = n.params[0];
                blur_.render(enc, rts_[idx].view, 0, 0, rtw, rth, /*clear*/true, b, 1, time, &radius, 1);
                break;
            }
            case VOp::Output: break;
        }
    }
    if (feed >= 0 && feed < static_cast<int>(nodes_.size())) {
        WGPUTextureView f[1] = { rts_[feed].view };
        blit_.render(enc, screen, vx, vy, vw, vh, /*clear*/false, f, 1, time, nullptr, 0);
    }
}

void VisualGraph::blit_node(WGPUCommandEncoder enc, WGPUTextureView screen, int idx,
                            float x, float y, float w, float h) {
    if (idx < 0 || idx >= static_cast<int>(rts_.size()) || !rts_[idx].view) return;
    if (idx < static_cast<int>(nodes_.size()) && nodes_[idx].op == VOp::Output) return;  // no own texture
    if (w < 1.f || h < 1.f) return;
    // Aspect-correct letterbox: fit the source into (x,y,w,h), centered, so the
    // recessed panel shows through as bars rather than the output being squished.
    const float srcA = (rtH_ > 0) ? static_cast<float>(rtW_) / static_cast<float>(rtH_) : 1.f;
    const float dstA = w / h;
    float fw = w, fh = h;
    if (srcA > dstA) fh = w / srcA;   // source relatively wider -> bars top/bottom
    else             fw = h * srcA;   // source relatively taller -> bars left/right
    const float fx = x + (w - fw) * 0.5f, fy = y + (h - fh) * 0.5f;
    WGPUTextureView f[1] = { rts_[idx].view };
    blit_.render(enc, screen, fx, fy, fw, fh, /*clear*/false, f, 1, 0.f, nullptr, 0);
}

void VisualGraph::shutdown() {
    plasma_.shutdown(); feedback_.shutdown(); blur_.shutdown(); blit_.shutdown();
    for (auto& r : rts_)   r.release();
    for (auto& r : histA_) r.release();
    for (auto& r : histB_) r.release();
    rts_.clear(); histA_.clear(); histB_.clear(); histCur_.clear();
}

}  // namespace vivid
