#pragma once
#include <webgpu/webgpu.h>
#include "gpu/shader_op.h"
#include "gpu/effect_op.h"
#include "gpu/render_target.h"
#include <vector>
#include <cstdint>

namespace vivid {

enum class VOp { Plasma, Video, Feedback, Blur, Output };

// A node in the rewireable visuals chain. `input` is the index of the node whose
// output texture feeds this node (-1 = unconnected). Generators ignore it; the
// Output node's input is what shows in the viewer.
struct VisualNode {
    VOp op;
    int input = -1;
};

// The composable visuals graph: nodes connected by texture edges, terminating in
// an Output node. The executor walks the input chain back from Output, renders
// each node into its own RenderTarget, and blits the node feeding Output to the
// viewer. Topology is fully user-rewireable; params stay global per op-type.
class VisualGraph {
public:
    bool init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat fmt, uint32_t rtW, uint32_t rtH);
    void shutdown();

    std::vector<VisualNode>&       nodes()       { return nodes_; }
    const std::vector<VisualNode>& nodes() const { return nodes_; }
    int  add_node(VOp op);                 // returns new node index
    void remove_node(int i);               // (Output cannot be removed)
    void clear_nodes() { nodes_.clear(); ensure_resources(0); }   // for load
    void set_input(int node, int input);   // wire input's output -> node's texture input
    int  output_index() const;             // index of the Output node, or -1

    // Generator convenience (V key / generator-node click): toggle the first
    // generator node between Plasma and Video.
    void set_generator(VOp g);
    VOp  generator() const;

    void render(WGPUCommandEncoder enc, WGPUTextureView screen,
                float vx, float vy, float vw, float vh, float time,
                const float* plasma_uniforms, float feedback_decay, float blur_radius,
                WGPUTextureView video_tex);

private:
    void ensure_resources(size_t n);

    WGPUDevice        dev_ = nullptr;
    WGPUQueue         q_   = nullptr;
    WGPUTextureFormat fmt_ = WGPUTextureFormat_Undefined;
    uint32_t          rtW_ = 0, rtH_ = 0;

    std::vector<VisualNode>   nodes_;
    std::vector<RenderTarget> rts_;          // node output (parallel to nodes_)
    std::vector<RenderTarget> histA_, histB_;// per-node feedback history
    std::vector<int>          histCur_;

    ShaderOp plasma_;
    EffectOp feedback_, blur_, blit_;
};

}  // namespace vivid
