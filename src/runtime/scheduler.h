#pragma once

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/runtime_core.h"
#include "runtime/graph_compiler.h"
#include "runtime/frame_executor.h"
#include "operator_api/gpu_types.h"
#include <webgpu/webgpu.h>
#include <filesystem>
#include <string>
#include <vector>
#include <functional>

namespace vivid {

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

    // Audio synchronization — call these around tick() to bridge cadence worlds.
    // pre_tick_audio_sync:  pull audio analysis into frame nodes, update audio sources.
    // post_tick_audio_sync: push frame outputs to audio nodes.
    // Safe to call even when there are no audio operators (they become no-ops).
    void pre_tick_audio_sync(double time);
    void post_tick_audio_sync();
    void shutdown();
    bool has_gpu_operators() const;
    bool has_audio_operators() const;
    void allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                               WGPUTextureFormat format,
                               WGPUTextureUsage extra_usage = 0);
    int find_gpu_sink() const;  // returns first GPU sink node index, or -1
    int find_effective_gpu_sink() const;  // solo-aware: returns soloed node if it has texture output

    // Solo mode — session-only, not serialized.
    // Delegates to RuntimeCore (which also syncs to CadenceBridge for audio).
    void set_solo(int node_idx);
    int solo_node_idx() const { return core_.solo_node_idx; }
    bool is_solo_active() const { return core_.is_solo_active(); }
    const std::vector<bool>& solo_active_set() const { return core_.solo_active_set; }

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

    // Shared runtime state — accessible by AudioEngine without const_cast.
    RuntimeCore& core() { return core_; }
    const RuntimeCore& core() const { return core_; }

    // Convenience accessors (delegate to core_).
    CompiledGraph* compiled_graph() { return core_.compiled_graph.get(); }
    const CompiledGraph* compiled_graph() const { return core_.compiled_graph.get(); }
    CadenceBridge& cadence_bridge() { return core_.cadence_bridge; }
    const CadenceBridge& cadence_bridge() const { return core_.cadence_bridge; }
    FrameExecutor& frame_executor() { return frame_executor_; }
    const FrameExecutor& frame_executor() const { return frame_executor_; }

private:
    std::string operators_src_dir_;
    std::filesystem::path graph_base_dir_;
    bool needs_gpu_realloc_ = false;

    RuntimeCore core_;
    FrameExecutor frame_executor_;
};

} // namespace vivid
