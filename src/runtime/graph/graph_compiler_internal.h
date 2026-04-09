#pragma once

#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/graph.h"

namespace vivid::graph_compiler_internal {

// Default initial capacity for pre-allocated lane buffers. Output builders on
// the frame thread can grow beyond this via LaneBuffer::resize() (up to
// CompiledGraph::max_lane_elements). Audio-thread pool buffers are fixed at
// this capacity. Bridge slots are also sized to this by default but can be
// overridden per-port based on compile-time lane count.
inline constexpr uint32_t kDefaultLaneCapacity = 1024;
inline constexpr uint32_t kGpuLanePromotionThreshold = 256;

struct AudioLanePlan {
    LaneExecutionStrategy strategy = LaneExecutionStrategy::Scalar;
    uint32_t lane_lift_count = 0;
    uint32_t lane_lift_set_id = 0;
    int32_t lane_id_port = -1;
    bool override_channel_counts = false;
};

struct FrameLanePlan {
    LaneExecutionStrategy strategy = LaneExecutionStrategy::Scalar;
    int32_t lane_id_port = -1;
};

AudioLanePlan plan_audio_lane_strategy(
    const CompiledNode& cn,
    const AudioNodeState& a,
    const CompiledGraph& cg,
    uint32_t node_idx);

FrameLanePlan plan_frame_lane_strategy(const CompiledNode& cn);

uint8_t effective_audio_output_channels(
    const CompiledNode& cn,
    const AudioNodeState& a,
    uint32_t output_port,
    uint32_t max_loop_lanes);

uint8_t effective_audio_input_channels(
    const CompiledNode& cn,
    const AudioNodeState& a,
    uint32_t input_port,
    uint32_t max_loop_lanes);

BridgeKind parse_bridge_kind(const std::string& s);
float remap_to_scale(const ConnectionDef& c);
void warm_up_instance_assets(CompiledNode& cn);

// Phase 4: conservative GPU lane promotion analysis.
void plan_gpu_lane_promotion(CompiledGraph& cg, uint32_t threshold = kGpuLanePromotionThreshold);

} // namespace vivid::graph_compiler_internal
