#include "runtime/cadence_bridge.h"
#include "operator_api/type_id.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vivid {

void CadenceBridge::build(const CompiledGraph& cg) {
    analysis_mappings_.clear();

    // Build flat lookup: graph node index → snapshot array index (-1 = not audio)
    node_to_snapshot_idx_.assign(cg.nodes.size(), -1);
    uint32_t audio_count = 0;
    for (uint32_t idx : cg.audio_order) {
        node_to_snapshot_idx_[idx] = static_cast<int32_t>(audio_count++);
    }

    // Allocate ParamSnapshot arrays
    for (auto& snap : snapshots_) {
        snap.node_params.resize(audio_count);
        snap.float_input_values.resize(audio_count);
        snap.lane_inputs.resize(audio_count);
        snap.input_string_values.resize(audio_count);
        snap.custom_inputs.resize(audio_count);
        snap.solo_active_set.clear();

        for (uint32_t i = 0; i < audio_count; ++i) {
            uint32_t gi = cg.audio_order[i];
            const auto& cn = cg.nodes[gi];
            const auto& a = *cn.audio;
            snap.node_params[i] = cn.param_values;
            snap.float_input_values[i] = a.float_input_defaults;
            snap.lane_inputs[i].resize(cn.input_port_count);
            snap.input_string_values[i].assign(cn.input_port_count, "");
            snap.custom_inputs[i].resize(cn.input_port_count);
        }
    }

    // Allocate AnalysisSnapshot arrays
    for (auto& snap : analysis_snapshots_) {
        snap.rms.assign(audio_count, 0.0f);
        snap.peak.assign(audio_count, 0.0f);
        snap.waveform.resize(audio_count);
        for (auto& w : snap.waveform) w.fill(0.0f);
        snap.lane_outputs.resize(audio_count);
        snap.float_outputs.resize(audio_count);
        snap.errored.assign(audio_count, false);
        snap.error_msgs.resize(audio_count);
        for (auto& msg : snap.error_msgs) msg.fill('\0');

        for (uint32_t i = 0; i < audio_count; ++i) {
            uint32_t gi = cg.audio_order[i];
            const auto& cn = cg.nodes[gi];
            const auto& a = *cn.audio;
            snap.lane_outputs[i].resize(cn.output_port_count);
            snap.float_outputs[i].resize(a.float_output_count, 0.0f);
        }
    }

    // Build analysis mappings (audio nodes with rms/peak/waveform ports)
    for (uint32_t i = 0; i < audio_count; ++i) {
        uint32_t gi = cg.audio_order[i];
        const auto& a = *cg.nodes[gi].audio;

        auto rms_it = a.analysis_output_port_indices.find("rms");
        auto peak_it = a.analysis_output_port_indices.find("peak");
        auto wave_it = a.analysis_output_port_indices.find("waveform");

        if (rms_it != a.analysis_output_port_indices.end() &&
            peak_it != a.analysis_output_port_indices.end() &&
            wave_it != a.analysis_output_port_indices.end()) {
            analysis_mappings_.push_back({
                gi, i,
                rms_it->second,
                peak_it->second,
                wave_it->second
            });
        }
    }

    param_active_.store(0, std::memory_order_relaxed);
    analysis_active_.store(0, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// propagate_audio_display_params — update audio node param_values for display
// ---------------------------------------------------------------------------

void CadenceBridge::propagate_audio_display_params(CompiledGraph& cg) {
    for (const auto& e : cg.edges) {
        if (!e.targets_param) continue;
        auto& to_cn = cg.nodes[e.to_node];
        if (to_cn.active_cadence != Cadence::Audio) continue;
        const auto& from_cn = cg.nodes[e.from_node];
        if (e.targets_file_param) {
            const std::string& src = e.sources_file_param
                ? from_cn.file_param_storage[e.from_file_param_idx]
                : from_cn.output_string_values[e.from_port];
            to_cn.file_param_storage[e.to_file_param_idx] = src;
            to_cn.file_param_ptrs[e.to_file_param_idx] =
                to_cn.file_param_storage[e.to_file_param_idx].c_str();
            continue;
        }
        float raw = e.sources_param
            ? from_cn.param_values[e.from_port]
            : from_cn.output_values[e.from_port];
        float val = e.has_remap() ? e.apply_remap(raw) : raw;
        if (!(to_cn.param_lock_flags[e.to_port] & PARAM_LOCK_WIRES))
            to_cn.param_values[e.to_port] = val;
    }
}

// ---------------------------------------------------------------------------
// push_to_audio — snapshot frame-rate outputs for audio consumption
// ---------------------------------------------------------------------------


void CadenceBridge::push_to_audio(const CompiledGraph& cg) {
    int write_idx = 1 - param_active_.load(std::memory_order_acquire);
    auto& snap = snapshots_[write_idx];

    // 1. Base defaults: reset each audio node's snapshot to its own defaults
    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.audio_order.size()); ++i) {
        uint32_t gi = cg.audio_order[i];
        const auto& cn = cg.nodes[gi];
        const auto& a = *cn.audio;
        snap.node_params[i] = cn.param_values;
        snap.float_input_values[i] = a.float_input_defaults;
        for (auto& sp : snap.lane_inputs[i]) sp.length = 0;
        for (auto& s : snap.input_string_values[i]) s.clear();
        for (auto& ci : snap.custom_inputs[i]) ci.clear();
    }

    // 2. Apply frame→audio snapshot edges
    for (uint32_t ei : cg.frame_to_audio_edges) {
        const auto& e = cg.edges[ei];
        const auto& from_cn = cg.nodes[e.from_node];

        if (e.to_node >= node_to_snapshot_idx_.size()) continue;
        int32_t si_signed = node_to_snapshot_idx_[e.to_node];
        if (si_signed < 0) continue;
        uint32_t si = static_cast<uint32_t>(si_signed);

        if (e.targets_param && !e.targets_file_param) {
            // Scalar param modulation
            float val = e.sources_param
                ? from_cn.param_values[e.from_port]
                : from_cn.output_values[e.from_port];
            val *= e.remap_scale();
            if (e.to_port < snap.node_params[si].size())
                snap.node_params[si][e.to_port] = val;
        } else if (e.data_type == VIVID_PORT_LANE_ARRAY && !e.targets_param) {
            // Spread input
            if (e.from_port < from_cn.output_lanes.size() &&
                e.to_port < snap.lane_inputs[si].size()) {
                const auto& src = from_cn.output_lanes[e.from_port];
                auto& dst = snap.lane_inputs[si][e.to_port];
                dst.length = std::min(static_cast<uint32_t>(src.size()),
                                      LaneSnapshot::kMaxLength);
                float scale = e.remap_scale();
                for (uint32_t j = 0; j < dst.length; ++j)
                    dst.data[j] = src[j] * scale;
                dst.lane_set_id = e.lane_set_id;
            }
        } else if (e.data_type == VIVID_PORT_STRING) {
            // String input or file param
            const std::string& src = e.sources_file_param
                ? from_cn.file_param_storage[e.from_file_param_idx]
                : from_cn.output_string_values[e.from_port];
            if (e.targets_file_param) {
                // String → file param on audio node (rare but possible)
                // Note: file params on audio nodes are handled via main_thread_update
            } else if (e.to_port < snap.input_string_values[si].size()) {
                snap.input_string_values[si][e.to_port] = src;
            }
        } else if (e.data_type == VIVID_PORT_SIGNAL && !e.targets_param) {
            // Float CV input (control float → audio SIGNAL input)
            float val = e.sources_param
                ? from_cn.param_values[e.from_port]
                : from_cn.output_values[e.from_port];
            val *= e.remap_scale();
            if (e.to_signal_ordinal < snap.float_input_values[si].size())
                snap.float_input_values[si][e.to_signal_ordinal] = val;
        } else if (vivid_is_custom_port_type(e.data_type) && !e.targets_param) {
            // Custom type snapshot — find which custom output ordinal this port maps to
            uint32_t custom_ord = UINT32_MAX;
            for (uint32_t p = 0; p < from_cn.custom_output_port_indices.size(); ++p) {
                if (from_cn.custom_output_port_indices[p] == e.from_port) {
                    custom_ord = p; break;
                }
            }
            if (custom_ord < from_cn.custom_outputs.size()) {
                void* data = from_cn.custom_outputs[custom_ord];
                if (data && e.to_port < snap.custom_inputs[si].size()) {
                    auto& dst = snap.custom_inputs[si][e.to_port];
                    dst.type_id = e.custom_type_id;
                    dst.transport = e.port_transport;
                    uint32_t copy_size = std::min(e.custom_payload_size,
                                                  CustomPortSnapshot::kMaxBytes);
                    dst.byte_size = copy_size;
                    std::memcpy(dst.bytes, data, copy_size);
                    dst.valid = true;
                }
            }
        }
    }

    // 3. Publish
    param_active_.store(write_idx, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// pull_from_audio — inject audio analysis into frame-rate nodes
// ---------------------------------------------------------------------------

void CadenceBridge::pull_from_audio(CompiledGraph& cg) {
    const auto& snap = analysis_snapshots_[analysis_active_.load(std::memory_order_acquire)];

    // Propagate error state
    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.audio_order.size()); ++i) {
        uint32_t gi = cg.audio_order[i];
        auto& cn = cg.nodes[gi];
        if (i < snap.errored.size() && snap.errored[i]) {
            cn.errored = true;
            if (i < snap.error_msgs.size())
                cn.error_message.assign(snap.error_msgs[i].data());
        } else {
            cn.errored = false;
            cn.error_message.clear();
        }
    }

    // Inject float outputs via audio_to_frame snapshot edges
    for (uint32_t ei : cg.audio_to_frame_edges) {
        const auto& e = cg.edges[ei];
        if (e.from_node >= node_to_snapshot_idx_.size()) continue;
        int32_t si_signed = node_to_snapshot_idx_[e.from_node];
        if (si_signed < 0) continue;
        uint32_t si = static_cast<uint32_t>(si_signed);

        auto& to_cn = cg.nodes[e.to_node];

        if (e.data_type == VIVID_PORT_SIGNAL && !e.sources_param) {
            // Float output → frame-rate input (use precomputed ordinal)
            if (si < snap.float_outputs.size() &&
                e.from_signal_ordinal < snap.float_outputs[si].size()) {
                float val = snap.float_outputs[si][e.from_signal_ordinal];
                if (e.targets_param) {
                    if (e.to_port < to_cn.param_values.size())
                        to_cn.param_values[e.to_port] = val;
                } else {
                    if (e.to_port < to_cn.bridge_input_values.size()) {
                        to_cn.bridge_input_values[e.to_port] = val;
                        if (e.to_port < to_cn.bridge_input_dirty.size())
                            to_cn.bridge_input_dirty[e.to_port] = 1;
                    }
                }
                to_cn.dirty = true;
            }
        } else if (e.data_type == VIVID_PORT_LANE_ARRAY) {
            // Spread output → frame-rate input
            if (si < snap.lane_outputs.size() &&
                e.from_port < snap.lane_outputs[si].size()) {
                const auto& src = snap.lane_outputs[si][e.from_port];
                if (e.to_port < to_cn.input_lanes.size()) {
                    to_cn.input_lanes[e.to_port].assign(src.data,
                        src.data + src.length);
                    to_cn.dirty = true;
                }
            }
        }
    }

    // Sync all audio-node float outputs back to CompiledNode output_values.
    // This covers scalar outputs (e.g. clock beat_phase) even when no frame-rate
    // consumer is wired — they still need to be visible for inspection/display.
    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.audio_order.size()); ++i) {
        uint32_t gi = cg.audio_order[i];
        auto& cn = cg.nodes[gi];
        int32_t si_signed = node_to_snapshot_idx_[gi];
        if (si_signed < 0) continue;
        uint32_t si = static_cast<uint32_t>(si_signed);
        if (si >= snap.float_outputs.size()) continue;
        const auto& fo = snap.float_outputs[si];
        // Map float output ordinals back to output port indices
        uint32_t float_ord = 0;
        for (uint32_t p = 0; p < cn.output_port_count && float_ord < fo.size(); ++p) {
            if (p < cn.output_port_types.size() &&
                cn.output_port_types[p] == VIVID_PORT_SIGNAL) {
                if (p < cn.output_values.size())
                    cn.output_values[p] = fo[float_ord];
                float_ord++;
            }
        }
        // Also sync spread outputs
        if (si < snap.lane_outputs.size()) {
            for (uint32_t p = 0; p < cn.output_port_count && p < snap.lane_outputs[si].size(); ++p) {
                const auto& src = snap.lane_outputs[si][p];
                if (p < cn.output_lanes.size() && src.length > 0)
                    cn.output_lanes[p].assign(src.data, src.data + src.length);
            }
        }
    }

    // Inject analysis data (rms, peak, waveform) into audio nodes' own output ports
    for (const auto& am : analysis_mappings_) {
        auto& cn = cg.nodes[am.graph_node_idx];
        uint32_t si = am.snapshot_idx;

        if (si < snap.rms.size() && am.rms_port_idx < cn.output_values.size())
            cn.output_values[am.rms_port_idx] = snap.rms[si];
        if (si < snap.peak.size() && am.peak_port_idx < cn.output_values.size())
            cn.output_values[am.peak_port_idx] = snap.peak[si];
        if (si < snap.waveform.size() && am.waveform_port_idx < cn.output_lanes.size()) {
            auto& dst = cn.output_lanes[am.waveform_port_idx];
            dst.assign(snap.waveform[si].begin(), snap.waveform[si].end());
        }
        cn.dirty = true;
    }
}


void CadenceBridge::set_solo_active_set(const std::vector<bool>& set) {
    // Write into inactive snapshot, will be published on next push_to_audio
    int write_idx = 1 - param_active_.load(std::memory_order_acquire);
    snapshots_[write_idx].solo_active_set = set;
}

} // namespace vivid
