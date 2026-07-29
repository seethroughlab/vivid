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

// A node in the rewireable visuals chain. Each node hosts an operator instance
// (the lifted ABI); `op_type` is its registry name — the ONLY identity a node has.
// `inputs` are the indices of the nodes whose output textures feed its ports (-1 = unconnected);
// parameter storage is descriptor-sized.
//
// ADR-0016 / S2: a node classifies itself from FACTS — its descriptor and its type name.
// The old `VOp` enum mirror is gone: it mapped every unrecognized op name to Plasma, so
// Kaleidoscope, Tint and Displace were all silently "generators", and that classification
// drove generator detection, set_generator() and the seeded master.level -> glow mapping.
// An enum cannot classify an open catalog; a shader library would have inherited the bug.
struct VisualNode {
    std::string op_type;        // registry key — the source of truth
    OpInstance  inst;           // the hosted operator (move-only)
    std::vector<int> inputs;    // texture input edges by port (-1 = unconnected); inputs[0]=A, [1]=B, ...
    int   id = 0;               // stable identity (params + mappings + persistence)
    std::vector<float> params;  // resolved param values (collect_params order 0..n-1)
    std::vector<float> base;    // manual base values (inspector); resolved = clamp(base + mod)
    std::vector<std::string> file_params;  // FILE/TEXT param string values (parallel to params; non-file slots empty)
    uint64_t file_param_generation = 0;    // host-triggered same-path reload signal for FILE/TEXT consumers
    std::string asset;          // optional project-relative asset (a .glsl for CustomShader)
    // Base values captured BY PARAM NAME across a rebuild (an operator hot-reload, or a shader
    // whose header changed). Indices move when a reload adds, removes or reorders a param, so a
    // name is the only thing worth carrying over. Consumed by make_instance(), then cleared.
    std::vector<std::pair<std::string, float>> stash;

    // The one op the host itself must recognize: the chain's sink. A host CONTRACT, not
    // classification, which is why this name appears here and no others do. (Video used to be the
    // second — the host injected its decoded frame — but Video is now an ordinary self-decoding
    // source op that owns its own file + decoder, so there is no special case for it.)
    bool is_output() const { return op_type == "Output"; }

    // BROKEN: the node's op type never resolved to a real operator, so it renders nothing and
    // drops out of the chain. The Output sink legitimately carries no operator, so it is never
    // "missing". The single source of truth for both the node badge (error()) and the health
    // rollup (VisualGraph::missing_op_count) — ADR-0019.
    bool op_missing() const { return !inst.op && !is_output(); }

    // A SOURCE heads a chain: it makes an image rather than transforming one. Read off the
    // node's own descriptor — no texture inputs to transform — so it is automatically true of
    // a package op (incl. Video/Webcam), a shader file, or anything else the catalog grows.
    bool is_source() const { return inst.op && inst.input_port_count == 0; }

    void stash_params() {   // call BEFORE dropping the instance: param_ptrs holds the names
        stash.clear();
        for (size_t i = 0; i < inst.param_ptrs.size() && i < base.size(); ++i)
            if (inst.param_ptrs[i] && inst.param_ptrs[i]->name)
                stash.emplace_back(inst.param_ptrs[i]->name, base[i]);
    }

    // What is wrong with this node right now, if anything — e.g. a shader file that will not
    // compile. Empty when the node is healthy. The node keeps rendering (a source falls back to
    // black, a filter passes its input through); this is how the UI says so instead of leaving
    // the user staring at a black frame wondering what they broke.
    std::string error() const;

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
    int  missing_op_count() const;             // nodes whose op type isn't a real operator (ADR-0019)
    std::vector<int> missing_op_node_indices() const;   // indices of those broken nodes (diagnostics panel)
    int  add_node(const std::string& type);   // returns new node index (fresh id)
    void load_node(const std::string& type, int id);   // append with a persisted id
    void remove_node(int i);                   // (Output cannot be removed)
    void clear_nodes() { nodes_.clear(); next_id_ = 0; ensure_resources(0); }
    void reset_to_default();                   // a clean canvas: just the Output sink (no baked-in content)
    void set_input(int node, int port, int src);   // wire src's output -> node's texture input `port`
    void set_input(int node, int src)   { set_input(node, 0, src); }   // back-compat: primary input (port A)
    void set_input_b(int node, int src) { set_input(node, 1, src); }   // back-compat: second input (port B)
    int  output_index() const;                 // index of the ACTIVE Output node, or -1
    void set_active_output(int idx);
    int  active_output_id() const { return active_output_id_; }

    // Would a node of this type be a source? (Asked of the REGISTRY, before instantiating.)
    bool type_is_source(const std::string& type) const;
    // Re-instantiate the first SOURCE node (the head of the chain — see VisualNode::is_source)
    // as `type`. False if there is no source node, or `type` is not itself a source type —
    // swapping the chain's head for a filter would leave the graph with nothing to filter.
    bool set_generator(const std::string& type);
    std::string generator() const;             // the first source's op type, "" if none
    OpRegistry* registry() const { return reg_; }   // the op catalog (for the Tab chooser)

    // Base directory that a node's relative `asset` (e.g. a CustomShader .glsl) resolves
    // against — the loaded project folder. Cleared for a fresh/default session.
    void set_asset_dir(const std::string& dir) { asset_dir_ = dir; }
    const std::string& asset_dir() const { return asset_dir_; }
    uint64_t bump_file_param_generation(int node);
    int bump_all_file_param_generations();

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
    void run_chain(WGPUCommandEncoder enc, float time);
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
        if (idx < static_cast<int>(nodes_.size()) && nodes_[idx].is_output()) return nullptr;
        return rts_[idx].view;
    }
    float    rt_aspect() const { return rtH_ ? static_cast<float>(rtW_) / static_cast<float>(rtH_) : 1.f; }
    uint32_t rt_w() const { return rtW_; }
    uint32_t rt_h() const { return rtH_; }
    FitMode  fit_mode() const { return fit_; }
    // The ACTIVE Output node's params, by NAME (index would be brittle as params are added).
    // These are the output's identity: size/aspect/fit + where it is shown (preview / pop-out).
    // Reads/writes the BASE value — the manual, persisted one, not base+modulation.
    float output_param(const char* name, float def = 0.f) const;
    void  set_output_param(const char* name, float v);
    // Resize every node's render target. Idempotent (a no-op when unchanged). Only safe between
    // frames / at the top of run_chain(), before anything in the encoder references an RT.
    void set_rt_size(uint32_t w, uint32_t h);

    // ADR-0024 Phase 6: read the ACTIVE OUTPUT's last-rendered pixels back to CPU as tightly-packed
    // RGBA8 (row-major, top-left origin; BGRA source is swizzled to RGBA). Returns false when there is
    // no output node or nothing feeds it — i.e. a blank frame. MAIN THREAD ONLY (owns device/queue);
    // it submits a copy + blocks briefly on the GPU readback, so it is not RT-safe and not per-frame.
    bool read_output_pixels(std::vector<uint8_t>& out_rgba, uint32_t& out_w, uint32_t& out_h);

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
    float             last_chain_time_ = -1.f;   // for the per-frame delta_time passed to ops (run_chain)
    std::string       asset_dir_;   // project dir for resolving node.asset (CustomShader .glsl)

    std::vector<VisualNode>   nodes_;
    int                       next_id_ = 0;
    int                       active_output_id_ = -1;
    std::vector<RenderTarget> rts_;          // node output (parallel to nodes_)
    // The VALUE channel analogue of rts_ (custom-ref ports, e.g. a VividMesh): per node, one slot per
    // custom-ref OUTPUT port. A producer op writes a pointer into its slot in process_gpu; a
    // downstream op reads its upstream's slot — resolved the same topo-ordered pass. The producer
    // owns the pointed-to value (its wgpu buffers are op members that outlive the frame). See run_chain.
    std::vector<std::vector<void*>> published_custom_;
    RenderTarget              fallback_;      // black input for disconnected ports
    bool                      fb_cleared_ = false;

    EffectOp blit_;   // final present (node-feeding-Output RT -> viewer)
};

}  // namespace vivid
