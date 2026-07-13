#pragma once
#include <webgpu/webgpu.h>
#include "gpu/effect_op.h"
#include "gpu/render_target.h"
#include "gpu/op_runtime.h"
#include "gpu/output_format.h"   // FitMode + the output size presets (owned by the Output node)
#include <string>
#include <vector>
#include <cstdint>

namespace vivid {

enum class VOp { Plasma, Video, Feedback, Blur, Output };

// op_type <-> the legacy VOp enum. New node identity comes from operator names;
// the enum remains only where older UI/persistence code still classifies built-ins.
const char* vop_name(VOp op);
VOp         vop_from_name(const std::string& name);

// A node in the rewireable visuals chain. Each node hosts an operator instance
// (the lifted ABI); `op_type` is its registry name. `input` is the index of the
// node whose output texture feeds it (-1 = unconnected). The legacy `op` mirror remains
// only for built-in type classification; parameter storage is descriptor-sized.
struct VisualNode {
    std::string op_type;        // registry key — the source of truth
    OpInstance  inst;           // the hosted operator (move-only)
    VOp   op = VOp::Plasma;     // legacy mirror of op_type
    std::vector<int> inputs;    // texture input edges by port (-1 = unconnected); inputs[0]=A, [1]=B, ...
    int   id = 0;               // stable identity (params + mappings + persistence)
    std::vector<float> params;  // resolved param values (collect_params order 0..n-1)
    std::vector<float> base;    // manual base values (inspector); resolved = clamp(base + mod)
    std::vector<std::string> file_params;  // FILE/TEXT param string values (parallel to params; non-file slots empty)
    std::string asset;          // optional project-relative asset (a .glsl for CustomShader)

    // Port-indexed input-edge access; out-of-range reads return -1 (unconnected).
    int  in(int port) const { return (port >= 0 && port < static_cast<int>(inputs.size())) ? inputs[port] : -1; }
    void set_in(int port, int src) {
        if (port < 0) return;
        if (port >= static_cast<int>(inputs.size())) inputs.resize(port + 1, -1);
        inputs[port] = src;
    }
};

// The composable visuals graph: nodes connected by texture edges, terminating in
// an Output node. The executor walks the input chain back from Output, runs each
// node's operator (process_gpu) into its own RenderTarget, and blits the node
// feeding Output to the viewer.
class VisualGraph {
public:
    bool init(WGPUDevice device, WGPUQueue queue, WGPUTextureFormat fmt,
              uint32_t rtW, uint32_t rtH, OpRegistry* registry);
    void shutdown();

    std::vector<VisualNode>&       nodes()       { return nodes_; }
    const std::vector<VisualNode>& nodes() const { return nodes_; }
    int  add_node(const std::string& type);   // returns new node index (fresh id)
    int  add_node(VOp op) { return add_node(vop_name(op)); }
    void load_node(const std::string& type, int id);   // append with a persisted id
    void load_node(VOp op, int id) { load_node(vop_name(op), id); }
    void remove_node(int i);                   // (Output cannot be removed)
    void clear_nodes() { nodes_.clear(); next_id_ = 0; ensure_resources(0); }
    void reset_to_default();                   // the out-of-box Plasma->Feedback->Blur->Output chain
    void set_input(int node, int port, int src);   // wire src's output -> node's texture input `port`
    void set_input(int node, int src)   { set_input(node, 0, src); }   // back-compat: primary input (port A)
    void set_input_b(int node, int src) { set_input(node, 1, src); }   // back-compat: second input (port B)
    int  output_index() const;                 // index of the ACTIVE Output node, or -1
    void set_active_output(int idx);
    int  active_output_id() const { return active_output_id_; }

    void set_generator(VOp g);                 // toggle first generator Plasma<->Video
    VOp  generator() const;
    OpRegistry* registry() const { return reg_; }   // the op catalog (for the Tab chooser)

    // Base directory that a node's relative `asset` (e.g. a CustomShader .glsl) resolves
    // against — the loaded project folder. Cleared for a fresh/default session.
    void set_asset_dir(const std::string& dir) { asset_dir_ = dir; }
    const std::string& asset_dir() const { return asset_dir_; }

    // Hot-reload: destroy / recreate the OpInstance of every node whose op_type is
    // `type`. release MUST run before the loader dlcloses the old dylib (so the old
    // instances destruct against still-loaded code); rebuild runs after the swap,
    // recreating from the new dylib. Node base/params live on the node → preserved.
    int release_op_instances(const std::string& type);   // returns affected node count
    int rebuild_op_instances(const std::string& type);
    // Re-instantiate node i as a different registered operator type (id/input/pos kept).
    bool set_node_op_type(int i, const std::string& type) {
        return (i >= 0 && i < static_cast<int>(nodes_.size())) ? make_instance(nodes_[i], type) : false;
    }

    // Run the chain: every node's operator renders into its own RenderTarget. Does NOT touch the
    // screen — presenting is a separate step (ADR-0014), so the caller can draw the node graph
    // first and blit the output OVER it (the floating preview sits above the canvas).
    void run_chain(WGPUCommandEncoder enc, float time, WGPUTextureView video_tex);
    // Blit the already-rendered output FBO (from the last run_chain()) into a view at a rect —
    // the floating preview, or a pop-out window's surface. Letterboxes per the Output node's fit
    // mode, so every surface shows the output's true aspect. Does NOT re-run the graph.
    // surf_w/surf_h are the TARGET's pixel size: the rect is clamped to them, because wgpu aborts
    // the process (not just the draw) on a viewport/scissor that escapes the render target.
    void present_to(WGPUCommandEncoder enc, WGPUTextureView view,
                    float vx, float vy, float vw, float vh,
                    float surf_w, float surf_h, float time, bool clear);

    WGPUTextureView node_view(int idx) const {
        if (idx < 0 || idx >= static_cast<int>(rts_.size())) return nullptr;
        if (idx < static_cast<int>(nodes_.size()) && nodes_[idx].op == VOp::Output) return nullptr;
        return rts_[idx].view;
    }
    float    rt_aspect() const { return rtH_ ? static_cast<float>(rtW_) / static_cast<float>(rtH_) : 1.f; }
    uint32_t rt_w() const { return rtW_; }
    uint32_t rt_h() const { return rtH_; }
    FitMode  fit_mode() const { return fit_; }
    // Resize every node's render target. Idempotent (a no-op when unchanged). Only safe between
    // frames / at the top of run_chain(), before anything in the encoder references an RT.
    void set_rt_size(uint32_t w, uint32_t h);

private:
    void ensure_resources(size_t n);
    bool make_instance(VisualNode& n, const std::string& type);  // create + record inst
    // Pull the output's format (size + fit) off the ACTIVE Output node's params. Run once per
    // frame, first thing in run_chain().
    void apply_output_settings();
    FitMode fit_ = FitMode::Fit;   // derived from the Output node's `fit` param — never a param slot

    WGPUDevice        dev_ = nullptr;
    WGPUQueue         q_   = nullptr;
    WGPUTextureFormat fmt_ = WGPUTextureFormat_Undefined;
    uint32_t          rtW_ = 0, rtH_ = 0;
    OpRegistry*       reg_ = nullptr;
    uint64_t          frame_ = 0;
    std::string       asset_dir_;   // project dir for resolving node.asset (CustomShader .glsl)

    std::vector<VisualNode>   nodes_;
    int                       next_id_ = 0;
    int                       active_output_id_ = -1;
    std::vector<RenderTarget> rts_;          // node output (parallel to nodes_)
    RenderTarget              fallback_;      // black input for disconnected ports
    bool                      fb_cleared_ = false;

    EffectOp blit_;   // final present (node-feeding-Output RT -> viewer)
};

}  // namespace vivid
