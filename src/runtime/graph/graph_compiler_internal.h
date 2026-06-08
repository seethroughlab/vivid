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

// Per-port DECLARED multiplicity (lane-value clean-break Phase 7d). Arity is
// declared on the port via VividPortDescriptor.multiplicity — the value-model
// successor to encoding arity in the port type (the LANE_ARRAY/STRING_LANES port
// types were retired in Phase 7d.5e). Gates read this instead of the port type.
inline VividMultiplicity port_declared_multiplicity(const VividPortDescriptor& pd) {
    return pd.multiplicity;
}

// Per-port PAYLOAD value-type (lane-value clean-break Phase 7d). Honors an explicit
// VividPortDescriptor.value_type, else derives from the payload port type (the
// multiplicity axis is orthogonal): SCALAR→FLOAT, STRING→STRING, AUDIO_BUFFER→AUDIO,
// TEXTURE→TEXTURE.
inline VividValueType port_value_type(const VividPortDescriptor& pd) {
    if (pd.value_type != VIVID_VALUE_FLOAT) return pd.value_type;  // 0 == FLOAT default
    switch (pd.type) {
        case VIVID_PORT_AUDIO_BUFFER:  return VIVID_VALUE_AUDIO;
        case VIVID_PORT_STRING:        return VIVID_VALUE_STRING;
        case VIVID_PORT_TEXTURE:       return VIVID_VALUE_TEXTURE;
        default:                       return VIVID_VALUE_FLOAT;  // SCALAR, custom
    }
}

// Phase 4: conservative GPU lane promotion analysis.
void plan_gpu_lane_promotion(CompiledGraph& cg, uint32_t threshold = kGpuLanePromotionThreshold);

// Lane-value clean-break, Phase 2: value-flow inference. Computes each node's
// input/output ValueEnvelopes (and every edge's value_envelope) from the
// operator's multiplicity_behavior + input envelopes + port value-type, in
// PARALLEL with the lane sets (which stay the live execution path). Asserts the
// inferred multiplicity is equivalent to the Pass-2.6 lane sets and records
// non-fatal diagnostics on mismatch. `topo_order` must be a topological order.
// Returns the number of edge equivalence mismatches (0 = fully equivalent).
uint32_t plan_value_flow(CompiledGraph& cg, const std::vector<uint32_t>& topo_order);

} // namespace vivid::graph_compiler_internal
