#pragma once

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/compiled_graph.h"
#include "runtime/graph_compiler.h"
#include "runtime/frame_executor.h"
#include "runtime/cadence_bridge.h"
#include "operator_api/gpu_types.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace vivid {

enum ParamLockFlags : uint8_t {
    PARAM_LOCK_NONE    = 0,
    PARAM_LOCK_WIRES   = 1,
    PARAM_LOCK_PRESETS = 2,
    PARAM_LOCK_ALL     = 3,
};

struct NodeState {
    // ── Identity & lifecycle ──────────────────────────────────────────────────
    std::string node_id;
    std::string type_name;
    OperatorLoader* loader;
    void* instance;

    // ── Port configuration (set once at build() time) ─────────────────────────
    uint32_t input_port_count;
    uint32_t output_port_count;
    std::vector<float> param_values;
    std::vector<uint8_t> param_lock_flags;  // parallel to param_values
    std::vector<float> input_values;
    std::vector<float> output_values;
    std::vector<std::string> input_string_values;
    std::vector<std::string> output_string_values;
    std::vector<const char*> c_input_string_values;
    std::vector<const char*> c_output_string_values;
    std::vector<VividPortType> input_port_types;
    std::vector<VividPortType> output_port_types;
    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;
    std::unordered_map<std::string, uint32_t> analysis_output_port_indices; // rms/peak/waveform
    std::unordered_map<std::string, uint32_t> param_indices;

    // ── Generation-based cooking (per-tick evaluation memoization) ────────────
    bool time_dependent = false;
    bool is_gpu = false;
    bool is_audio = false;
    uint64_t generation = 0;
    std::vector<uint32_t> upstream_nodes;       // indices of nodes feeding into this one
    bool processed_this_tick = false;
    uint64_t last_processed_gen = 0;
    std::vector<float> prev_output_values;

    // ── Spread data (per-tick values for spread ports) ────────────────────────
    std::vector<std::vector<float>> output_spreads;   // [port_idx] → spread data
    std::vector<std::vector<float>> input_spreads;    // [port_idx] → spread data
    std::vector<std::vector<std::string>> output_string_spreads; // [port_idx] → string spread
    std::vector<std::vector<std::string>> input_string_spreads;  // [port_idx] → string spread

    // Pre-allocated staging buffers for the VividProcessContext passed to operators each tick.
    // Sized to kMaxSpreadCapacity at build() time; operators write into these in-place.
    std::vector<VividSpreadPort> c_in_spreads;
    std::vector<VividSpreadPort> c_out_spreads;
    std::vector<std::vector<float>> out_spread_buf;  // backing storage for c_out_spreads[i].data
    std::vector<VividStringSpreadPort> c_in_string_spreads;
    std::vector<VividStringSpreadPort> c_out_string_spreads;
    std::vector<std::vector<const char*>> in_string_spread_ptrs;
    std::vector<std::vector<const char*>> out_string_spread_ptr_buf;

    // ── GPU resources (allocated by allocate_gpu_textures()) ─────────────────
    WGPUTexture      gpu_texture      = nullptr;
    WGPUTextureView  gpu_texture_view = nullptr;
    uint32_t         gpu_tex_width    = 0;
    uint32_t         gpu_tex_height   = 0;
    bool             gpu_tex_inherited = false;
    std::vector<uint32_t> texture_input_port_indices;   // which input ports are GPU_TEXTURE
    std::vector<WGPUTextureView> resolved_tex_inputs;   // filled before process()
    std::vector<WGPUTexture>     resolved_tex_raw;       // raw texture handles (parallel)
    std::vector<uint32_t>        resolved_tex_widths;    // input texture widths
    std::vector<uint32_t>        resolved_tex_heights;   // input texture heights
    // Invariant: is_gpu_sink ↔ GPU domain node with ≥1 GPU_TEXTURE input and 0 GPU_TEXTURE outputs.
    // The GPU sink is the terminal node of the GPU subgraph; its primary texture feeds the display.
    bool is_gpu_sink = false;

    // Auxiliary texture outputs (2nd, 3rd... GPU_TEXTURE output ports), scheduler-allocated.
    std::vector<int32_t>         aux_texture_output_port_indices; // output port idx per aux slot
    std::vector<WGPUTexture>     aux_gpu_textures;
    std::vector<WGPUTextureView> aux_gpu_texture_views;

    // ── Custom ports (operator-defined types with high bit set in VividPortType) ──
    std::vector<uint32_t> custom_input_port_indices;
    std::vector<void*>    resolved_custom_inputs;
    std::vector<uint32_t> custom_output_port_indices;
    std::vector<void*>    custom_outputs;       // captured each tick (operator's write-back)
    std::vector<void*>    custom_output_buf;    // pre-allocated buffer passed to operator via ctx

    std::vector<uint32_t> string_input_port_indices;
    std::vector<uint32_t> string_spread_input_port_indices;
    bool has_texture_output = false;
    bool has_string_output = false;
    bool has_string_spread_output = false;

    // ── File (string) params — separate from float param_values ──────────────
    std::vector<std::string> file_param_storage;     // owned strings
    std::vector<const char*> file_param_ptrs;        // pointers into storage
    std::unordered_map<std::string, uint32_t> file_param_indices;
    std::vector<uint8_t> file_param_is_path;         // 1=file path semantics, 0=plain text

    // ── Error state — set by try/catch in tick(), cleared on reload ───────────
    bool errored = false;
    std::string error_message;

    // Transient GPU shader error — cleared each tick, set by gpu_ctx.operator_errored.
    // Unlike ns.errored, this does NOT permanently block processing.
    bool        gpu_shader_error     = false;
    std::string gpu_shader_error_msg;

    // Per-instance loader for WGSLFilter nodes (owns the loader; ns.loader points here)
    std::unique_ptr<OperatorLoader> owned_loader;

    // Placeholder state when a graph references an operator type that is not available.
    bool missing_operator = false;
};

struct Wire {
    uint32_t from_node_idx, from_port_idx;
    uint32_t to_node_idx, to_port_idx;
    bool sources_param = false;   // true → from_port_idx indexes into param_values
    bool targets_param = false;   // true → to_port_idx indexes into param_values
    bool sources_file_param = false; // true → from_port_idx indexes file_param_storage
    bool targets_file_param = false; // true → string wire into file_param_storage
    uint32_t from_file_param_idx = 0; // index into file_param_storage (when sources_file_param)
    uint32_t to_file_param_idx = 0;   // index into file_param_storage (when targets_file_param)
    bool is_texture_wire = false; // true → carries VIVID_PORT_TEXTURE
    bool is_custom_wire  = false; // true → carries a custom port type (high bit set)
    bool is_string_wire = false;  // true → carries VIVID_PORT_STRING
    bool is_string_spread_wire = false; // true → carries VIVID_PORT_STRING_SPREAD
    float from_min = 0.0f, from_max = 1.0f;
    float to_min   = 0.0f, to_max  = 1.0f;
    bool  clamp    = false;
};

// Optional callback invoked after each GPU node's process()
// texture_view is the node's per-node output texture (for thumbnail capture)
using PostNodeFn = std::function<void(uint32_t node_idx, const std::string& node_id,
                                      WGPUTextureView texture_view)>;

class Scheduler {
public:
    bool build(const Graph& graph, OperatorRegistry& registry);
    void tick(double time, double delta_time, uint64_t frame, void* gpu_state = nullptr,
              PostNodeFn on_gpu_node = nullptr,
              const VividInputState* input = nullptr);
    // Sync CompiledNode results → NodeState for inspector/UI/snapshot consumers.
    void sync_to_nodestate();
    // Sync a single NodeState's params to its CompiledNode.
    // Use when modifying param_values on NodeState directly (e.g. in tests).
    void sync_node_to_compiled(const std::string& node_id);
    void shutdown();
    const std::vector<NodeState>& nodes() const { return nodes_; }
    std::vector<NodeState>& nodes_mut() { return nodes_; }
    const std::vector<Wire>& wires() const { return wires_; }
    const NodeState* find_node(const std::string& id) const;
    NodeState* find_node_mut(const std::string& id);
    bool has_gpu_operators() const;
    bool has_audio_operators() const;
    void allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                               WGPUTextureFormat format,
                               WGPUTextureUsage extra_usage = 0);
    int find_gpu_sink() const;  // returns first GPU sink node index, or -1
    int find_effective_gpu_sink() const;  // solo-aware: returns soloed node if it has texture output

    // Solo mode — session-only, not serialized
    void set_solo(int node_idx);  // -1 to clear
    int solo_node_idx() const { return solo_node_idx_; }
    bool is_solo_active() const { return solo_node_idx_ >= 0; }
    const std::vector<bool>& solo_active_set() const { return solo_active_set_; }

    // Hot-reload: destroy old instances, swap dylib, recreate with param reconciliation
    bool reload_operator(const std::string& type_name, OperatorRegistry& registry,
                         const std::string& new_dylib_path);
    std::string type_name(uint32_t node_idx) const;

    // Public wrapper for init_node_state — used by control_server for package rebuild
    void reinit_node(NodeState& ns, const VividOperatorDescriptor* desc,
                     const std::unordered_map<std::string, float>* param_overrides,
                     const std::unordered_map<std::string, std::string>* string_overrides = nullptr) {
        init_node_state(ns, desc, param_overrides, string_overrides);
    }

    bool gpu_sink_source_size(int sink_idx, uint32_t& w, uint32_t& h) const;
    WGPUTexture gpu_sink_source_texture(int sink_idx) const;
    bool is_audio_type(const std::string& type_name) const;

    void set_operators_src_dir(const std::string& dir) { operators_src_dir_ = dir; }
    const std::string& operators_src_dir() const { return operators_src_dir_; }

    bool needs_gpu_realloc() const { return needs_gpu_realloc_; }
    void clear_gpu_realloc() { needs_gpu_realloc_ = false; }

    // Cadence-aware runtime accessors
    CompiledGraph* compiled_graph() { return compiled_graph_.get(); }
    const CompiledGraph* compiled_graph() const { return compiled_graph_.get(); }
    CadenceBridge& cadence_bridge() { return cadence_bridge_; }
    const CadenceBridge& cadence_bridge() const { return cadence_bridge_; }
    FrameExecutor& frame_executor() { return frame_executor_; }
    const FrameExecutor& frame_executor() const { return frame_executor_; }

private:
    void init_node_state(NodeState& ns, const VividOperatorDescriptor* desc,
                         const std::unordered_map<std::string, float>* param_overrides,
                         const std::unordered_map<std::string, std::string>* string_overrides = nullptr);

    std::vector<NodeState> nodes_;
    std::vector<Wire> wires_;
    std::string operators_src_dir_;
    std::filesystem::path graph_base_dir_;
    WGPUDevice gpu_device_ = nullptr;
    bool needs_gpu_realloc_ = false;

    // Solo mode state (session-only)
    int solo_node_idx_ = -1;
    std::vector<bool> solo_active_set_;

    // Cadence-aware runtime
    std::unique_ptr<CompiledGraph> compiled_graph_;
    FrameExecutor frame_executor_;
    CadenceBridge cadence_bridge_;
};

} // namespace vivid
