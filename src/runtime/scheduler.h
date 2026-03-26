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
