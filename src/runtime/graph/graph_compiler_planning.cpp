#include "runtime/graph/graph_compiler_internal.h"

#include <cstring>

namespace vivid::graph_compiler_internal {

namespace {

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
