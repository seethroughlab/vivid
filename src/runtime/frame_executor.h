#pragma once

#include "runtime/compiled_graph.h"
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
// Replaces Scheduler::tick().  Operates on a CompiledGraph, iterating
// frame_order and propagating values via frame_direct_edges.
// ---------------------------------------------------------------------------

class FrameExecutor {
public:
    void tick(CompiledGraph& cg, double time, double delta_time, uint64_t frame,
              void* gpu_state = nullptr, PostNodeFn on_gpu_node = nullptr,
              const VividInputState* input = nullptr);

    // Solo mode
    void set_solo(int node_idx);
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
    bool needs_gpu_realloc() const { return needs_gpu_realloc_; }
    void clear_gpu_realloc() { needs_gpu_realloc_ = false; }

private:
    int solo_node_idx_ = -1;
    std::vector<bool> solo_active_set_;
    bool needs_gpu_realloc_ = false;
    std::string operators_src_dir_;

public:
    void set_operators_src_dir(const std::string& dir) { operators_src_dir_ = dir; }
    const std::string& operators_src_dir() const { return operators_src_dir_; }
};

} // namespace vivid
