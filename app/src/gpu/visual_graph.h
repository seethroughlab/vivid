#pragma once
#include <webgpu/webgpu.h>
#include "gpu/shader_op.h"
#include "gpu/effect_op.h"
#include "gpu/render_target.h"
#include <vector>
#include <cstdint>

namespace vivid {

enum class VOp { Plasma, Video, Feedback, Blur };

struct VisualNode {
    VOp   op;
    float x = 0.f, y = 0.f;   // editor position (P21)
};

// The composable visuals chain (P20): a sequence of op-nodes — a generator
// (Plasma or Video) followed by effects — each rendering into its own
// RenderTarget; a final blit shows the last node in the viewer. Replaces the
// hardcoded plasma->feedback->blur chain that lived inline in main.
class VisualGraph {
public:
    bool init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat fmt, uint32_t rtW, uint32_t rtH);
    void shutdown();

    std::vector<VisualNode>&       nodes()       { return chain_; }
    const std::vector<VisualNode>& nodes() const { return chain_; }
    void set_generator(VOp g) { if (!chain_.empty()) chain_[0].op = g; }
    VOp  generator() const { return chain_.empty() ? VOp::Plasma : chain_[0].op; }

    // Render the chain into `screen` at the viewer sub-rect. plasma_uniforms = 4
    // floats (warp/hue/density/glow); video_tex feeds a Video generator.
    void render(WGPUCommandEncoder enc, WGPUTextureView screen,
                float vx, float vy, float vw, float vh, float time,
                const float* plasma_uniforms, float feedback_decay, float blur_radius,
                WGPUTextureView video_tex);

private:
    void ensure_rts(size_t n);

    WGPUDevice        dev_ = nullptr;
    WGPUQueue         q_   = nullptr;
    WGPUTextureFormat fmt_ = WGPUTextureFormat_Undefined;
    uint32_t          rtW_ = 0, rtH_ = 0;

    std::vector<VisualNode>   chain_;
    std::vector<RenderTarget> rts_;      // one output RT per chain node
    RenderTarget              hist_[2];  // feedback ping-pong history
    int                       histCur_ = 0;

    ShaderOp plasma_;
    EffectOp feedback_, blur_, blit_;
};

}  // namespace vivid
