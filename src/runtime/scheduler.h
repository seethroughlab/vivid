#pragma once

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include <webgpu/webgpu.h>
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
    std::string node_id;
    OperatorLoader* loader;
    void* instance;
    uint32_t input_port_count;
    uint32_t output_port_count;
    std::vector<float> param_values;
    std::vector<uint8_t> param_lock_flags;  // parallel to param_values
    std::vector<float> input_values;
    std::vector<float> output_values;
    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;
    std::unordered_map<std::string, uint32_t> param_indices;

    // Generation-based cooking
    bool time_dependent = false;
    bool is_gpu = false;
    bool is_audio = false;
    uint64_t generation = 0;
    std::vector<uint32_t> upstream_nodes;       // indices of nodes feeding into this one
    std::vector<uint64_t> upstream_gens_cached;  // generation of each upstream at last cook
    std::vector<float> prev_output_values;

    // Spread data
    std::vector<std::vector<float>> output_spreads;   // [port_idx] → spread data
    std::vector<std::vector<float>> input_spreads;    // [port_idx] → spread data

    // Pre-allocated spread port arrays for process context (avoids per-frame heap allocs)
    std::vector<VividSpreadPort> c_in_spreads;
    std::vector<VividSpreadPort> c_out_spreads;
    std::vector<std::vector<float>> out_spread_buf;

    // Per-node GPU texture
    WGPUTexture      gpu_texture      = nullptr;
    WGPUTextureView  gpu_texture_view = nullptr;
    uint32_t         gpu_tex_width    = 0;
    uint32_t         gpu_tex_height   = 0;
    std::vector<uint32_t> texture_input_port_indices;   // which input ports are GPU_TEXTURE
    std::vector<WGPUTextureView> resolved_tex_inputs;   // filled before process()
    std::vector<WGPUTexture>     resolved_tex_raw;       // raw texture handles (parallel)
    std::vector<uint32_t>        resolved_tex_widths;    // input texture widths
    std::vector<uint32_t>        resolved_tex_heights;   // input texture heights
    bool is_gpu_sink = false;  // GPU domain + texture inputs + no texture outputs

    // File (string) params — separate from float param_values
    std::vector<std::string> file_param_storage;     // owned strings
    std::vector<const char*> file_param_ptrs;        // pointers into storage
    std::unordered_map<std::string, uint32_t> file_param_indices;

    // Error state — set by try/catch in tick(), cleared on reload
    bool errored = false;
    std::string error_message;

    // Per-instance loader for WGSLFilter nodes (owns the loader; ns.loader points here)
    std::unique_ptr<OperatorLoader> owned_loader;
};

struct Wire {
    uint32_t from_node_idx, from_port_idx;
    uint32_t to_node_idx, to_port_idx;
    bool sources_param = false;   // true → from_port_idx indexes into param_values
    bool targets_param = false;   // true → to_port_idx indexes into param_values
    bool is_texture_wire = false; // true → carries GPU_TEXTURE data
    float scale = 1.0f;          // multiplied into propagated values
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
    bool needs_gpu_realloc_ = false;
};

} // namespace vivid
