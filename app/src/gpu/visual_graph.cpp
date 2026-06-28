#include "gpu/visual_graph.h"

namespace vivid {

// ---- op shaders (moved here from main; the ops now belong to the graph) ----

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

// ----------------------------------------------------------------------------

bool VisualGraph::init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat fmt,
                       uint32_t rtW, uint32_t rtH) {
    dev_ = device; q_ = queue; fmt_ = fmt; rtW_ = rtW; rtH_ = rtH;
    if (!plasma_.init(device, queue, fmt, kPlasmaGLSL)) return false;
    if (!feedback_.init(device, queue, fmt, kFeedbackGLSL, 2)) return false;
    if (!blur_.init(device, queue, fmt, kBlurGLSL, 1)) return false;
    if (!blit_.init(device, queue, fmt, kBlitGLSL, 1)) return false;
    hist_[0].init(device, rtW, rtH, fmt);
    hist_[1].init(device, rtW, rtH, fmt);
    chain_ = { { VOp::Plasma }, { VOp::Feedback }, { VOp::Blur } };  // default chain
    return true;
}

void VisualGraph::ensure_rts(size_t n) {
    if (rts_.size() == n) return;
    for (auto& r : rts_) r.release();
    rts_.clear();
    rts_.resize(n);
    for (auto& r : rts_) r.init(dev_, rtW_, rtH_, fmt_);
}

void VisualGraph::render(WGPUCommandEncoder enc, WGPUTextureView screen,
                         float vx, float vy, float vw, float vh, float time,
                         const float* pu, float decay, float radius, WGPUTextureView video_tex) {
    if (chain_.empty()) return;
    ensure_rts(chain_.size());
    const float rtw = static_cast<float>(rtW_), rth = static_cast<float>(rtH_);

    for (size_t i = 0; i < chain_.size(); ++i) {
        WGPUTextureView in0 = (i > 0) ? rts_[i - 1].view : nullptr;
        switch (chain_[i].op) {
            case VOp::Plasma:
                plasma_.render(enc, rts_[i].view, 0, 0, rtw, rth, time, pu, /*clear*/true);
                break;
            case VOp::Video: {
                WGPUTextureView in[1] = { video_tex };
                blit_.render(enc, rts_[i].view, 0, 0, rtw, rth, /*clear*/true, in, 1, time, nullptr, 0);
                break;
            }
            case VOp::Feedback: {
                RenderTarget& cur = hist_[histCur_]; RenderTarget& prev = hist_[histCur_ ^ 1];
                WGPUTextureView fin[2] = { in0 ? in0 : rts_[i].view, prev.view };
                feedback_.render(enc, cur.view, 0, 0, rtw, rth, /*clear*/true, fin, 2, time, &decay, 1);
                WGPUTextureView cin[1] = { cur.view };
                blit_.render(enc, rts_[i].view, 0, 0, rtw, rth, /*clear*/true, cin, 1, time, nullptr, 0);
                histCur_ ^= 1;
                break;
            }
            case VOp::Blur: {
                WGPUTextureView bin[1] = { in0 ? in0 : rts_[i].view };
                blur_.render(enc, rts_[i].view, 0, 0, rtw, rth, /*clear*/true, bin, 1, time, &radius, 1);
                break;
            }
        }
    }
    // Show the last node's output in the viewer sub-rect.
    WGPUTextureView fin[1] = { rts_.back().view };
    blit_.render(enc, screen, vx, vy, vw, vh, /*clear*/false, fin, 1, time, nullptr, 0);
}

void VisualGraph::shutdown() {
    plasma_.shutdown(); feedback_.shutdown(); blur_.shutdown(); blit_.shutdown();
    hist_[0].release(); hist_[1].release();
    for (auto& r : rts_) r.release();
    rts_.clear();
}

}  // namespace vivid
