#pragma once

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "operator_api/gpu_types.h"
#include <webgpu/webgpu.h>
#include <filesystem>
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

    // ── Opaque data ports (package-defined types, e.g. 3D scene fragments) ────
    std::vector<void*> gpu_data_outputs;          // [data_output_slot_idx], captured each tick
    std::vector<void*> output_data_buf;           // pre-allocated buffer passed to operator via ctx
    std::vector<uint32_t> data_input_port_indices;
    std::vector<uint32_t> data_output_port_indices; // which output port indices are DATA type
    std::vector<uint32_t> string_input_port_indices;
    std::vector<uint32_t> string_spread_input_port_indices;
    std::vector<void*> resolved_data_inputs;
    bool has_texture_output = false;
    bool has_data_output = false;
    bool has_string_output = false;
    bool has_string_spread_output = false;

    // ── GPU buffer ports ──────────────────────────────────────────────────────
    std::vector<uint32_t>        buffer_input_port_indices;
    std::vector<VividGpuBuffer*> resolved_buffer_inputs;
    std::vector<uint32_t>        buffer_output_port_indices;
    std::vector<VividGpuBuffer*> gpu_buffer_outputs;
    std::vector<VividGpuBuffer*> output_buffer_buf;

    // ── GPU mesh ports ────────────────────────────────────────────────────────
    std::vector<uint32_t>     mesh_input_port_indices;
    std::vector<VividMesh*>   resolved_mesh_inputs;
    std::vector<uint32_t>     mesh_output_port_indices;
    std::vector<VividMesh*>   gpu_mesh_outputs;
    std::vector<VividMesh*>   output_mesh_buf;

    // ── GPU compute ports ─────────────────────────────────────────────────────
    std::vector<uint32_t>            compute_input_port_indices;
    std::vector<VividComputeBuffer*> resolved_compute_inputs;
    std::vector<uint32_t>            compute_output_port_indices;
    std::vector<VividComputeBuffer*> gpu_compute_outputs;
    std::vector<VividComputeBuffer*> output_compute_buf;

    // ── File (string) params — separate from float param_values ──────────────
    std::vector<std::string> file_param_storage;     // owned strings
    std::vector<const char*> file_param_ptrs;        // pointers into storage
    std::unordered_map<std::string, uint32_t> file_param_indices;
    std::vector<uint8_t> file_param_is_path;         // 1=file path semantics, 0=plain text

    // ── Error state — set by try/catch in tick(), cleared on reload ───────────
    bool errored = false;
    std::string error_message;

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
    bool is_texture_wire = false; // true → carries GPU_TEXTURE data
    bool is_data_wire    = false; // true → carries VIVID_PORT_DATA
    bool is_string_wire = false;  // true → carries CONTROL_STRING
    bool is_string_spread_wire = false; // true → carries CONTROL_STRING_SPREAD
    bool is_buffer_wire  = false; // true → carries VIVID_PORT_GPU_BUFFER
    bool is_mesh_wire    = false; // true → carries VIVID_PORT_GPU_MESH
    bool is_compute_wire = false; // true → carries VIVID_PORT_GPU_COMPUTE
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
    void shutdown();
    const std::vector<NodeState>& nodes() const { return nodes_; }
    std::vector<NodeState>& nodes_mut() { return nodes_; }
    const std::vector<Wire>& wires() const { return wires_; }
    NodeState* find_node_mut(const std::string& id);
    bool has_gpu_operators() const;
    bool has_audio_operators() const;
    void allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                               WGPUTextureFormat format,
                               WGPUTextureUsage extra_usage = 0);
    int find_gpu_sink() const;  // returns first GPU sink node index, or -1
    void inject_external_output(uint32_t node_idx, uint32_t port_idx, float value);
    void inject_external_spread(uint32_t node_idx, uint32_t port_idx,
                                const float* data, uint32_t length);

    // Hot-reload: destroy old instances, swap dylib, recreate with param reconciliation
    bool reload_operator(const std::string& type_name, OperatorRegistry& registry,
                         const std::string& new_dylib_path);
    std::string type_name(uint32_t node_idx) const;

    bool gpu_sink_source_size(int sink_idx, uint32_t& w, uint32_t& h) const;
    WGPUTexture gpu_sink_source_texture(int sink_idx) const;
    bool is_audio_type(const std::string& type_name) const;

    void set_operators_src_dir(const std::string& dir) { operators_src_dir_ = dir; }
    const std::string& operators_src_dir() const { return operators_src_dir_; }

    bool needs_gpu_realloc() const { return needs_gpu_realloc_; }
    void clear_gpu_realloc() { needs_gpu_realloc_ = false; }

private:
    void init_node_state(NodeState& ns, const VividOperatorDescriptor* desc,
                         const std::unordered_map<std::string, float>* param_overrides,
                         const std::unordered_map<std::string, std::string>* string_overrides = nullptr);

    std::vector<NodeState> nodes_;
    std::vector<Wire> wires_;
    std::string operators_src_dir_;
    std::filesystem::path graph_base_dir_;
    bool needs_gpu_realloc_ = false;
};

} // namespace vivid
