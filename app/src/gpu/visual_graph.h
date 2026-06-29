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
    int id = 0;              // stable identity (params + mappings + persistence key off this)
    float params[4] = {};    // per-node resolved param values (Plasma 0..3; Feedback/Blur [0])
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
    int  add_node(VOp op);                 // returns new node index (assigns a fresh id)
    void load_node(VOp op, int id);        // append with a persisted id (for load)
    void remove_node(int i);               // (Output cannot be removed)
    void clear_nodes() { nodes_.clear(); next_id_ = 0; ensure_resources(0); }   // for load
    void set_input(int node, int input);   // wire input's output -> node's texture input
    int  output_index() const;             // index of the ACTIVE Output node, or -1
    void set_active_output(int idx);       // make node idx (if an Output) drive the viewer
    int  active_output_id() const { return active_output_id_; }

    // Generator convenience (V key / generator-node click): toggle the first
    // generator node between Plasma and Video.
    void set_generator(VOp g);
    VOp  generator() const;

    // Params are read per-node from each VisualNode::params (set by NodeGraph::apply_params).
    void render(WGPUCommandEncoder enc, WGPUTextureView screen,
                float vx, float vy, float vw, float vh, float time,
                WGPUTextureView video_tex);

    // A node's last-rendered output view (for live thumbnails via Renderer2D's
    // textured-quad path). nullptr for Output / out-of-range. Aspect for letterbox.
    WGPUTextureView node_view(int idx) const {
        if (idx < 0 || idx >= static_cast<int>(rts_.size())) return nullptr;
        if (idx < static_cast<int>(nodes_.size()) && nodes_[idx].op == VOp::Output) return nullptr;
        return rts_[idx].view;
    }
    float rt_aspect() const { return rtH_ ? static_cast<float>(rtW_) / static_cast<float>(rtH_) : 1.f; }

private:
    void ensure_resources(size_t n);

    WGPUDevice        dev_ = nullptr;
    WGPUQueue         q_   = nullptr;
    WGPUTextureFormat fmt_ = WGPUTextureFormat_Undefined;
    uint32_t          rtW_ = 0, rtH_ = 0;

    std::vector<VisualNode>   nodes_;
    int                       next_id_ = 0;          // monotonic node-id allocator
    int                       active_output_id_ = -1; // which Output drives the viewer
    std::vector<RenderTarget> rts_;          // node output (parallel to nodes_)
    std::vector<RenderTarget> histA_, histB_;// per-node feedback history
    std::vector<int>          histCur_;

    ShaderOp plasma_;
    EffectOp feedback_, blur_, blit_;
};

}  // namespace vivid
