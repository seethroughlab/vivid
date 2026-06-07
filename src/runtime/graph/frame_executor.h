#pragma once

#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/executor_common.h"
#include "runtime/graph/value_arena.h"
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
    void tick(CompiledGraph& cg, const GraphMetronomeSample& metronome, double time,
              double delta_time, uint64_t frame,
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

    // Release any GPU textures queued for deferred destruction whose grace
    // period has elapsed. Safe to call once per frame. Also flushes everything
    // immediately when force=true (shutdown / device teardown).
    void drain_deferred_gpu_releases(bool force = false);

    // Per-node lane state context for LoopBased frame operators.
    using NodeLaneCtx = ExecutorLaneCtx;

private:
    // tick() per-node helpers — extracted from the monolithic tick() loop so the
    // hard lane-propagation and dispatch logic is isolated and unit-reachable.
    // All operate on the current node; behavior is identical to the inlined code.

    // Copy upstream outputs into cn's inputs across frame_direct_edges: scalar
    // (with remap), string, string-lane, file-param, and lane-aware ref
    // propagation with copy-on-write merge and scalar→lane lifting.
    void propagate_frame_direct_edges(CompiledGraph& cg, CompiledNode& cn, uint32_t ni);

    // Set the VividFrameContext fields that are identical between the LoopBased
    // per-lane path and the normal control path. Callers fill input/output_values
    // and the lane_* fields afterward (those differ per path).
    void populate_frame_context(VividFrameContext& ctx, CompiledNode& cn,
                                double time, double delta_time, uint64_t frame,
                                const GraphMetronomeSample& metronome,
                                const VividInputState* input);

    // GPU node dispatch: build VividGpuContext, resolve texture/custom inputs,
    // call process_gpu(), and apply preferred-size realloc requests.
    void process_gpu_node(CompiledGraph& cg, CompiledNode& cn, uint32_t ni, void* gpu_state,
                          double time, double delta_time, uint64_t frame,
                          const GraphMetronomeSample& metronome, const VividInputState* input);

    // LoopBased frame processing: per-lane loop over the operator's process_frame().
    void process_loopbased_node(CompiledNode& cn, uint32_t fi_ord,
                                double time, double delta_time, uint64_t frame,
                                const GraphMetronomeSample& metronome, const VividInputState* input);

    // Normal (non-lifted) control processing: a single process_frame() call.
    void process_control_node(CompiledNode& cn,
                              double time, double delta_time, uint64_t frame,
                              const GraphMetronomeSample& metronome, const VividInputState* input);

    int solo_node_idx_ = -1;
    std::vector<bool> solo_active_set_;
    bool needs_gpu_realloc_ = false;
    bool analysis_enabled_ = true;
    WGPUDevice gpu_device_ = nullptr;

    // Deferred GPU texture destruction. When a topology change or resize forces
    // allocate_gpu_textures() to reallocate per-node textures, the OLD textures
    // may still be referenced by a command buffer that the GPU has not finished
    // executing (wgpu-native encodes Metal render passes lazily at
    // CommandEncoderFinish, and submitted buffers drain asynchronously). Freeing
    // them immediately can leave a render-pass color attachment pointing at a
    // destroyed Metal texture -> EXC_BAD_ACCESS at submit. We hold them for a
    // few frames so the GPU is guaranteed done before release.
    struct DeferredGpuRelease {
        int frames_remaining = 0;
        std::vector<WGPUTexture> textures;
        std::vector<WGPUTextureView> views;
    };
    std::vector<DeferredGpuRelease> deferred_gpu_releases_;
    static constexpr int kGpuReleaseGraceFrames = 3;
    std::string operators_src_dir_;

    // Lane state service for LoopBased frame operators.
    LaneStateService frame_lane_state_;
    std::vector<NodeLaneCtx> frame_lane_contexts_;

    // Arena for value-buffer allocation during wire propagation (lane-value
    // clean-break Phase 7a; successor to the lane pool). Growable: the frame
    // thread may allocate, so wide remapped/expanded values (>capacity) grow
    // rather than silently truncating to an empty buffer.
    ValueArena value_arena_{VIVID_VALUE_FLOAT, 1024, /*growable=*/true};

public:
    void set_operators_src_dir(const std::string& dir) { operators_src_dir_ = dir; }
    const std::string& operators_src_dir() const { return operators_src_dir_; }
};

} // namespace vivid
