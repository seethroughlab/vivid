#pragma once

#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/graph.h"

namespace vivid::graph_compiler_internal {

inline constexpr uint32_t kMaxLaneCapacity = 1024;

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

BridgeKind parse_bridge_kind(const std::string& s);
float remap_to_scale(const ConnectionDef& c);
void warm_up_instance_assets(CompiledNode& cn);

} // namespace vivid::graph_compiler_internal
