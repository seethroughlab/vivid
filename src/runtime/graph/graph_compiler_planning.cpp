#include "runtime/graph/graph_compiler_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vivid::graph_compiler_internal {

// ---- Value-flow inference (lane-value clean-break, Phase 2) -----------------
namespace {

// Payload type a port carries. Channel count (for AUDIO) is payload layout, NOT
// multiplicity — the value-flow separation invariant.
VividValueType value_type_for_port(VividPortType t) {
    switch (t) {
        case VIVID_PORT_AUDIO_BUFFER: return VIVID_VALUE_AUDIO;
        case VIVID_PORT_STRING:       return VIVID_VALUE_STRING;
        case VIVID_PORT_TEXTURE:      return VIVID_VALUE_TEXTURE;
        default:                      return VIVID_VALUE_FLOAT;  // SCALAR, custom
    }
}

// Initial storage policy from payload type + node domain. Refined in Phase 3.
VividStorageKind value_storage_for(const CompiledNode& cn, VividValueType vt) {
    if (vt == VIVID_VALUE_TEXTURE || cn.is_gpu())     return VIVID_STORAGE_GPU;
    if (vt == VIVID_VALUE_AUDIO || cn.active_cadence == Cadence::Audio)
                                                      return VIVID_STORAGE_AUDIO_BLOCK;
    if (vt == VIVID_VALUE_STRING)                     return VIVID_STORAGE_STRING_STORE;
    return VIVID_STORAGE_CPU;
}

// Infer an output port's multiplicity from the value model — independently of the
// lane-set id. A lane-array/string-lanes port carries Many by definition; otherwise
// the operator's multiplicity_behavior decides (Map/Preserve/Kernel forward the
// input multiplicity). Mirrors the Pass-2.6 output rules in value terms.
VividMultiplicity infer_output_multiplicity(const CompiledNode& cn, size_t port_idx,
                                            bool any_many_input) {
    // Reduction breakout: a DECLARED-Many output port of a Reducer exposes
    // per-element structure (Many) while its primary outputs collapse to scalar.
    // Value-model successor to Pass 2.6's breakout rule — scoped to REDUCE behavior
    // (was lane_behavior==Reduction) + per-port DECLARED multiplicity (was the
    // LANE_ARRAY port type). Non-Reduction declared-Many outputs follow the
    // behavior/input multiplicity below (Pass 2.6 does NOT force them Many).
    if (cn.multiplicity_behavior == VIVID_MULTIPLICITY_REDUCE &&
        port_idx < cn.output_port_multiplicities.size() &&
        cn.output_port_multiplicities[port_idx] == VIVID_MULTIPLICITY_MANY)
        return VIVID_MULTIPLICITY_MANY;
    switch (cn.multiplicity_behavior) {
        case VIVID_MULTIPLICITY_SCALAR_ONLY:
        case VIVID_MULTIPLICITY_REDUCE:   return VIVID_MULTIPLICITY_SCALAR;
        case VIVID_MULTIPLICITY_GENERATE:
        case VIVID_MULTIPLICITY_COLLECT:  return VIVID_MULTIPLICITY_MANY;
        case VIVID_MULTIPLICITY_MAP:
        case VIVID_MULTIPLICITY_PRESERVE:
        case VIVID_MULTIPLICITY_KERNEL:
        default:
            return any_many_input ? VIVID_MULTIPLICITY_MANY : VIVID_MULTIPLICITY_SCALAR;
    }
}

}  // namespace

void plan_value_flow(CompiledGraph& cg, const std::vector<uint32_t>& topo_order) {
    uint32_t next_pgid = 2;  // 0/1 reserved (no distinct provenance; UI colors >1)
    for (uint32_t idx : topo_order) {
        auto& cn = cg.nodes[idx];

        // Input envelopes: propagate NATIVELY from incoming Direct edges' published
        // value envelopes (7d.5c.2) — the value model is now the multiplicity
        // AUTHORITY, no longer a projection of the Pass-2.6 lane sets. Snapshot
        // (cross-cadence) lift is handled separately by the strategy planner.
        cn.input_value_envelopes.assign(cn.input_port_count, ValueEnvelope{});
        for (uint32_t pi = 0; pi < cn.input_port_count; ++pi) {
            const VividValueType vt = (pi < cn.input_port_types.size())
                ? value_type_for_port(cn.input_port_types[pi]) : VIVID_VALUE_FLOAT;
            ValueEnvelope& env = cn.input_value_envelopes[pi];
            env.value_type    = vt;
            env.multiplicity  = VIVID_MULTIPLICITY_SCALAR;
            env.value_count   = 1;
            env.identity_mode = VIVID_IDENTITY_NONE;
            env.storage_kind  = value_storage_for(cn, vt);
        }
        bool any_many_input = false;
        uint32_t resolved_in_pgid = 0;  // first Many input's provenance group (for forward)
        for (const auto& e : cg.edges) {
            if (e.to_node != idx || e.transport != EdgeTransport::Direct) continue;
            if (e.to_port >= cn.input_value_envelopes.size()) continue;
            if (e.value_envelope.multiplicity != VIVID_MULTIPLICITY_MANY) continue;
            ValueEnvelope& env = cn.input_value_envelopes[e.to_port];
            env.multiplicity        = VIVID_MULTIPLICITY_MANY;
            env.value_count         = e.value_envelope.value_count;
            env.identity_mode       = e.value_envelope.identity_mode;
            env.provenance_group_id = e.value_envelope.provenance_group_id;
            if (resolved_in_pgid == 0) resolved_in_pgid = e.value_envelope.provenance_group_id;
            any_many_input          = true;
        }

        // Output envelopes: multiplicity INFERRED from the value model (the sole
        // multiplicity authority since 7e.5b — the lane-set projection is gone).
        // value_count is compile-time 1 (runtime refines the actual element count);
        // identity is POSITIONAL for Many outputs (stable-id behaviors are a future
        // value-model feature, not produced by the current operator set).
        cn.output_value_envelopes.assign(cn.output_port_count, ValueEnvelope{});
        uint32_t node_minted_pgid = 0;  // shared id minted for this node's generated/breakout Many outs
        for (size_t pi = 0; pi < cn.output_port_count; ++pi) {
            const VividValueType vt = (pi < cn.output_port_types.size())
                ? value_type_for_port(cn.output_port_types[pi]) : VIVID_VALUE_FLOAT;
            const VividMultiplicity inferred = infer_output_multiplicity(cn, pi, any_many_input);

            ValueEnvelope env;
            env.value_type    = vt;
            env.multiplicity  = inferred;
            env.value_count   = 1;
            env.identity_mode = (inferred == VIVID_MULTIPLICITY_MANY)
                                ? VIVID_IDENTITY_POSITIONAL : VIVID_IDENTITY_NONE;
            env.storage_kind  = value_storage_for(cn, vt);
            // Provenance group id (value-model successor to lane_set_id, 7e.5a):
            // GENERATE/COLLECT + REDUCE-breakout mint ONE shared id per node;
            // MAP/PRESERVE/KERNEL forward the resolved Many input's id; scalar → 0.
            if (inferred == VIVID_MULTIPLICITY_MANY) {
                const VividMultiplicityBehavior mb = cn.multiplicity_behavior;
                if (mb == VIVID_MULTIPLICITY_GENERATE || mb == VIVID_MULTIPLICITY_COLLECT ||
                    mb == VIVID_MULTIPLICITY_REDUCE) {
                    if (node_minted_pgid == 0) node_minted_pgid = next_pgid++;
                    env.provenance_group_id = node_minted_pgid;
                } else {
                    env.provenance_group_id = resolved_in_pgid;
                }
            }
            cn.output_value_envelopes[pi] = env;
        }

        // Publish output envelopes onto outgoing edges.
        for (auto& e : cg.edges) {
            if (e.from_node != idx) continue;
            if (e.from_port < cn.output_value_envelopes.size())
                e.value_envelope = cn.output_value_envelopes[e.from_port];
        }
    }
}

namespace {

uint8_t clamp_audio_width(uint32_t width) {
    width = std::max<uint32_t>(1, width);
    width = std::min<uint32_t>(255, width);
    return static_cast<uint8_t>(width);
}

bool is_audio_port(const std::vector<VividPortType>& port_types, uint32_t port_idx) {
    return port_idx < port_types.size() &&
           port_types[port_idx] == VIVID_PORT_AUDIO_BUFFER;
}

int32_t detect_lane_id_port(const CompiledNode& cn) {
    auto li_it = cn.input_port_indices.find("lane_ids");
    if (li_it != cn.input_port_indices.end()) {
        uint32_t pi = li_it->second;
        // Many-capable "lane_ids" input (value-model successor to LANE_ARRAY; 7d.5d.1).
        if (pi < cn.input_port_multiplicities.size() &&
            cn.input_port_multiplicities[pi] == VIVID_MULTIPLICITY_MANY) {
            return static_cast<int32_t>(pi);
        }
    }
    return -1;
}

// Lift trigger (7d.5c.1): does any input carry Many per the value envelopes? The
// value model is the multiplicity authority; this drives the lift DECISION.
bool has_many_value_input(const CompiledNode& cn) {
    for (const auto& env : cn.input_value_envelopes) {
        if (env.multiplicity == VIVID_MULTIPLICITY_MANY) return true;
    }
    return false;
}

// Cross-cadence: a Snapshot (bridge) edge carrying Many into this node — the value
// successor to the `e.lane_set_id != 0` snapshot fallback below.
bool has_many_snapshot_input(const CompiledGraph& cg, uint32_t node_idx) {
    for (const auto& e : cg.edges) {
        if (e.to_node == node_idx && e.transport == EdgeTransport::Snapshot &&
            e.value_envelope.multiplicity == VIVID_MULTIPLICITY_MANY)
            return true;
    }
    return false;
}

} // namespace

AudioLanePlan plan_audio_lane_strategy(
    const CompiledNode& cn,
    const AudioNodeState& a,
    const CompiledGraph& cg,
    uint32_t node_idx) {
    AudioLanePlan plan;

    if (cn.multiplicity_behavior != VIVID_MULTIPLICITY_MAP) return plan;

    bool all_mono = true;
    for (uint32_t p = 0; p < cn.input_port_count && all_mono; ++p) {
        if (p < a.descriptor_input_channels.size() &&
            cn.input_port_types[p] == VIVID_PORT_AUDIO_BUFFER &&
            a.descriptor_input_channels[p] > 1) {
            all_mono = false;
        }
    }
    for (uint32_t p = 0; p < cn.output_port_count && all_mono; ++p) {
        if (p < a.descriptor_output_channels.size() &&
            cn.output_port_types[p] == VIVID_PORT_AUDIO_BUFFER &&
            a.descriptor_output_channels[p] > 1) {
            all_mono = false;
        }
    }
    if (!all_mono) return plan;

    uint8_t max_wire_ch = 1;
    for (const auto& e : cg.edges) {
        if (e.to_node == node_idx && e.transport == EdgeTransport::Direct &&
            !e.targets_param) {
            uint8_t src_ch = 1;
            auto& from_a = cg.nodes[e.from_node].audio;
            if (from_a && e.from_port < from_a->output_channel_counts.size())
                src_ch = from_a->output_channel_counts[e.from_port];
            if (src_ch > max_wire_ch) max_wire_ch = src_ch;
        }
    }

    if (max_wire_ch > 1) {
        plan.strategy = LaneExecutionStrategy::InstancePerLane;
        plan.lane_lift_count = max_wire_ch;
        plan.override_channel_counts = true;
        return plan;
    }

    const auto* desc = cn.loader ? cn.loader->descriptor() : nullptr;
    bool opt_in = desc && desc->strategy_independent;
    if (!opt_in) return plan;

    // Lift DECISION from the value model (7d.5c.1): a Many input on a Direct wire
    // or via a Snapshot (cross-cadence) edge. Behavior-identical to the old
    // lane-set trigger while the value envelopes are still the lane-set projection.
    const bool lift = has_many_value_input(cn) || has_many_snapshot_input(cg, node_idx);

    if (lift) {
        plan.strategy = LaneExecutionStrategy::LoopBased;
        plan.lane_id_port = detect_lane_id_port(cn);
    }

    return plan;
}

FrameLanePlan plan_frame_lane_strategy(const CompiledNode& cn) {
    FrameLanePlan plan;

    if (cn.multiplicity_behavior != VIVID_MULTIPLICITY_MAP) return plan;

    const auto* desc = cn.loader ? cn.loader->descriptor() : nullptr;
    if (!desc || !desc->strategy_independent) return plan;

    // Lift DECISION from the value model (7d.5c.1). Frame has no cross-cadence lift.
    if (has_many_value_input(cn)) {
        plan.strategy = LaneExecutionStrategy::LoopBased;
        plan.lane_id_port = detect_lane_id_port(cn);
    }

    return plan;
}

uint8_t effective_audio_output_channels(
    const CompiledNode& cn,
    const AudioNodeState& a,
    uint32_t output_port,
    uint32_t max_loop_lanes) {
    if (!is_audio_port(cn.output_port_types, output_port)) {
        if (output_port < a.output_channel_counts.size())
            return clamp_audio_width(a.output_channel_counts[output_port]);
        return 1;
    }

    switch (a.execution_strategy) {
        case LaneExecutionStrategy::InstancePerLane:
            if (a.lane_lift_count > 0)
                return clamp_audio_width(a.lane_lift_count);
            break;
        case LaneExecutionStrategy::LoopBased:
            return clamp_audio_width(max_loop_lanes);
        case LaneExecutionStrategy::Scalar:
            break;
    }

    if (output_port < a.output_channel_counts.size())
        return clamp_audio_width(a.output_channel_counts[output_port]);
    return 1;
}

uint8_t effective_audio_input_channels(
    const CompiledNode& cn,
    const AudioNodeState& a,
    uint32_t input_port,
    uint32_t max_loop_lanes) {
    if (!is_audio_port(cn.input_port_types, input_port)) {
        if (input_port < a.input_channel_counts.size())
            return clamp_audio_width(a.input_channel_counts[input_port]);
        return 1;
    }

    switch (a.execution_strategy) {
        case LaneExecutionStrategy::InstancePerLane:
            if (a.lane_lift_count > 0)
                return clamp_audio_width(a.lane_lift_count);
            break;
        case LaneExecutionStrategy::LoopBased:
            return clamp_audio_width(max_loop_lanes);
        case LaneExecutionStrategy::Scalar:
            break;
    }

    if (input_port < a.input_channel_counts.size())
        return clamp_audio_width(a.input_channel_counts[input_port]);
    return 1;
}

BridgeKind parse_bridge_kind(const std::string& s) {
    if (s == "hold") return BridgeKind::Hold;
    if (s == "snapshot") return BridgeKind::Snapshot;
    if (s == "last_sample") return BridgeKind::LastSample;
    if (s == "rms") return BridgeKind::Rms;
    if (s == "peak") return BridgeKind::Peak;
    if (s == "waveform") return BridgeKind::Waveform;
    return BridgeKind::None;
}

float remap_to_scale(const ConnectionDef& c) {
    float range = c.from_max - c.from_min;
    return (range != 0.0f) ? (c.to_max - c.to_min) / range : 1.0f;
}

// ---------------------------------------------------------------------------
// plan_gpu_lane_promotion — conservative GPU storage-buffer promotion.
//
// For each GPU node's LANE_ARRAY input port, check whether the source lane
// can be promoted to a GPU storage buffer. Conservative: skip if the source
// also feeds audio consumers or non-GPU frame consumers (would require
// readback). Skip if compile-time lane count is below threshold.
// ---------------------------------------------------------------------------

void plan_gpu_lane_promotion(CompiledGraph& cg, uint32_t threshold) {
    // Build a set of nodes that are audio-cadence consumers.
    std::vector<bool> is_audio_node(cg.nodes.size(), false);
    for (uint32_t idx : cg.audio_order) is_audio_node[idx] = true;

    for (uint32_t ni = 0; ni < static_cast<uint32_t>(cg.nodes.size()); ++ni) {
        auto& cn = cg.nodes[ni];
        if (!cn.gpu) continue;

        // Size promotion vectors.
        cn.gpu->lane_input_gpu_promoted.assign(cn.input_port_count, false);
        cn.gpu->resolved_lane_gpu_bufs.resize(cn.input_port_count, nullptr);
        cn.gpu->resolved_lane_gpu_lengths.resize(cn.input_port_count, 0);

        for (uint32_t p = 0; p < cn.input_port_count; ++p) {
            // Many input (value-model successor to LANE_ARRAY; 7d.5d.1). GPU nodes
            // only carry float-many lane inputs, so multiplicity alone suffices —
            // matching the old LANE_ARRAY check (the downstream edge gate on
            // e.value_envelope float-many below already restricts to float lanes).
            if (p >= cn.input_port_multiplicities.size() ||
                cn.input_port_multiplicities[p] != VIVID_MULTIPLICITY_MANY)
                continue;

            // Find source node/port for this input.
            uint32_t src_node = UINT32_MAX;
            uint32_t src_port = UINT32_MAX;
            for (const auto& e : cg.edges) {
                if (e.to_node == ni && e.to_port == p && !e.targets_param &&
                    e.value_envelope.value_type == VIVID_VALUE_FLOAT &&
                    e.value_envelope.multiplicity == VIVID_MULTIPLICITY_MANY) {
                    src_node = e.from_node;
                    src_port = e.from_port;
                    break;
                }
            }
            if (src_node == UINT32_MAX) continue;

            // Check: does the source also feed audio-cadence consumers?
            bool feeds_audio = false;
            bool feeds_cpu_frame = false;
            for (const auto& e : cg.edges) {
                if (e.from_node != src_node || e.from_port != src_port) continue;
                if (e.to_node == ni) continue; // skip self

                if (is_audio_node[e.to_node]) {
                    feeds_audio = true;
                    break;
                }
                if (!cg.nodes[e.to_node].gpu) {
                    feeds_cpu_frame = true;
                }
            }
            if (feeds_audio || feeds_cpu_frame) continue;

            // Check compile-time element count from the value envelope.
            uint32_t lane_count = 0;
            if (p < cn.input_value_envelopes.size() &&
                cn.input_value_envelopes[p].multiplicity == VIVID_MULTIPLICITY_MANY)
                lane_count = cn.input_value_envelopes[p].value_count;
            if (lane_count < threshold) continue;

            cn.gpu->lane_input_gpu_promoted[p] = true;
        }
    }
}

} // namespace vivid::graph_compiler_internal
