#pragma once

#include "runtime/graph/lane_state.h"
#include "runtime/graph/graph.h"  // GraphMetronomeSample
#include <cstdint>

namespace vivid {

// ---------------------------------------------------------------------------
// Shared helpers for frame and audio executors.
// ---------------------------------------------------------------------------

// Per-node lane state context — identical for both executor types.
struct ExecutorLaneCtx {
    LaneStateService* service;
    uint32_t node_idx;
};

// Lane state service bridge callbacks.  The void* context is an
// ExecutorLaneCtx* (or a layout-compatible NodeLaneCtx*).
inline void* lane_state_fn_bridge(void* ctx_ptr, uint32_t lane_id, uint32_t byte_size) {
    auto* lsc = static_cast<ExecutorLaneCtx*>(ctx_ptr);
    return lsc->service->get(lsc->node_idx, lane_id, byte_size);
}
inline uint32_t allocate_lane_id_fn_bridge(void* ctx_ptr) {
    auto* lsc = static_cast<ExecutorLaneCtx*>(ctx_ptr);
    return lsc->service->allocate_lane_id();
}
inline void retire_lane_id_fn_bridge(void* ctx_ptr, uint32_t lane_id) {
    auto* lsc = static_cast<ExecutorLaneCtx*>(ctx_ptr);
    lsc->service->retire(lsc->node_idx, lane_id);
}

// Populate metronome fields on any context struct that carries them
// (VividFrameContext, VividAudioContext, VividGpuContext).
template <typename Ctx>
inline void populate_metronome_context(Ctx& ctx, const GraphMetronomeSample& sample) {
    ctx.metronome_bpm            = sample.bpm;
    ctx.metronome_beats_per_bar  = static_cast<uint32_t>(sample.beats_per_bar);
    ctx.metronome_beats_elapsed  = sample.beats_elapsed;
    ctx.metronome_beat_phase     = sample.beat_phase;
    ctx.metronome_bar_phase      = sample.bar_phase;
    ctx.metronome_beat_ms        = sample.beat_ms;
}

}  // namespace vivid
