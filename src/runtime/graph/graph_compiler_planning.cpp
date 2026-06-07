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
        case VIVID_PORT_STRING:
        case VIVID_PORT_STRING_LANES: return VIVID_VALUE_STRING;
        case VIVID_PORT_TEXTURE:      return VIVID_VALUE_TEXTURE;
        default:                      return VIVID_VALUE_FLOAT;  // SCALAR, LANE_ARRAY, custom
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

uint32_t plan_value_flow(CompiledGraph& cg, const std::vector<uint32_t>& topo_order) {
    uint32_t mismatches = 0;
    for (uint32_t idx : topo_order) {
        auto& cn = cg.nodes[idx];

        // Input envelopes: project from the already-resolved input lane sets + the
        // input port's payload type. Track whether any input is Many.
        // NOTE (7d.3): native edge-propagation was attempted here but the value
        // envelope (WIRE multiplicity) genuinely differs from the lane-set INPUT
        // projection (the per-invocation/effective view for lifted/LoopBased nodes),
        // so it cannot coexist with the lane-set cross-check. Native value-flow is
        // deferred to 7d.5, where Pass 2.6 + this cross-check are removed together.
        cn.input_value_envelopes.assign(cn.input_lane_sets.size(), ValueEnvelope{});
        bool any_many_input = false;
        for (size_t pi = 0; pi < cn.input_lane_sets.size(); ++pi) {
            const VividValueType vt = (pi < cn.input_port_types.size())
                ? value_type_for_port(cn.input_port_types[pi]) : VIVID_VALUE_FLOAT;
            ValueEnvelope env = envelope_from_lane_set(cn.input_lane_sets[pi], vt,
                                                       value_storage_for(cn, vt));
            cn.input_value_envelopes[pi] = env;
            if (env.multiplicity == VIVID_MULTIPLICITY_MANY) any_many_input = true;
        }

        // Output envelopes: multiplicity INFERRED from the value model; identity +
        // count carried from the lane-set projection (runtime-determined parts).
        cn.output_value_envelopes.assign(cn.output_lane_sets.size(), ValueEnvelope{});
        for (size_t pi = 0; pi < cn.output_lane_sets.size(); ++pi) {
            const VividValueType vt = (pi < cn.output_port_types.size())
                ? value_type_for_port(cn.output_port_types[pi]) : VIVID_VALUE_FLOAT;
            const ValueEnvelope proj = envelope_from_lane_set(cn.output_lane_sets[pi], vt,
                                                              value_storage_for(cn, vt));
            const VividMultiplicity inferred = infer_output_multiplicity(cn, pi, any_many_input);

            ValueEnvelope env = proj;
            env.multiplicity = inferred;
            cn.output_value_envelopes[pi] = env;

            // Equivalence proof: the independently-inferred multiplicity must match
            // the Pass-2.6 lane-set projection. Non-fatal (Phase 2 changes no graph's
            // compile result); loud under VIVID_VERBOSE.
            if (inferred != proj.multiplicity) {
                ++mismatches;
                if (std::getenv("VIVID_VERBOSE")) {
                    std::fprintf(stderr,
                        "[vivid] value-flow: multiplicity mismatch at node '%s' out-port %zu "
                        "(inferred=%u, lane-set=%u)\n",
                        cn.node_id.c_str(), pi,
                        static_cast<unsigned>(inferred), static_cast<unsigned>(proj.multiplicity));
                }
            }
        }

        // Publish output envelopes onto outgoing edges.
        for (auto& e : cg.edges) {
            if (e.from_node != idx) continue;
            if (e.from_port < cn.output_value_envelopes.size())
                e.value_envelope = cn.output_value_envelopes[e.from_port];
        }
    }
    return mismatches;
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
        if (pi < cn.input_port_types.size() &&
            cn.input_port_types[pi] == VIVID_PORT_LANE_ARRAY) {
            return static_cast<int32_t>(pi);
        }
    }
    return -1;
}

uint32_t find_structural_input(const CompiledNode& cn) {
    for (const auto& ils : cn.input_lane_sets) {
        if (!ils.is_scalar()) return ils.lane_set_id;
    }
    return 0;
}

} // namespace

AudioLanePlan plan_audio_lane_strategy(
    const CompiledNode& cn,
    const AudioNodeState& a,
    const CompiledGraph& cg,
    uint32_t node_idx) {
    AudioLanePlan plan;

    if (cn.lane_behavior != LaneBehavior::Pointwise) return plan;

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
        plan.lane_lift_set_id = find_structural_input(cn);
        plan.override_channel_counts = true;
        return plan;
    }

    const auto* desc = cn.loader ? cn.loader->descriptor() : nullptr;
    bool opt_in = desc && desc->strategy_independent;
    if (!opt_in) return plan;

    uint32_t structural_set_id = find_structural_input(cn);
    if (structural_set_id == 0) {
        for (const auto& e : cg.edges) {
            if (e.to_node == node_idx && e.transport == EdgeTransport::Snapshot &&
                e.lane_set_id != 0) {
                structural_set_id = e.lane_set_id;
                break;
            }
        }
    }

    if (structural_set_id != 0) {
        plan.strategy = LaneExecutionStrategy::LoopBased;
        plan.lane_lift_set_id = structural_set_id;
        plan.lane_id_port = detect_lane_id_port(cn);
    }

    return plan;
}

FrameLanePlan plan_frame_lane_strategy(const CompiledNode& cn) {
    FrameLanePlan plan;

    if (cn.lane_behavior != LaneBehavior::Pointwise) return plan;

    const auto* desc = cn.loader ? cn.loader->descriptor() : nullptr;
    if (!desc || !desc->strategy_independent) return plan;

    uint32_t structural_set_id = find_structural_input(cn);
    if (structural_set_id != 0) {
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
            if (a.lane_lift_set_id != 0)
                return clamp_audio_width(max_loop_lanes);
            break;
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
            if (a.lane_lift_set_id != 0)
                return clamp_audio_width(max_loop_lanes);
            break;
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
            if (p >= cn.input_port_types.size() ||
                cn.input_port_types[p] != VIVID_PORT_LANE_ARRAY)
                continue;

            // Find source node/port for this input.
            uint32_t src_node = UINT32_MAX;
            uint32_t src_port = UINT32_MAX;
            for (const auto& e : cg.edges) {
                if (e.to_node == ni && e.to_port == p && !e.targets_param &&
                    e.data_type == VIVID_PORT_LANE_ARRAY) {
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

            // Check compile-time lane count from input_lane_sets.
            uint32_t lane_count = 0;
            if (p < cn.input_lane_sets.size() && !cn.input_lane_sets[p].is_scalar())
                lane_count = cn.input_lane_sets[p].lane_count;
            if (lane_count < threshold) continue;

            cn.gpu->lane_input_gpu_promoted[p] = true;
        }
    }
}

} // namespace vivid::graph_compiler_internal
