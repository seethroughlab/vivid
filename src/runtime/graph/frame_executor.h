#pragma once

#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/lane_state.h"
#include <functional>
#include <string>
#include <vector>

// Forward declarations — use webgpu types from the actual headers
#include <webgpu/webgpu.h>

namespace vivid {

// Optional callback invoked after each GPU node's process()
using PostNodeFn = std::function<void(uint32_t node_idx, const std::string& node_id,
                                      WGPUTextureView texture_view)>;

// ---------------------------------------------------------------------------
// FrameExecutor — processes frame-rate and GPU nodes.
//
// Operates on a CompiledGraph, iterating frame_order and propagating
// values via frame_direct_edges.
// ---------------------------------------------------------------------------

class FrameExecutor {
public:
    void tick(CompiledGraph& cg, double time, double delta_time, uint64_t frame,
              void* gpu_state = nullptr, PostNodeFn on_gpu_node = nullptr,
              const VividInputState* input = nullptr);

    // Enable/disable GPU frame analysis (readback + metric computation).
    void set_analysis_enabled(bool enabled) { analysis_enabled_ = enabled; }
    bool analysis_enabled() const { return analysis_enabled_; }

    // Solo mode — active set is computed by RuntimeCore and synced here.
    void set_solo(int node_idx, const std::vector<bool>& active_set);
    int solo_node_idx() const { return solo_node_idx_; }
    bool is_solo_active() const { return solo_node_idx_ >= 0; }
    const std::vector<bool>& solo_active_set() const { return solo_active_set_; }

    // GPU texture management
    void allocate_gpu_textures(CompiledGraph& cg, WGPUDevice device,
                               uint32_t default_w, uint32_t default_h,
                               WGPUTextureFormat format,
                               WGPUTextureUsage extra_usage = 0);
    int find_gpu_sink(const CompiledGraph& cg) const;
    int find_effective_gpu_sink(const CompiledGraph& cg) const;
    bool has_gpu_operators(const CompiledGraph& cg) const;
    bool gpu_sink_source_size(const CompiledGraph& cg, int sink_idx,
                              uint32_t& w, uint32_t& h) const;
    WGPUTexture gpu_sink_source_texture(const CompiledGraph& cg, int sink_idx) const;
    bool needs_gpu_realloc() const { return needs_gpu_realloc_; }
    void clear_gpu_realloc() { needs_gpu_realloc_ = false; }

    // Lifecycle — release GPU textures and flush device.
    void shutdown_gpu(CompiledGraph& cg);

    // Per-node lane state context for LoopBased frame operators.
    struct NodeLaneCtx { LaneStateService* service; uint32_t node_idx; };

private:
    int solo_node_idx_ = -1;
    std::vector<bool> solo_active_set_;
    bool needs_gpu_realloc_ = false;
    bool analysis_enabled_ = true;
    WGPUDevice gpu_device_ = nullptr;
    std::string operators_src_dir_;

    // Lane state service for LoopBased frame operators.
    LaneStateService frame_lane_state_;
    std::vector<NodeLaneCtx> frame_lane_contexts_;

public:
    void set_operators_src_dir(const std::string& dir) { operators_src_dir_ = dir; }
    const std::string& operators_src_dir() const { return operators_src_dir_; }
};

} // namespace vivid
