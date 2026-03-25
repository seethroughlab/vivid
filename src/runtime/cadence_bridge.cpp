#include "runtime/cadence_bridge.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vivid {

void CadenceBridge::build(const CompiledGraph& cg) {
    node_to_snapshot_idx_.clear();
    analysis_mappings_.clear();

    // Count audio-cadence nodes and build snapshot index mapping
    uint32_t audio_count = 0;
    for (uint32_t idx : cg.audio_order) {
        node_to_snapshot_idx_[idx] = audio_count++;
    }

    // Allocate ParamSnapshot arrays
    for (auto& snap : snapshots_) {
        snap.node_params.resize(audio_count);
        snap.float_input_values.resize(audio_count);
        snap.spread_inputs.resize(audio_count);
        snap.input_string_values.resize(audio_count);
        snap.custom_inputs.resize(audio_count);
        snap.solo_active_set.clear();

        for (uint32_t i = 0; i < audio_count; ++i) {
            uint32_t gi = cg.audio_order[i];
            const auto& cn = cg.nodes[gi];
            snap.node_params[i] = cn.param_values;
            snap.float_input_values[i] = cn.float_input_defaults;
            snap.spread_inputs[i].resize(cn.input_port_count);
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
        snap.spread_outputs.resize(audio_count);
        snap.float_outputs.resize(audio_count);
        snap.errored.assign(audio_count, false);
        snap.error_msgs.resize(audio_count);
        for (auto& msg : snap.error_msgs) msg.fill('\0');

        for (uint32_t i = 0; i < audio_count; ++i) {
            uint32_t gi = cg.audio_order[i];
            const auto& cn = cg.nodes[gi];
            snap.spread_outputs[i].resize(cn.output_port_count);
            snap.float_outputs[i].resize(cn.float_output_count, 0.0f);
        }
    }

    // Build analysis mappings (audio nodes with rms/peak/waveform ports)
    for (uint32_t i = 0; i < audio_count; ++i) {
        uint32_t gi = cg.audio_order[i];
        const auto& cn = cg.nodes[gi];

        auto rms_it = cn.analysis_output_port_indices.find("rms");
        auto peak_it = cn.analysis_output_port_indices.find("peak");
        auto wave_it = cn.analysis_output_port_indices.find("waveform");

        if (rms_it != cn.analysis_output_port_indices.end() &&
            peak_it != cn.analysis_output_port_indices.end() &&
            wave_it != cn.analysis_output_port_indices.end()) {
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
// push_to_audio — snapshot frame-rate outputs for audio consumption
// ---------------------------------------------------------------------------

static float compute_scale(const CompiledEdge& e) {
    float range = e.from_max - e.from_min;
    return (range != 0.0f) ? (e.to_max - e.to_min) / range : 1.0f;
}

void CadenceBridge::push_to_audio(const CompiledGraph& cg) {
    int write_idx = 1 - param_active_.load(std::memory_order_acquire);
    auto& snap = snapshots_[write_idx];

    // 1. Base defaults: reset each audio node's snapshot to its own defaults
    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.audio_order.size()); ++i) {
        uint32_t gi = cg.audio_order[i];
        const auto& cn = cg.nodes[gi];
        snap.node_params[i] = cn.param_values;
        snap.float_input_values[i] = cn.float_input_defaults;
        for (auto& sp : snap.spread_inputs[i]) sp.length = 0;
        for (auto& s : snap.input_string_values[i]) s.clear();
        for (auto& ci : snap.custom_inputs[i]) ci.clear();
    }

    // 2. Apply frame→audio snapshot edges
    for (uint32_t ei : cg.frame_to_audio_edges) {
        const auto& e = cg.edges[ei];
        const auto& from_cn = cg.nodes[e.from_node];

        auto snap_it = node_to_snapshot_idx_.find(e.to_node);
        if (snap_it == node_to_snapshot_idx_.end()) continue;
        uint32_t si = snap_it->second;

        if (e.targets_param && !e.targets_file_param) {
            // Scalar param modulation
            float val = e.sources_param
                ? from_cn.param_values[e.from_port]
                : from_cn.output_values[e.from_port];
            val *= compute_scale(e);
            if (e.to_port < snap.node_params[si].size())
                snap.node_params[si][e.to_port] = val;
        } else if (e.data_type == VIVID_PORT_SPREAD && !e.targets_param) {
            // Spread input
            if (e.from_port < from_cn.output_spreads.size() &&
                e.to_port < snap.spread_inputs[si].size()) {
                const auto& src = from_cn.output_spreads[e.from_port];
                auto& dst = snap.spread_inputs[si][e.to_port];
                dst.length = std::min(static_cast<uint32_t>(src.size()),
                                      SpreadSnapshot::kMaxLength);
                float scale = compute_scale(e);
                for (uint32_t j = 0; j < dst.length; ++j)
                    dst.data[j] = src[j] * scale;
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
            val *= compute_scale(e);
            // Need to find the float input ordinal for this port
            const auto& to_cn = cg.nodes[e.to_node];
            uint32_t float_ord = 0;
            for (uint32_t p = 0; p < e.to_port && p < to_cn.input_port_types.size(); ++p) {
                if (to_cn.input_port_types[p] == VIVID_PORT_SIGNAL) float_ord++;
            }
            if (float_ord < snap.float_input_values[si].size())
                snap.float_input_values[si][float_ord] = val;
        } else if (vivid_is_custom_port_type(e.data_type) && !e.targets_param) {
            // Custom type snapshot
            if (e.from_port < from_cn.custom_output_port_indices.size()) {
                // Find the custom output buffer
                uint32_t custom_ord = 0;
                for (uint32_t p = 0; p < from_cn.custom_output_port_indices.size(); ++p) {
                    if (from_cn.custom_output_port_indices[p] == e.from_port) {
                        custom_ord = p; break;
                    }
                }
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
        auto snap_it = node_to_snapshot_idx_.find(e.from_node);
        if (snap_it == node_to_snapshot_idx_.end()) continue;
        uint32_t si = snap_it->second;

        auto& to_cn = cg.nodes[e.to_node];

        if (e.data_type == VIVID_PORT_SIGNAL && !e.sources_param) {
            // Float output → frame-rate input
            // Find the float output ordinal for the source port
            const auto& from_cn = cg.nodes[e.from_node];
            uint32_t float_ord = 0;
            for (uint32_t p = 0; p < e.from_port && p < from_cn.output_port_types.size(); ++p) {
                if (from_cn.output_port_types[p] == VIVID_PORT_SIGNAL) float_ord++;
            }
            if (si < snap.float_outputs.size() &&
                float_ord < snap.float_outputs[si].size()) {
                float val = snap.float_outputs[si][float_ord];
                if (e.targets_param) {
                    if (e.to_port < to_cn.param_values.size())
                        to_cn.param_values[e.to_port] = val;
                } else {
                    if (e.to_port < to_cn.input_values.size())
                        to_cn.input_values[e.to_port] = val;
                }
                to_cn.generation++;
            }
        } else if (e.data_type == VIVID_PORT_SPREAD) {
            // Spread output → frame-rate input
            if (si < snap.spread_outputs.size() &&
                e.from_port < snap.spread_outputs[si].size()) {
                const auto& src = snap.spread_outputs[si][e.from_port];
                if (e.to_port < to_cn.input_spreads.size()) {
                    to_cn.input_spreads[e.to_port].assign(src.data,
                        src.data + src.length);
                    to_cn.generation++;
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
        auto snap_it = node_to_snapshot_idx_.find(gi);
        if (snap_it == node_to_snapshot_idx_.end()) continue;
        uint32_t si = snap_it->second;
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
        if (si < snap.spread_outputs.size()) {
            for (uint32_t p = 0; p < cn.output_port_count && p < snap.spread_outputs[si].size(); ++p) {
                const auto& src = snap.spread_outputs[si][p];
                if (p < cn.output_spreads.size() && src.length > 0)
                    cn.output_spreads[p].assign(src.data, src.data + src.length);
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
        if (si < snap.waveform.size() && am.waveform_port_idx < cn.output_spreads.size()) {
            auto& dst = cn.output_spreads[am.waveform_port_idx];
            dst.assign(snap.waveform[si].begin(), snap.waveform[si].end());
        }
        cn.generation++;
    }
}

// ---------------------------------------------------------------------------
// update_sources — main-thread update hook for audio operators
// ---------------------------------------------------------------------------

void CadenceBridge::update_sources(double time, CompiledGraph& cg) {
    for (uint32_t idx : cg.audio_order) {
        auto& cn = cg.nodes[idx];
        if (!cn.loader || !cn.loader->has_main_thread_update()) continue;
        cn.loader->main_thread_update(
            cn.instance, time,
            cn.file_param_ptrs.empty() ? nullptr : cn.file_param_ptrs.data(),
            static_cast<uint32_t>(cn.file_param_ptrs.size()));
    }
}

void CadenceBridge::set_solo_active_set(const std::vector<bool>& set) {
    // Write into inactive snapshot, will be published on next push_to_audio
    int write_idx = 1 - param_active_.load(std::memory_order_acquire);
    snapshots_[write_idx].solo_active_set = set;
}

} // namespace vivid
