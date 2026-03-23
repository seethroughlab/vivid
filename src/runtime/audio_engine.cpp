#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "runtime/audio_engine.h"
#include "runtime/crash_guard.h"
#include "operator_api/port_type_registry.h"
#include "operator_api/type_id.h"
#include "runtime/shared_handle_registry.h"
#include "runtime/scheduler.h"
#include "common/topo_sort.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>

namespace vivid {

static float unset_signal_output_sentinel() {
    return std::numeric_limits<float>::quiet_NaN();
}

// Compute a linear scale equivalent from ConnectionDef remap fields.
// Audio engine wires use a simple float scale; this extracts the gain factor.
static float remap_to_scale(const ConnectionDef& c) {
    float range = c.from_max - c.from_min;
    return (range != 0.0f) ? (c.to_max - c.to_min) / range : 1.0f;
}

AudioEngine::AudioEngine() = default;

void AudioEngine::init_audio_node_state(AudioNodeState& ns, const VividOperatorDescriptor* desc,
                                        const std::unordered_map<std::string, float>* param_overrides) {
    ns.input_port_count = 0;
    ns.output_port_count = 0;
    ns.input_port_indices.clear();
    ns.output_port_indices.clear();
    ns.param_indices.clear();
    ns.input_port_types.clear();
    ns.output_port_types.clear();
    ns.has_spread_ports = false;
    ns.has_string_input_ports = false;
    ns.has_custom_input_ports = false;
    ns.has_custom_output_ports = false;

    ns.descriptor_input_channels.clear();
    ns.descriptor_output_channels.clear();

    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            ns.input_port_indices[desc->ports[i].name] = ns.input_port_count++;
            ns.input_port_types.push_back(desc->ports[i].type);
            ns.descriptor_input_channels.push_back(desc->ports[i].channels);
        } else {
            ns.output_port_indices[desc->ports[i].name] = ns.output_port_count++;
            ns.output_port_types.push_back(desc->ports[i].type);
            ns.descriptor_output_channels.push_back(desc->ports[i].channels);
        }
        if (desc->ports[i].type == VIVID_PORT_SPREAD) {
            ns.has_spread_ports = true;
        }
        if (desc->ports[i].direction == VIVID_PORT_INPUT &&
            desc->ports[i].type == VIVID_PORT_STRING) {
            ns.has_string_input_ports = true;
        }
        if (desc->ports[i].direction == VIVID_PORT_INPUT &&
            vivid_is_custom_port_type(desc->ports[i].type)) {
            ns.has_custom_input_ports = true;
        }
        if (desc->ports[i].direction == VIVID_PORT_OUTPUT &&
            vivid_is_custom_port_type(desc->ports[i].type)) {
            ns.has_custom_output_ports = true;
        }
    }

    // Channel counts default to 1; build() will run negotiation and resize if needed
    ns.input_channel_counts.assign(ns.input_port_count, 1);
    ns.output_channel_counts.assign(ns.output_port_count, 1);
    ns.is_mono_autodup = false;

    ns.input_buffers.resize(ns.input_port_count, std::vector<float>(kBufferSize, 0.0f));
    ns.output_buffers.resize(ns.output_port_count, std::vector<float>(kBufferSize, 0.0f));

    // Pre-allocate spread data structures
    ns.spread_inputs.resize(ns.input_port_count);
    ns.spread_outputs.resize(ns.output_port_count);
    ns.spread_in_ports.resize(ns.input_port_count);
    ns.spread_out_ports.resize(ns.output_port_count);
    for (uint32_t p = 0; p < ns.input_port_count; ++p) {
        ns.spread_in_ports[p].data = ns.spread_inputs[p].data;
        ns.spread_in_ports[p].length = 0;
        ns.spread_in_ports[p].capacity = SpreadSnapshot::kMaxLength;
    }
    for (uint32_t p = 0; p < ns.output_port_count; ++p) {
        ns.spread_out_ports[p].data = ns.spread_outputs[p].data;
        ns.spread_out_ports[p].length = 0;
        ns.spread_out_ports[p].capacity = SpreadSnapshot::kMaxLength;
    }
    ns.input_string_values.assign(ns.input_port_count, "");
    ns.c_input_string_values.assign(ns.input_port_count, nullptr);
    ns.custom_input_values.assign(ns.input_port_count, nullptr);

    // Float CV input defaults (from descriptor default_value for FLOAT input ports)
    ns.float_input_defaults.clear();
    ns.float_input_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT &&
            desc->ports[i].type == VIVID_PORT_SIGNAL) {
            ns.float_input_defaults.push_back(desc->ports[i].default_value);
            ns.float_input_count++;
        }
    }
    ns.float_input_values = ns.float_input_defaults;

    // SIGNAL/FLOAT output ports (scalar values + auto-extraction from buffers)
    ns.float_output_values.clear();
    ns.float_output_count = 0;
    ns.signal_output_extractions.clear();
    {
        uint32_t out_idx = 0;
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            if (desc->ports[i].direction == VIVID_PORT_OUTPUT) {
                if (desc->ports[i].type == VIVID_PORT_SIGNAL) {
                    // SIGNAL outputs get both a buffer (already allocated above)
                    // and a float_output_values slot for scalar extraction
                    ns.signal_output_extractions.push_back({out_idx, ns.float_output_count});
                    ns.float_output_count++;
                }
                out_idx++;
            }
        }
    }
    ns.float_output_values.resize(ns.float_output_count, 0.0f);

    // Custom output ports (audio-domain custom outputs)
    ns.custom_output_ptrs.clear();
    ns.custom_output_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_OUTPUT &&
            vivid_is_custom_port_type(desc->ports[i].type)) {
            ns.custom_output_count++;
        }
    }
    ns.custom_output_ptrs.resize(ns.custom_output_count, nullptr);

    // Pre-allocate pointer arrays (avoids audio-thread allocation)
    ns.in_ptrs.resize(ns.input_port_count);
    ns.out_ptrs.resize(ns.output_port_count);

    ns.param_count = desc->param_count;
    ns.param_values.resize(desc->param_count);
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        ns.param_values[i] = desc->params[i].default_value;
        ns.param_indices[desc->params[i].name] = i;
    }
    if (param_overrides) {
        for (const auto& [pname, pval] : *param_overrides) {
            auto pi = ns.param_indices.find(pname);
            if (pi != ns.param_indices.end()) {
                ns.param_values[pi->second] = pval;
            }
        }
    }
}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::build(const Graph& graph, OperatorRegistry& registry, const Scheduler& scheduler) {
    nodes_.clear();
    wires_.clear();
    audio_float_wires_.clear();
    audio_custom_wires_.clear();
    audio_spread_wires_.clear();
    cross_wires_.clear();
    cross_spread_wires_.clear();
    cross_string_wires_.clear();
    cross_custom_wires_.clear();
    cross_float_wires_.clear();

    // Map node id → audio node index
    std::unordered_map<std::string, uint32_t> audio_node_index;

    // 1. Extract audio-domain nodes from the graph
    for (const auto& ndef : graph.nodes()) {
        OperatorLoader* loader = registry.find(ndef.type);
        if (!loader) continue;

        const VividOperatorDescriptor* desc = loader->descriptor();
        if (desc->domain != VIVID_DOMAIN_AUDIO) continue;

        AudioNodeState ns;
        ns.node_id = ndef.id;
        ns.loader = loader;
        ns.instance = loader->create_instance();
        init_audio_node_state(ns, desc, &ndef.params);

        audio_node_index[ndef.id] = static_cast<uint32_t>(nodes_.size());
        nodes_.push_back(std::move(ns));
    }

    if (nodes_.empty()) return false;

    // 2. Resolve connections
    uint32_t n = static_cast<uint32_t>(nodes_.size());
    std::vector<std::vector<uint32_t>> adj(n);
    std::vector<uint32_t> in_degree(n, 0);

    // Build a lookup for control scheduler nodes by id
    std::unordered_map<std::string, size_t> control_node_map;
    for (size_t i = 0; i < scheduler.nodes().size(); ++i) {
        control_node_map[scheduler.nodes()[i].node_id] = i;
    }

    for (const auto& conn : graph.connections()) {
        auto from_audio = audio_node_index.find(conn.from_node);
        auto to_audio = audio_node_index.find(conn.to_node);

        if (from_audio != audio_node_index.end() && to_audio != audio_node_index.end()) {
            // Audio → Audio wire
            uint32_t fi = from_audio->second;
            uint32_t ti = to_audio->second;
            auto& from_ns = nodes_[fi];
            auto& to_ns = nodes_[ti];

            auto fp_it = from_ns.output_port_indices.find(conn.from_port);
            auto tp_it = to_ns.input_port_indices.find(conn.to_port);
            if (fp_it != from_ns.output_port_indices.end() &&
                tp_it != to_ns.input_port_indices.end()) {
                // Type-check: route to specialized wire types
                VividPortType from_ptype = fp_it->second < from_ns.output_port_types.size()
                    ? from_ns.output_port_types[fp_it->second] : VIVID_PORT_AUDIO;
                VividPortType to_ptype = tp_it->second < to_ns.input_port_types.size()
                    ? to_ns.input_port_types[tp_it->second] : VIVID_PORT_AUDIO;

                if (from_ptype == VIVID_PORT_SPREAD && to_ptype == VIVID_PORT_SPREAD) {
                    AudioSpreadWire sw;
                    sw.from_node_idx = fi;
                    sw.from_port_idx = fp_it->second;
                    sw.to_node_idx = ti;
                    sw.to_port_idx = tp_it->second;
                    sw.scale = remap_to_scale(conn);
                    audio_spread_wires_.push_back(sw);
                } else if (from_ptype == VIVID_PORT_SIGNAL && to_ptype == VIVID_PORT_SIGNAL) {
                    // SIGNAL→SIGNAL: always create a scalar float wire so that
                    // operators reading input_float_values (e.g. ChordProgression
                    // reading beat_phase) receive the value regardless of whether
                    // the source writes to output_float_values or output_buffers.
                    // Additionally create a buffer wire when the source has a
                    // buffer slot, for operators that read per-sample input_buffers.
                    uint32_t from_float_ord = 0;
                    for (uint32_t pi = 0; pi < fp_it->second; ++pi) {
                        if (from_ns.output_port_types[pi] == VIVID_PORT_SIGNAL) from_float_ord++;
                    }
                    uint32_t to_float_ord = 0;
                    for (uint32_t pi = 0; pi < tp_it->second; ++pi) {
                        if (to_ns.input_port_types[pi] == VIVID_PORT_SIGNAL) to_float_ord++;
                    }
                    AudioFloatPortWire fw;
                    fw.from_node_idx = fi;
                    fw.from_float_port_idx = from_float_ord;
                    fw.to_node_idx = ti;
                    fw.to_float_port_idx = to_float_ord;
                    fw.scale = remap_to_scale(conn);
                    audio_float_wires_.push_back(fw);

                    bool source_has_buffer = false;
                    for (const auto& se : from_ns.signal_output_extractions) {
                        if (se.port_idx == fp_it->second) { source_has_buffer = true; break; }
                    }
                    if (source_has_buffer) {
                        AudioWire w;
                        w.from_node_idx = fi;
                        w.from_port_idx = fp_it->second;
                        w.to_node_idx = ti;
                        w.to_port_idx = tp_it->second;
                        w.scale = remap_to_scale(conn);
                        wires_.push_back(w);
                    }
                } else if (vivid_is_custom_port_type(from_ptype) &&
                           vivid_is_custom_port_type(to_ptype) &&
                           from_ptype == to_ptype) {
                    // Custom→Custom: opaque type wire between audio nodes
                    // Compute ordinals among custom-only ports
                    uint32_t from_custom_ord = 0;
                    for (uint32_t pi = 0; pi < fp_it->second; ++pi) {
                        if (vivid_is_custom_port_type(from_ns.output_port_types[pi])) from_custom_ord++;
                    }
                    uint32_t to_custom_ord = 0;
                    for (uint32_t pi = 0; pi < tp_it->second; ++pi) {
                        if (vivid_is_custom_port_type(to_ns.input_port_types[pi])) to_custom_ord++;
                    }
                    AudioCustomWire cw;
                    cw.from_node_idx = fi;
                    cw.from_port_idx = from_custom_ord;
                    cw.to_node_idx = ti;
                    cw.to_port_idx = to_custom_ord;
                    cw.type_id = from_ptype;
                    audio_custom_wires_.push_back(cw);
                } else if (from_ptype == VIVID_PORT_SIGNAL && to_ptype == VIVID_PORT_AUDIO) {
                    // SIGNAL→AUDIO: buffer wire (1-channel SIGNAL output → AUDIO input)
                    AudioWire w;
                    w.from_node_idx = fi;
                    w.from_port_idx = fp_it->second;
                    w.to_node_idx = ti;
                    w.to_port_idx = tp_it->second;
                    w.scale = remap_to_scale(conn);
                    wires_.push_back(w);
                } else if (from_ptype == VIVID_PORT_AUDIO && to_ptype == VIVID_PORT_SIGNAL) {
                    // AUDIO→SIGNAL input: SIGNAL inputs on audio ops are scalar (float_input_values),
                    // so we extract the last sample from the AUDIO buffer and deliver via float wire
                    uint32_t to_float_ord = 0;
                    for (uint32_t pi = 0; pi < tp_it->second; ++pi) {
                        if (to_ns.input_port_types[pi] == VIVID_PORT_SIGNAL) to_float_ord++;
                    }
                    // Source is AUDIO — we need to extract last sample. For now, route as float wire
                    // using the source's float_output (populated by auto-extraction or operator).
                    // Find or create a float ordinal for the AUDIO output port — AUDIO ports don't
                    // normally have float ordinals, so this case is a type mismatch we warn about.
                    std::fprintf(stderr,
                        "[vivid] AudioEngine: AUDIO→SIGNAL input wire not yet supported: %s/%s -> %s/%s; wire skipped\n",
                        conn.from_node.c_str(), conn.from_port.c_str(),
                        conn.to_node.c_str(), conn.to_port.c_str());
                    continue;
                } else if (vivid_port_type_compatible(from_ptype, to_ptype)) {
                    // Compatible types (e.g. AUDIO→AUDIO) — generic wire
                    AudioWire w;
                    w.from_node_idx = fi;
                    w.from_port_idx = fp_it->second;
                    w.to_node_idx = ti;
                    w.to_port_idx = tp_it->second;
                    w.scale = remap_to_scale(conn);
                    wires_.push_back(w);
                } else {
                    // Type mismatch — warn and skip
                    std::fprintf(stderr,
                        "[vivid] AudioEngine: port type mismatch: %s/%s (type 0x%x) -> %s/%s (type 0x%x); wire skipped\n",
                        conn.from_node.c_str(), conn.from_port.c_str(), from_ptype,
                        conn.to_node.c_str(), conn.to_port.c_str(), to_ptype);
                    continue;
                }

                adj[fi].push_back(ti);
                in_degree[ti]++;
            }
        } else if (from_audio == audio_node_index.end() && to_audio != audio_node_index.end()) {
            // Control → Audio cross-domain wire
            uint32_t ti = to_audio->second;
            auto& to_ns = nodes_[ti];

            // Find the control node's output port index
            auto ctrl_it = control_node_map.find(conn.from_node);
            if (ctrl_it == control_node_map.end()) continue;

            const auto& ctrl_ns = scheduler.nodes()[ctrl_it->second];
            auto cp_it = ctrl_ns.output_port_indices.find(conn.from_port);
            if (cp_it == ctrl_ns.output_port_indices.end()) continue;

            // Try param mapping first (scalar control → audio param)
            auto pp_it = to_ns.param_indices.find(conn.to_port);
            if (pp_it != to_ns.param_indices.end()) {
                CrossDomainWire cw;
                cw.control_node_id = conn.from_node;
                cw.control_output_port_idx = cp_it->second;
                cw.audio_node_idx = ti;
                cw.audio_param_idx = pp_it->second;
                cw.scale = remap_to_scale(conn);
                cross_wires_.push_back(cw);
            } else {
                // Try input port mapping (for CONTROL_SPREAD cross-domain wires)
                auto ip_it = to_ns.input_port_indices.find(conn.to_port);
                if (ip_it != to_ns.input_port_indices.end() &&
                    ip_it->second < to_ns.input_port_types.size() &&
                    to_ns.input_port_types[ip_it->second] == VIVID_PORT_SPREAD) {
                    CrossDomainSpreadWire sw;
                    sw.control_node_id = conn.from_node;
                    sw.control_spread_port_idx = cp_it->second;
                    sw.audio_node_idx = ti;
                    sw.audio_port_idx = ip_it->second;
                    sw.scale = remap_to_scale(conn);
                    cross_spread_wires_.push_back(sw);
                } else if (ip_it != to_ns.input_port_indices.end() &&
                           ip_it->second < to_ns.input_port_types.size() &&
                           cp_it->second < ctrl_ns.output_port_types.size() &&
                           to_ns.input_port_types[ip_it->second] == VIVID_PORT_STRING &&
                           ctrl_ns.output_port_types[cp_it->second] == VIVID_PORT_STRING) {
                    CrossDomainStringWire sw;
                    sw.control_node_id = conn.from_node;
                    sw.control_output_port_idx = cp_it->second;
                    sw.audio_node_idx = ti;
                    sw.audio_port_idx = ip_it->second;
                    cross_string_wires_.push_back(sw);
                } else if (ip_it != to_ns.input_port_indices.end() &&
                           ip_it->second < to_ns.input_port_types.size() &&
                           cp_it->second < ctrl_ns.output_port_types.size() &&
                           vivid_is_custom_port_type(ctrl_ns.output_port_types[cp_it->second]) &&
                           vivid_is_custom_port_type(to_ns.input_port_types[ip_it->second])) {
                    // Custom-type wire: require exact port type match
                    VividPortType from_ptype = ctrl_ns.output_port_types[cp_it->second];
                    VividPortType to_ptype   = to_ns.input_port_types[ip_it->second];
                    if (from_ptype == to_ptype) {
                        VividPortTypeInfo info{};
                        if (!vivid_lookup_port_type(from_ptype, &info)) {
                            std::fprintf(stderr, "[vivid] AudioEngine: custom port type 0x%x not registered; wire rejected\n", from_ptype);
                            continue;
                        }
                        if (!info.audio_safe) {
                            std::fprintf(stderr,
                                         "[vivid] AudioEngine: custom port type '%s' [%s] is not audio-safe; wire rejected\n",
                                         info.type_name, info.stable_type_id);
                            continue;
                        }
                        if ((info.transport == VIVID_PORT_TRANSPORT_CUSTOM_VALUE ||
                             info.transport == VIVID_PORT_TRANSPORT_CUSTOM_REF) &&
                            info.payload_size > CustomPortSnapshot::kMaxBytes) {
                            std::fprintf(stderr,
                                         "[vivid] AudioEngine: custom payload '%s' [%s] (%u bytes) exceeds max (%u); wire rejected\n",
                                         info.type_name, info.stable_type_id,
                                         info.payload_size, CustomPortSnapshot::kMaxBytes);
                            continue;
                        }
                        CrossDomainCustomWire hw;
                        hw.source_node_id         = conn.from_node;
                        hw.source_output_port_idx = cp_it->second;
                        hw.audio_node_idx         = ti;
                        hw.audio_port_idx         = ip_it->second;
                        hw.type_id                = from_ptype;
                        hw.transport              = info.transport;
                        hw.payload_size           = info.payload_size;
                        cross_custom_wires_.push_back(std::move(hw));
                    }
                } else if (ip_it != to_ns.input_port_indices.end() &&
                           ip_it->second < to_ns.input_port_types.size() &&
                           to_ns.input_port_types[ip_it->second] == VIVID_PORT_SIGNAL) {
                    // Control float output → audio FLOAT input port (CV modulation)
                    uint32_t float_ord = 0;
                    for (uint32_t pi = 0; pi < ip_it->second; ++pi) {
                        if (to_ns.input_port_types[pi] == VIVID_PORT_SIGNAL) float_ord++;
                    }
                    CrossDomainFloatPortWire fw;
                    fw.control_node_id         = conn.from_node;
                    fw.control_output_port_idx = cp_it->second;
                    fw.audio_node_idx          = ti;
                    fw.audio_float_port_idx    = float_ord;
                    fw.scale                   = remap_to_scale(conn);
                    cross_float_wires_.push_back(fw);
                }
            }
        }
    }

    // 3. Topological sort
    auto sorted_order = kahn_sort(n, adj, in_degree);
    if (sorted_order.empty()) {
        std::fprintf(stderr, "[vivid] AudioEngine: cycle detected in audio subgraph\n");
        return false;
    }

    // 4. Reorder nodes to sorted order
    std::vector<uint32_t> old_to_new(n);
    for (uint32_t i = 0; i < n; ++i) {
        old_to_new[sorted_order[i]] = i;
    }

    std::vector<AudioNodeState> sorted_nodes(n);
    for (uint32_t i = 0; i < n; ++i) {
        sorted_nodes[old_to_new[i]] = std::move(nodes_[i]);
    }
    nodes_ = std::move(sorted_nodes);

    for (auto& w : wires_) {
        w.from_node_idx = old_to_new[w.from_node_idx];
        w.to_node_idx = old_to_new[w.to_node_idx];
    }
    for (auto& fw : audio_float_wires_) {
        fw.from_node_idx = old_to_new[fw.from_node_idx];
        fw.to_node_idx = old_to_new[fw.to_node_idx];
    }
    for (auto& cw : audio_custom_wires_) {
        cw.from_node_idx = old_to_new[cw.from_node_idx];
        cw.to_node_idx = old_to_new[cw.to_node_idx];
    }
    for (auto& sw : audio_spread_wires_) {
        sw.from_node_idx = old_to_new[sw.from_node_idx];
        sw.to_node_idx = old_to_new[sw.to_node_idx];
    }
    for (auto& cw : cross_wires_) {
        cw.audio_node_idx = old_to_new[cw.audio_node_idx];
    }
    for (auto& sw : cross_spread_wires_) {
        sw.audio_node_idx = old_to_new[sw.audio_node_idx];
    }
    for (auto& sw : cross_string_wires_) {
        sw.audio_node_idx = old_to_new[sw.audio_node_idx];
    }
    for (auto& dw : cross_custom_wires_) {
        dw.audio_node_idx = old_to_new[dw.audio_node_idx];
    }
    for (auto& fw : cross_float_wires_) {
        fw.audio_node_idx = old_to_new[fw.audio_node_idx];
    }

    // -----------------------------------------------------------------------
    // Channel negotiation (Phase 2): resolve per-port channel counts
    // -----------------------------------------------------------------------
    // Pass 1: Set explicit channels from descriptors
    for (uint32_t i = 0; i < n; ++i) {
        auto& ns = nodes_[i];
        for (uint32_t p = 0; p < ns.input_port_count; ++p) {
            uint8_t dc = (p < ns.descriptor_input_channels.size()) ? ns.descriptor_input_channels[p] : 0;
            if (dc > 0) ns.input_channel_counts[p] = dc;
            // else stays 1, will be overridden by wire propagation
        }
        for (uint32_t p = 0; p < ns.output_port_count; ++p) {
            uint8_t dc = (p < ns.descriptor_output_channels.size()) ? ns.descriptor_output_channels[p] : 0;
            if (dc > 0) ns.output_channel_counts[p] = dc;
        }
    }

    // Pass 2: Propagate via wires (topo order ensures sources are resolved first)
    // Source output channel count flows to destination input; auto outputs inherit from inputs
    for (uint32_t i = 0; i < n; ++i) {
        auto& ns = nodes_[i];

        // For auto (descriptor=0) output ports: inherit max of input channels
        uint8_t max_input_ch = 1;
        for (uint32_t p = 0; p < ns.input_port_count; ++p) {
            if (ns.input_port_types[p] == VIVID_PORT_AUDIO && ns.input_channel_counts[p] > max_input_ch)
                max_input_ch = ns.input_channel_counts[p];
        }
        for (uint32_t p = 0; p < ns.output_port_count; ++p) {
            uint8_t dc = (p < ns.descriptor_output_channels.size()) ? ns.descriptor_output_channels[p] : 0;
            if (dc == 0 && ns.output_port_types[p] == VIVID_PORT_AUDIO) {
                ns.output_channel_counts[p] = max_input_ch;
            }
        }

        // Propagate this node's output channel counts to downstream inputs
        for (auto& w : wires_) {
            if (w.from_node_idx == i) {
                uint8_t src_ch = ns.output_channel_counts[w.from_port_idx];
                auto& to_ns = nodes_[w.to_node_idx];
                uint8_t dc = (w.to_port_idx < to_ns.descriptor_input_channels.size())
                             ? to_ns.descriptor_input_channels[w.to_port_idx] : 0;
                if (dc == 0) {
                    // Auto input: take max of current and incoming
                    if (src_ch > to_ns.input_channel_counts[w.to_port_idx])
                        to_ns.input_channel_counts[w.to_port_idx] = src_ch;
                }
            }
        }
    }

    // Pass 3: Detect auto-dup nodes (mono operators in multi-channel chains)
    for (uint32_t i = 0; i < n; ++i) {
        auto& ns = nodes_[i];
        // Check if ALL audio ports have explicit channels <= 1 in descriptor
        bool all_mono_declared = true;
        for (uint32_t p = 0; p < ns.input_port_count; ++p) {
            if (ns.input_port_types[p] != VIVID_PORT_AUDIO) continue;
            uint8_t dc = (p < ns.descriptor_input_channels.size()) ? ns.descriptor_input_channels[p] : 0;
            if (dc > 1) { all_mono_declared = false; break; }
        }
        if (all_mono_declared) {
            for (uint32_t p = 0; p < ns.output_port_count; ++p) {
                if (ns.output_port_types[p] != VIVID_PORT_AUDIO) continue;
                uint8_t dc = (p < ns.descriptor_output_channels.size()) ? ns.descriptor_output_channels[p] : 0;
                if (dc > 1) { all_mono_declared = false; break; }
            }
        }

        // Find max wire channel count touching this node
        uint8_t max_wire_ch = 1;
        for (const auto& w : wires_) {
            if (w.to_node_idx == i) {
                uint8_t src_ch = nodes_[w.from_node_idx].output_channel_counts[w.from_port_idx];
                if (src_ch > max_wire_ch) max_wire_ch = src_ch;
            }
        }

        if (all_mono_declared && max_wire_ch > 1) {
            ns.is_mono_autodup = true;
            // Mono processing stays at 1 channel per port internally
            for (uint32_t p = 0; p < ns.input_port_count; ++p)
                if (ns.input_port_types[p] == VIVID_PORT_AUDIO)
                    ns.input_channel_counts[p] = 1;
            for (uint32_t p = 0; p < ns.output_port_count; ++p)
                if (ns.output_port_types[p] == VIVID_PORT_AUDIO)
                    ns.output_channel_counts[p] = 1;
        }
    }

    // -----------------------------------------------------------------------
    // Multi-channel buffer allocation (Phase 3)
    // -----------------------------------------------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        auto& ns = nodes_[i];
        if (ns.is_mono_autodup) {
            // Find the wire channel count for auto-dup sizing
            uint8_t wire_ch = 1;
            for (const auto& w : wires_) {
                if (w.to_node_idx == i) {
                    uint8_t src_ch = nodes_[w.from_node_idx].output_channel_counts[w.from_port_idx];
                    if (src_ch > wire_ch) wire_ch = src_ch;
                }
            }
            // Multi-channel input/output buffers for deinterleave/interleave
            for (uint32_t p = 0; p < ns.input_port_count; ++p) {
                if (ns.input_port_types[p] == VIVID_PORT_AUDIO)
                    ns.input_buffers[p].resize(wire_ch * kBufferSize, 0.0f);
            }
            for (uint32_t p = 0; p < ns.output_port_count; ++p) {
                if (ns.output_port_types[p] == VIVID_PORT_AUDIO)
                    ns.output_buffers[p].resize(wire_ch * kBufferSize, 0.0f);
            }
        } else {
            for (uint32_t p = 0; p < ns.input_port_count; ++p) {
                if (ns.input_port_types[p] == VIVID_PORT_AUDIO)
                    ns.input_buffers[p].resize(ns.input_channel_counts[p] * kBufferSize, 0.0f);
            }
            for (uint32_t p = 0; p < ns.output_port_count; ++p) {
                if (ns.output_port_types[p] == VIVID_PORT_AUDIO)
                    ns.output_buffers[p].resize(ns.output_channel_counts[p] * kBufferSize, 0.0f);
            }
        }
    }

    // Resolve wire channel counts (Phase 5)
    for (auto& w : wires_) {
        auto& from_ns = nodes_[w.from_node_idx];
        auto& to_ns = nodes_[w.to_node_idx];
        w.from_channels = from_ns.is_mono_autodup
            ? [&]() -> uint8_t { // auto-dup: wire carries the upstream channel count
                for (const auto& uw : wires_) {
                    if (uw.to_node_idx == w.from_node_idx) {
                        uint8_t sc = nodes_[uw.from_node_idx].output_channel_counts[uw.from_port_idx];
                        if (sc > 1) return sc;
                    }
                }
                return from_ns.output_channel_counts[w.from_port_idx];
            }()
            : from_ns.output_channel_counts[w.from_port_idx];
        w.to_channels = to_ns.is_mono_autodup
            ? w.from_channels  // auto-dup input accepts whatever comes in
            : to_ns.input_channel_counts[w.to_port_idx];
    }

    // -----------------------------------------------------------------------
    // Auto-duplication setup (Phase 4)
    // -----------------------------------------------------------------------
    auto_dup_groups_.clear();
    node_to_dup_group_.clear();
    for (uint32_t i = 0; i < n; ++i) {
        auto& ns = nodes_[i];
        if (!ns.is_mono_autodup) continue;

        // Determine channel count from incoming wires
        uint8_t ch = 1;
        for (const auto& w : wires_) {
            if (w.to_node_idx == i) {
                uint8_t src_ch = nodes_[w.from_node_idx].output_channel_counts[w.from_port_idx];
                // For auto-dup source nodes, look at their wire channel count
                if (nodes_[w.from_node_idx].is_mono_autodup) src_ch = w.from_channels;
                if (src_ch > ch) ch = src_ch;
            }
        }
        if (ch <= 1) {
            ns.is_mono_autodup = false;
            continue;
        }

        AutoDupGroup group;
        group.node_idx = i;
        group.channel_count = ch;
        group.instances.resize(ch);
        group.instances[0] = ns.instance;  // primary

        // Create additional instances for channels 1..N-1
        for (uint8_t c = 1; c < ch; ++c) {
            group.instances[c] = ns.loader->create_instance();
        }

        // Allocate per-channel mono buffers
        group.per_ch_inputs.resize(ch);
        group.per_ch_outputs.resize(ch);
        group.per_ch_in_ptrs.resize(ch);
        group.per_ch_out_ptrs.resize(ch);
        for (uint8_t c = 0; c < ch; ++c) {
            group.per_ch_inputs[c].resize(ns.input_port_count, std::vector<float>(kBufferSize, 0.0f));
            group.per_ch_outputs[c].resize(ns.output_port_count, std::vector<float>(kBufferSize, 0.0f));
            group.per_ch_in_ptrs[c].resize(ns.input_port_count);
            group.per_ch_out_ptrs[c].resize(ns.output_port_count);
            for (uint32_t p = 0; p < ns.input_port_count; ++p)
                group.per_ch_in_ptrs[c][p] = group.per_ch_inputs[c][p].data();
            for (uint32_t p = 0; p < ns.output_port_count; ++p)
                group.per_ch_out_ptrs[c][p] = group.per_ch_outputs[c][p].data();
        }

        node_to_dup_group_[i] = static_cast<uint32_t>(auto_dup_groups_.size());
        auto_dup_groups_.push_back(std::move(group));
    }

    // Initialize param snapshots
    for (auto& snap : snapshots_) {
        snap.node_params.resize(n);
        snap.float_input_values.resize(n);
        snap.spread_inputs.resize(n);
        snap.input_string_values.resize(n);
        snap.custom_inputs.resize(n);
        snap.role_bindings.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            snap.node_params[i] = nodes_[i].param_values;
            snap.float_input_values[i] = nodes_[i].float_input_defaults;
            snap.spread_inputs[i].resize(nodes_[i].input_port_count);
            snap.input_string_values[i].assign(nodes_[i].input_port_count, "");
            snap.custom_inputs[i].resize(nodes_[i].input_port_count);
        }
    }

    // Initialize analysis snapshots
    for (auto& snap : analysis_snapshots_) {
        snap.rms.resize(n, 0.0f);
        snap.peak.resize(n, 0.0f);
        snap.waveform.resize(n);
        snap.spread_outputs.resize(n);
        snap.float_outputs.resize(n);
        snap.errored.resize(n, false);
        snap.error_msgs.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            snap.spread_outputs[i].resize(nodes_[i].output_port_count);
            snap.float_outputs[i].resize(nodes_[i].float_output_count, 0.0f);
        }
    }

    // Build node_id → index map
    node_id_to_index_.clear();
    for (uint32_t i = 0; i < n; ++i) {
        node_id_to_index_[nodes_[i].node_id] = static_cast<int>(i);
    }

    // Initialize waveform ring buffers (one per audio node)
    waveform_rings_.resize(n);
    waveform_ring_pos_.resize(n, 0);
    for (auto& ring : waveform_rings_) ring.fill(0.0f);

    // Build param mappings: ALL audio nodes → scheduler nodes (for push_params)
    param_mappings_.clear();
    analysis_mappings_.clear();
    for (uint32_t ai = 0; ai < n; ++ai) {
        for (uint32_t si = 0; si < static_cast<uint32_t>(scheduler.nodes().size()); ++si) {
            if (scheduler.nodes()[si].node_id == nodes_[ai].node_id) {
                // Every audio node needs param propagation from scheduler
                ParamMapping pm;
                pm.audio_engine_idx = ai;
                pm.scheduler_node_idx = si;

                // Build spread output mappings for CONTROL_SPREAD output ports
                for (uint32_t op = 0; op < nodes_[ai].output_port_count; ++op) {
                    if (op < nodes_[ai].output_port_types.size() &&
                        nodes_[ai].output_port_types[op] == VIVID_PORT_SPREAD) {
                        const auto* desc = nodes_[ai].loader->descriptor();
                        uint32_t out_idx = 0;
                        for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
                            if (desc->ports[pi].direction == VIVID_PORT_OUTPUT) {
                                if (out_idx == op) {
                                    auto sched_it = scheduler.nodes()[si].output_port_indices.find(desc->ports[pi].name);
                                    if (sched_it != scheduler.nodes()[si].output_port_indices.end()) {
                                        pm.spread_output_mappings.push_back({op, sched_it->second});
                                    }
                                    break;
                                }
                                out_idx++;
                            }
                        }
                    }
                }

                // Build float output mappings: audio FLOAT outputs → scheduler outputs
                {
                    const auto* adesc = nodes_[ai].loader->descriptor();
                    uint32_t float_ord = 0;
                    for (uint32_t pi = 0; pi < adesc->port_count; ++pi) {
                        if (adesc->ports[pi].direction == VIVID_PORT_OUTPUT &&
                            adesc->ports[pi].type == VIVID_PORT_SIGNAL) {
                            auto sched_it = scheduler.nodes()[si].output_port_indices.find(adesc->ports[pi].name);
                            if (sched_it != scheduler.nodes()[si].output_port_indices.end()) {
                                pm.float_output_mappings.push_back({float_ord, sched_it->second});
                            }
                            float_ord++;
                        }
                    }
                }

                param_mappings_.push_back(std::move(pm));

                // Analysis mappings only for nodes with rms/peak/waveform ports
                auto rms_it = scheduler.nodes()[si].analysis_output_port_indices.find("rms");
                auto peak_it = scheduler.nodes()[si].analysis_output_port_indices.find("peak");
                auto wave_it = scheduler.nodes()[si].analysis_output_port_indices.find("waveform");
                if (rms_it != scheduler.nodes()[si].analysis_output_port_indices.end() &&
                    peak_it != scheduler.nodes()[si].analysis_output_port_indices.end() &&
                    wave_it != scheduler.nodes()[si].analysis_output_port_indices.end()) {
                    AudioToControlMapping m;
                    m.audio_engine_idx = ai;
                    m.scheduler_node_idx = si;
                    m.rms_port_idx = rms_it->second;
                    m.peak_port_idx = peak_it->second;
                    m.waveform_port_idx = wave_it->second;

                    analysis_mappings_.push_back(m);
                }
                break;
            }
        }
    }

    // Find audio_out sink node, or fall back to last node with output ports
    sink_node_idx_ = -1;
    for (uint32_t i = 0; i < n; ++i) {
        const auto* desc = nodes_[i].loader->descriptor();
        if (desc && std::string(desc->name) == "audio_out") {
            if (sink_node_idx_ == -1) {
                sink_node_idx_ = static_cast<int>(i);
            } else {
                std::fprintf(stderr, "[vivid] AudioEngine: warning: multiple audio_out nodes, using first\n");
            }
        }
    }
    if (sink_node_idx_ == -1) {
        std::fprintf(stderr, "[vivid] AudioEngine: no audio_out node — audio will be silent\n");
    }

    std::fprintf(stderr, "[vivid] Audio evaluation order:");
    for (uint32_t i = 0; i < n; ++i) {
        std::fprintf(stderr, "%s%s", (i == 0 ? " " : " -> "), nodes_[i].node_id.c_str());
    }
    std::fprintf(stderr, " (sink=%d, %zu param mappings, %zu analysis mappings)\n",
        sink_node_idx_, param_mappings_.size(), analysis_mappings_.size());

    return true;
}

bool AudioEngine::start(bool use_null_device) {
    if (nodes_.empty()) return false;

    device_ = new ma_device;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = kSampleRate;
    config.periodSizeInFrames = kBufferSize;
    config.dataCallback = &AudioEngine::ma_data_callback;
    config.pUserData = this;

    ma_result init_result;
    if (use_null_device) {
        ma_backend backends[] = { ma_backend_null };
        init_result = ma_device_init_ex(backends, 1, nullptr, &config, device_);
    } else {
        init_result = ma_device_init(nullptr, &config, device_);
    }
    if (init_result != MA_SUCCESS) {
        std::fprintf(stderr, "[vivid] AudioEngine: failed to init miniaudio device\n");
        delete device_;
        device_ = nullptr;
        return false;
    }

    running_ = true;
    if (ma_device_start(device_) != MA_SUCCESS) {
        running_ = false;
        std::fprintf(stderr, "[vivid] AudioEngine: failed to start miniaudio device\n");
        ma_device_uninit(device_);
        delete device_;
        device_ = nullptr;
        return false;
    }
    std::fprintf(stderr, "[vivid] AudioEngine: started (%u Hz, %u frames/buffer, %zu audio nodes)\n",
        kSampleRate, kBufferSize, nodes_.size());
    return true;
}

void AudioEngine::push_params(const Scheduler& scheduler) {
    int write_idx = 1 - active_.load(std::memory_order_acquire);
    auto& snap = snapshots_[write_idx];

    // Base: audio engine's own param values (initial defaults)
    for (size_t i = 0; i < nodes_.size(); ++i) {
        snap.node_params[i] = nodes_[i].param_values;
        snap.float_input_values[i] = nodes_[i].float_input_defaults;
        for (auto& s : snap.spread_inputs[i]) s.length = 0;
        snap.input_string_values[i].assign(nodes_[i].input_port_count, "");
        for (auto& di : snap.custom_inputs[i]) {
            di.clear();
        }
    }

    // Overlay: scheduler's param values (where set_param/inspector writes)
    for (const auto& m : param_mappings_) {
        const auto& sched_ns = scheduler.nodes()[m.scheduler_node_idx];
        for (const auto& [pname, ae_idx] : nodes_[m.audio_engine_idx].param_indices) {
            auto sit = sched_ns.param_indices.find(pname);
            if (sit != sched_ns.param_indices.end()) {
                snap.node_params[m.audio_engine_idx][ae_idx] = sched_ns.param_values[sit->second];
            }
        }
    }

    // Cross-domain wires override everything (live control modulation)
    for (const auto& cw : cross_wires_) {
        for (const auto& ctrl_ns : scheduler.nodes()) {
            if (ctrl_ns.node_id == cw.control_node_id) {
                float val = ctrl_ns.output_values[cw.control_output_port_idx] * cw.scale;
                snap.node_params[cw.audio_node_idx][cw.audio_param_idx] = val;
                break;
            }
        }
    }

    // Cross-domain spread wires: copy spread data from scheduler to param snapshot
    for (const auto& sw : cross_spread_wires_) {
        for (const auto& ctrl_ns : scheduler.nodes()) {
            if (ctrl_ns.node_id == sw.control_node_id) {
                auto& dst = snap.spread_inputs[sw.audio_node_idx][sw.audio_port_idx];
                if (sw.control_spread_port_idx < ctrl_ns.output_spreads.size()) {
                    const auto& src = ctrl_ns.output_spreads[sw.control_spread_port_idx];
                    uint32_t src_len = static_cast<uint32_t>(src.size());
                    dst.length = std::min(src_len, SpreadSnapshot::kMaxLength);
                    if (src_len > SpreadSnapshot::kMaxLength) {
                        if (!sw.truncation_warned) {
                            sw.truncation_warned = true;
                            std::fprintf(stderr, "[vivid] spread truncated from %u to %u "
                                "crossing to audio domain (wire: %s → audio)\n",
                                src_len, SpreadSnapshot::kMaxLength, sw.control_node_id.c_str());
                        }
                    }
                    if (dst.length > 0) {
                        std::memcpy(dst.data, src.data(), dst.length * sizeof(float));
                    }
                } else {
                    dst.length = 0;
                }
                break;
            }
        }
    }

    // Cross-domain string wires: copy string values from scheduler to audio input ports
    for (const auto& sw : cross_string_wires_) {
        for (const auto& ctrl_ns : scheduler.nodes()) {
            if (ctrl_ns.node_id == sw.control_node_id) {
                auto& dst = snap.input_string_values[sw.audio_node_idx][sw.audio_port_idx];
                if (sw.control_output_port_idx < ctrl_ns.output_string_values.size()) {
                    dst = ctrl_ns.output_string_values[sw.control_output_port_idx];
                } else {
                    dst.clear();
                }
                break;
            }
        }
    }

    // Cross-domain custom wires: snapshot typed opaque payloads.
    for (const auto& hw : cross_custom_wires_) {
        for (const auto& src_ns : scheduler.nodes()) {
            if (src_ns.node_id != hw.source_node_id) continue;
            auto& dst = snap.custom_inputs[hw.audio_node_idx][hw.audio_port_idx];
            dst.clear();
            // Find the custom output slot for this wire's source port
            void* data_ptr = nullptr;
            if (hw.source_output_port_idx < src_ns.output_port_types.size() &&
                vivid_is_custom_port_type(src_ns.output_port_types[hw.source_output_port_idx])) {
                for (uint32_t s = 0; s < src_ns.custom_output_port_indices.size(); ++s) {
                    if (src_ns.custom_output_port_indices[s] == hw.source_output_port_idx &&
                        s < src_ns.custom_outputs.size()) {
                        data_ptr = src_ns.custom_outputs[s];
                        break;
                    }
                }
            }
            if (data_ptr) {
                dst.type_id   = hw.type_id;
                dst.transport = hw.transport;
                // Audio crossing uses bounded POD snapshots for both
                // CUSTOM_VALUE payloads and audio-safe CUSTOM_REF ref-token
                // payloads. CUSTOM_REF is still a ref-token contract here,
                // not direct shared-object access on the audio thread.
                const uint32_t copy_size = std::min(hw.payload_size,
                                                     CustomPortSnapshot::kMaxBytes);
                std::memcpy(dst.bytes, data_ptr, copy_size);
                dst.byte_size = copy_size;
                dst.valid = true;
            }
            break;
        }
    }

    // Float CV wires: snapshot defaults, then apply live control → FLOAT port wires
    for (const auto& fw : cross_float_wires_) {
        for (const auto& ctrl_ns : scheduler.nodes()) {
            if (ctrl_ns.node_id == fw.control_node_id) {
                float val = ctrl_ns.output_values[fw.control_output_port_idx] * fw.scale;
                auto& dst = snap.float_input_values[fw.audio_node_idx];
                if (fw.audio_float_port_idx < dst.size())
                    dst[fw.audio_float_port_idx] = val;
                break;
            }
        }
    }

    // Role binding configs: snapshot pointers from scheduler NodeState
    for (const auto& m : param_mappings_) {
        const auto& sched_ns = scheduler.nodes()[m.scheduler_node_idx];
        auto& rb = snap.role_bindings[m.audio_engine_idx];
        if (!sched_ns.role_binding_configs.empty()) {
            rb.count = static_cast<uint32_t>(sched_ns.role_binding_configs.size());
            rb.configs = sched_ns.role_binding_configs.data();
        } else {
            rb.count = 0;
            rb.configs = nullptr;
        }
    }

    // Solo mode: map scheduler solo state to audio engine indices
    if (scheduler.is_solo_active()) {
        const auto& sched_solo = scheduler.solo_active_set();
        snap.solo_active_set.resize(nodes_.size(), false);
        for (const auto& m : param_mappings_) {
            if (m.scheduler_node_idx < sched_solo.size())
                snap.solo_active_set[m.audio_engine_idx] = sched_solo[m.scheduler_node_idx];
        }
    } else {
        snap.solo_active_set.clear();
    }

    active_.store(write_idx, std::memory_order_release);
}

void AudioEngine::update_sources(double time, const Scheduler& scheduler) {
    for (auto& ns : nodes_) {
        if (!ns.loader->has_main_thread_update()) continue;

        // Find matching scheduler node to get file param values
        const char** fps = nullptr;
        uint32_t fpc = 0;
        for (const auto& sched_ns : scheduler.nodes()) {
            if (sched_ns.node_id == ns.node_id) {
                if (!sched_ns.file_param_ptrs.empty()) {
                    fps = const_cast<const char**>(sched_ns.file_param_ptrs.data());
                }
                fpc = static_cast<uint32_t>(sched_ns.file_param_ptrs.size());
                break;
            }
        }

        ns.loader->main_thread_update(ns.instance, time, fps, fpc);
    }
}

void AudioEngine::inject_analysis(Scheduler& scheduler) {
    const auto& snap = analysis_snapshots_[analysis_active_.load(std::memory_order_acquire)];

    // Error state + spread outputs: all audio nodes
    for (const auto& pm : param_mappings_) {
        // Propagate audio error state to scheduler node for UI display
        if (pm.audio_engine_idx < snap.errored.size() && snap.errored[pm.audio_engine_idx]) {
            auto& sched_ns = scheduler.nodes_mut()[pm.scheduler_node_idx];
            sched_ns.errored = true;
            sched_ns.error_message = snap.error_msgs[pm.audio_engine_idx].data();
        } else {
            auto& sched_ns = scheduler.nodes_mut()[pm.scheduler_node_idx];
            if (sched_ns.is_audio) {
                sched_ns.errored = false;
                sched_ns.error_message.clear();
            }
        }
        // Inject FLOAT outputs from audio nodes back to scheduler
        for (const auto& fm : pm.float_output_mappings) {
            if (pm.audio_engine_idx < snap.float_outputs.size() &&
                fm.audio_float_ordinal < snap.float_outputs[pm.audio_engine_idx].size()) {
                scheduler.inject_external_output(pm.scheduler_node_idx, fm.scheduler_port_idx,
                                                  snap.float_outputs[pm.audio_engine_idx][fm.audio_float_ordinal]);
            }
        }

        // Inject CONTROL_SPREAD outputs from audio nodes back to scheduler
        for (const auto& sm : pm.spread_output_mappings) {
            if (pm.audio_engine_idx < snap.spread_outputs.size() &&
                sm.audio_port_idx < snap.spread_outputs[pm.audio_engine_idx].size()) {
                const auto& ss = snap.spread_outputs[pm.audio_engine_idx][sm.audio_port_idx];
                if (ss.length > 0) {
                    scheduler.inject_external_spread(pm.scheduler_node_idx, sm.scheduler_port_idx,
                                                     ss.data, ss.length);
                }
            }
        }
    }

    // Analysis data (rms/peak/waveform): only nodes with analysis ports
    for (const auto& m : analysis_mappings_) {
        scheduler.inject_external_output(m.scheduler_node_idx, m.rms_port_idx,
                                         snap.rms[m.audio_engine_idx]);
        scheduler.inject_external_output(m.scheduler_node_idx, m.peak_port_idx,
                                         snap.peak[m.audio_engine_idx]);
        if (m.audio_engine_idx < snap.waveform.size()) {
            scheduler.inject_external_spread(m.scheduler_node_idx, m.waveform_port_idx,
                                             snap.waveform[m.audio_engine_idx].data(),
                                             AnalysisSnapshot::kWaveformSamples);
        }
    }
}

const AnalysisSnapshot& AudioEngine::analysis_read() const {
    return analysis_snapshots_[analysis_active_.load(std::memory_order_acquire)];
}

int AudioEngine::audio_node_index(const std::string& node_id) const {
    auto it = node_id_to_index_.find(node_id);
    return (it != node_id_to_index_.end()) ? it->second : -1;
}

void AudioEngine::pause() {
    if (device_ && running_) {
        ma_device_stop(device_);
        running_ = false;
    }
}

void AudioEngine::resume() {
    if (device_ && !running_) {
        if (ma_device_start(device_) == MA_SUCCESS) {
            running_ = true;
        } else {
            std::fprintf(stderr, "[vivid] AudioEngine: failed to resume\n");
        }
    }
}

// Phase 1: Destroy old instances while the old dylib is still loaded.
// Must be called BEFORE scheduler.reload_operator() swaps the dylib.
void AudioEngine::pre_reload_operator(const std::string& type_name) {
    pause();
    reload_saved_.clear();

    for (uint32_t ni = 0; ni < static_cast<uint32_t>(nodes_.size()); ++ni) {
        auto& ns = nodes_[ni];
        const auto* desc = ns.loader->descriptor();
        if (!desc || std::string(desc->name) != type_name) continue;

        // Save param values by name
        ReloadSavedNode saved;
        saved.node_idx = ni;
        for (const auto& [name, idx] : ns.param_indices)
            saved.params[name] = ns.param_values[idx];
        reload_saved_.push_back(std::move(saved));

        // Destroy old instances using the still-valid old loader
        auto dup_it = node_to_dup_group_.find(ni);
        if (dup_it != node_to_dup_group_.end()) {
            auto& group = auto_dup_groups_[dup_it->second];
            for (uint8_t c = 1; c < group.channel_count; ++c) {
                if (group.instances[c]) {
                    ns.loader->destroy_instance(group.instances[c]);
                    group.instances[c] = nullptr;
                }
            }
        }
        if (ns.instance) {
            ns.loader->destroy_instance(ns.instance);
            ns.instance = nullptr;
        }
    }
    // Note: audio remains paused until post_reload_operator
}

// Phase 2: Create new instances from the new (already-swapped) loader.
// Must be called AFTER scheduler.reload_operator() swaps the dylib.
bool AudioEngine::post_reload_operator(const std::string& type_name, OperatorRegistry& registry) {
    OperatorLoader* new_loader = registry.find(type_name);
    if (!new_loader) {
        resume();
        reload_saved_.clear();
        return false;
    }
    const auto* new_desc = new_loader->descriptor();
    if (!new_desc) {
        resume();
        reload_saved_.clear();
        return false;
    }

    for (const auto& saved : reload_saved_) {
        auto& ns = nodes_[saved.node_idx];

        auto dup_it = node_to_dup_group_.find(saved.node_idx);
        uint8_t channel_count = 1;
        if (dup_it != node_to_dup_group_.end())
            channel_count = auto_dup_groups_[dup_it->second].channel_count;

        std::vector<void*> new_instances(channel_count, nullptr);
        for (uint8_t c = 0; c < channel_count; ++c) {
            new_instances[c] = new_loader->create_instance();
            if (!new_instances[c]) {
                for (uint8_t j = 0; j < c; ++j) {
                    if (new_instances[j]) new_loader->destroy_instance(new_instances[j]);
                }
                std::fprintf(stderr,
                             "[vivid] AudioEngine: failed to create replacement instance for '%s'\n",
                             type_name.c_str());
                resume();
                reload_saved_.clear();
                return false;
            }
        }

        ns.loader = new_loader;
        ns.instance = new_instances[0];
        init_audio_node_state(ns, new_desc, &saved.params);

        if (dup_it != node_to_dup_group_.end()) {
            auto& group = auto_dup_groups_[dup_it->second];
            group.instances[0] = ns.instance;
            for (uint8_t c = 1; c < channel_count; ++c)
                group.instances[c] = new_instances[c];
        }

        ns.errored = false;
        ns.error_message[0] = '\0';
    }

    reload_saved_.clear();

    // Update param snapshots to match new layout
    uint32_t n = static_cast<uint32_t>(nodes_.size());
    for (auto& snap : snapshots_) {
        snap.node_params.resize(n);
        snap.float_input_values.resize(n);
        snap.spread_inputs.resize(n);
        snap.input_string_values.resize(n);
        snap.custom_inputs.resize(n);
        snap.role_bindings.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            snap.node_params[i] = nodes_[i].param_values;
            snap.float_input_values[i] = nodes_[i].float_input_defaults;
            snap.spread_inputs[i].resize(nodes_[i].input_port_count);
            snap.input_string_values[i].assign(nodes_[i].input_port_count, "");
            snap.custom_inputs[i].resize(nodes_[i].input_port_count);
        }
    }

    resume();
    return true;
}

// Legacy single-call reload — only correct if old dylib is still loaded.
// Prefer the two-phase protocol (pre_reload + post_reload) around
// scheduler.reload_operator().
bool AudioEngine::reload_operator(const std::string& type_name, OperatorRegistry& registry) {
    pre_reload_operator(type_name);
    return post_reload_operator(type_name, registry);
}

void AudioEngine::shutdown() {
    if (device_) {
        if (running_) {
            ma_device_stop(device_);
            running_ = false;
        }
        ma_device_uninit(device_);
        delete device_;
        device_ = nullptr;
    }

    // Destroy extra auto-dup instances (skip [0] — that's the primary, destroyed below)
    for (auto& group : auto_dup_groups_) {
        auto& ns = nodes_[group.node_idx];
        for (uint8_t c = 1; c < group.channel_count; ++c) {
            if (group.instances[c]) {
                ns.loader->destroy_instance(group.instances[c]);
                group.instances[c] = nullptr;
            }
        }
    }
    auto_dup_groups_.clear();
    node_to_dup_group_.clear();

    for (auto& ns : nodes_) {
        if (ns.instance) {
            ns.loader->destroy_instance(ns.instance);
            ns.instance = nullptr;
        }
    }
    nodes_.clear();
    wires_.clear();
    audio_float_wires_.clear();
    audio_custom_wires_.clear();
    audio_spread_wires_.clear();
    cross_wires_.clear();
    cross_spread_wires_.clear();
    cross_string_wires_.clear();
    cross_custom_wires_.clear();
    cross_float_wires_.clear();

    std::fprintf(stderr, "[vivid] AudioEngine: shutdown\n");
}

void AudioEngine::ma_data_callback(ma_device* device_ptr, void* output, const void* /*input*/, ma_uint32 frame_count) {
    auto* engine = static_cast<AudioEngine*>(device_ptr->pUserData);
    engine->audio_callback(static_cast<float*>(output), frame_count);
}

void AudioEngine::audio_callback(float* output, uint32_t frame_count) {
    auto cb_start = std::chrono::steady_clock::now();

    // Read params from the active snapshot (lock-free)
    const auto& snap = snapshots_[active_.load(std::memory_order_acquire)];

    // Apply param snapshot to audio nodes
    for (size_t i = 0; i < nodes_.size(); ++i) {
        auto& ns = nodes_[i];
        if (i < snap.node_params.size()) {
            for (size_t p = 0; p < ns.param_values.size() && p < snap.node_params[i].size(); ++p) {
                ns.param_values[p] = snap.node_params[i][p];
            }
        }
        if (i < snap.float_input_values.size()) {
            for (size_t p = 0; p < ns.float_input_values.size() &&
                               p < snap.float_input_values[i].size(); ++p) {
                ns.float_input_values[p] = snap.float_input_values[i][p];
            }
        }
        // Apply spread inputs from param snapshot
        if (ns.has_spread_ports && i < snap.spread_inputs.size()) {
            for (size_t p = 0; p < ns.spread_inputs.size() && p < snap.spread_inputs[i].size(); ++p) {
                ns.spread_inputs[p] = snap.spread_inputs[i][p];
            }
        }
        if (ns.has_string_input_ports && i < snap.input_string_values.size()) {
            for (size_t p = 0; p < ns.input_string_values.size() &&
                               p < snap.input_string_values[i].size(); ++p) {
                ns.input_string_values[p] = snap.input_string_values[i][p];
            }
        }
        if (ns.has_custom_input_ports && i < snap.custom_inputs.size()) {
            for (size_t p = 0; p < ns.custom_input_values.size() &&
                               p < snap.custom_inputs[i].size(); ++p) {
                auto& in = snap.custom_inputs[i][p];
                if (!in.valid) {
                    ns.custom_input_values[p] = nullptr;
                } else {
                    // The audio operator reads a stable snapshot view of the
                    // custom payload bytes for this callback. For CUSTOM_REF
                    // ports, this is a small ref-token struct, not direct
                    // access to the underlying shared object.
                    ns.custom_input_values[p] = static_cast<void*>(
                        const_cast<uint8_t*>(in.bytes));
                }
            }
        } else {
            std::fill(ns.custom_input_values.begin(), ns.custom_input_values.end(), nullptr);
        }
    }

    // Process in chunks of kBufferSize
    uint32_t frames_written = 0;
    while (frames_written < frame_count) {
        uint32_t chunk = std::min(kBufferSize, frame_count - frames_written);

        // Process each audio node in topological order
        for (uint32_t ni = 0; ni < static_cast<uint32_t>(nodes_.size()); ++ni) {
            auto& ns = nodes_[ni];

            // Zero input buffers (full multi-channel extent)
            for (auto& buf : ns.input_buffers)
                std::memset(buf.data(), 0, buf.size() * sizeof(float));

            // Solo mode: skip non-active audio nodes (sink always runs)
            if (!snap.solo_active_set.empty() &&
                ni < static_cast<uint32_t>(snap.solo_active_set.size()) &&
                !snap.solo_active_set[ni] &&
                static_cast<int>(ni) != sink_node_idx_) {
                for (auto& buf : ns.output_buffers)
                    std::memset(buf.data(), 0, buf.size() * sizeof(float));
                continue;
            }

            // Copy upstream audio outputs into this node's inputs (multi-channel aware)
            for (const auto& w : wires_) {
                if (w.to_node_idx == ni) {
                    const float* src = nodes_[w.from_node_idx].output_buffers[w.from_port_idx].data();
                    float* dst = ns.input_buffers[w.to_port_idx].data();
                    uint8_t fc = w.from_channels;
                    uint8_t tc = w.to_channels;
                    float scale = w.scale;

                    if (fc == tc) {
                        // N→N: copy all channels
                        for (uint8_t c = 0; c < fc; ++c) {
                            const float* sc = src + c * kBufferSize;
                            float* dc = dst + c * kBufferSize;
                            for (uint32_t s = 0; s < chunk; ++s)
                                dc[s] += sc[s] * scale;
                        }
                    } else if (fc == 1 && tc > 1) {
                        // 1→N: upmix mono to all channels
                        for (uint8_t c = 0; c < tc; ++c) {
                            float* dc = dst + c * kBufferSize;
                            for (uint32_t s = 0; s < chunk; ++s)
                                dc[s] += src[s] * scale;
                        }
                    } else if (fc > 1 && tc == 1) {
                        // N→1: downmix — average all channels
                        float inv_n = 1.0f / static_cast<float>(fc);
                        for (uint32_t s = 0; s < chunk; ++s) {
                            float sum = 0.0f;
                            for (uint8_t c = 0; c < fc; ++c)
                                sum += src[c * kBufferSize + s];
                            dst[s] += sum * inv_n * scale;
                        }
                    } else {
                        // N→M: copy min(fc,tc) channels, zero-pad or truncate
                        uint8_t common = std::min(fc, tc);
                        for (uint8_t c = 0; c < common; ++c) {
                            const float* sc = src + c * kBufferSize;
                            float* dc = dst + c * kBufferSize;
                            for (uint32_t s = 0; s < chunk; ++s)
                                dc[s] += sc[s] * scale;
                        }
                    }
                }
            }

            // Build pointer arrays for VividAudioContext (pre-allocated)
            for (uint32_t p = 0; p < ns.input_port_count; ++p)
                ns.in_ptrs[p] = ns.input_buffers[p].data();
            for (uint32_t p = 0; p < ns.output_port_count; ++p)
                ns.out_ptrs[p] = ns.output_buffers[p].data();

            // Set up spread ports for nodes that have them
            if (ns.has_spread_ports) {
                for (uint32_t p = 0; p < ns.input_port_count; ++p) {
                    ns.spread_in_ports[p].length = ns.spread_inputs[p].length;
                }
                for (uint32_t p = 0; p < ns.output_port_count; ++p) {
                    ns.spread_out_ports[p].length = 0;
                }
            }

            if (ns.has_string_input_ports) {
                for (uint32_t p = 0; p < ns.input_port_count; ++p) {
                    ns.c_input_string_values[p] = ns.input_string_values[p].c_str();
                }
            }

            double time = static_cast<double>(audio_frame_ + frames_written) / kSampleRate;

            // Reset float output values to a sentinel so we can distinguish
            // scalar-written SIGNAL outputs from buffer-backed SIGNAL outputs
            // that still need last-sample extraction after process_audio().
            std::fill(ns.float_output_values.begin(), ns.float_output_values.end(),
                      unset_signal_output_sentinel());
            std::fill(ns.custom_output_ptrs.begin(), ns.custom_output_ptrs.end(), nullptr);

            // Check if this is an auto-dup node
            auto dup_it = node_to_dup_group_.find(ni);
            if (dup_it != node_to_dup_group_.end()) {
                // ---- Auto-duplication processing (Phase 4) ----
                auto& group = auto_dup_groups_[dup_it->second];
                uint8_t ch = group.channel_count;

                // Deinterleave: copy channel c from multi-channel input → per_ch_inputs[c]
                for (uint8_t c = 0; c < ch; ++c) {
                    for (uint32_t p = 0; p < ns.input_port_count; ++p) {
                        if (ns.input_port_types[p] == VIVID_PORT_AUDIO) {
                            const float* mc = ns.input_buffers[p].data() + c * kBufferSize;
                            std::memcpy(group.per_ch_inputs[c][p].data(), mc, chunk * sizeof(float));
                        } else {
                            // Non-audio ports: copy same data to all channels
                            std::memcpy(group.per_ch_inputs[c][p].data(),
                                        ns.input_buffers[p].data(), chunk * sizeof(float));
                        }
                    }
                }

                // Process each channel instance
                for (uint8_t c = 0; c < ch; ++c) {
                    VividAudioContext audio_ctx{};
                    audio_ctx.time = time;
                    audio_ctx.delta_time = static_cast<double>(chunk) / kSampleRate;
                    audio_ctx.frame = audio_frame_ + frames_written;
                    audio_ctx.param_values = ns.param_values.data();
                    audio_ctx.input_buffers = group.per_ch_in_ptrs[c].data();
                    audio_ctx.output_buffers = group.per_ch_out_ptrs[c].data();
                    audio_ctx.buffer_size = chunk;
                    audio_ctx.sample_rate = kSampleRate;
                    audio_ctx.input_channel_counts = nullptr;  // mono view
                    audio_ctx.output_channel_counts = nullptr;
                    audio_ctx.input_spreads = ns.has_spread_ports ? ns.spread_in_ports.data() : nullptr;
                    audio_ctx.output_spreads = ns.has_spread_ports ? ns.spread_out_ports.data() : nullptr;
                    audio_ctx.custom_inputs = ns.has_custom_input_ports ? ns.custom_input_values.data() : nullptr;
                    audio_ctx.custom_input_count = static_cast<uint32_t>(ns.custom_input_values.size());
                    audio_ctx.input_string_values = ns.has_string_input_ports ? ns.c_input_string_values.data() : nullptr;
                    audio_ctx.input_float_values = ns.float_input_values.empty()
                        ? ns.float_input_scratch_ : ns.float_input_values.data();
                    audio_ctx.output_float_values = ns.float_output_values.empty()
                        ? ns.float_output_scratch_ : ns.float_output_values.data();
                    audio_ctx.custom_outputs = ns.custom_output_ptrs.empty() ? nullptr : ns.custom_output_ptrs.data();
                    audio_ctx.custom_output_count = ns.custom_output_count;
                    audio_ctx.file_param_values = nullptr;
                    audio_ctx.file_param_count = 0;
                    audio_ctx.shared_handles = vivid::shared_handle_service();
                    // Role binding configs (same for all channel duplicates)
                    if (ni < static_cast<uint32_t>(snap.role_bindings.size())) {
                        audio_ctx.role_binding_count  = snap.role_bindings[ni].count;
                        audio_ctx.role_binding_configs = snap.role_bindings[ni].configs;
                    } else {
                        audio_ctx.role_binding_count  = 0;
                        audio_ctx.role_binding_configs = nullptr;
                    }

                    if (!ns.errored) {
                        try {
                            CrashGuard guard(ns.node_id.c_str());
                            ns.loader->process_audio(group.instances[c], &audio_ctx);
                        } catch (const std::exception& e) {
                            ns.errored = true;
                            std::snprintf(ns.error_message, sizeof(ns.error_message), "%s", e.what());
                        } catch (...) {
                            ns.errored = true;
                            std::snprintf(ns.error_message, sizeof(ns.error_message), "Unknown exception");
                        }
                    }
                }

                if (ns.errored) {
                    for (auto& buf : ns.output_buffers)
                        std::memset(buf.data(), 0, buf.size() * sizeof(float));
                } else {
                    // Interleave: copy per_ch_outputs[c] → channel c of multi-channel output
                    for (uint8_t c = 0; c < ch; ++c) {
                        for (uint32_t p = 0; p < ns.output_port_count; ++p) {
                            if (ns.output_port_types[p] == VIVID_PORT_AUDIO) {
                                float* mc = ns.output_buffers[p].data() + c * kBufferSize;
                                std::memcpy(mc, group.per_ch_outputs[c][p].data(), chunk * sizeof(float));
                            }
                        }
                    }
                }
            } else {
                // ---- Normal (non-dup) processing ----
                VividAudioContext audio_ctx{};
                audio_ctx.time = time;
                audio_ctx.delta_time = static_cast<double>(chunk) / kSampleRate;
                audio_ctx.frame = audio_frame_ + frames_written;
                audio_ctx.param_values = ns.param_values.data();
                audio_ctx.input_buffers = ns.in_ptrs.data();
                audio_ctx.output_buffers = ns.out_ptrs.data();
                audio_ctx.buffer_size = chunk;
                audio_ctx.sample_rate = kSampleRate;
                audio_ctx.input_channel_counts = ns.input_channel_counts.data();
                audio_ctx.output_channel_counts = ns.output_channel_counts.data();
                audio_ctx.input_spreads = ns.has_spread_ports ? ns.spread_in_ports.data() : nullptr;
                audio_ctx.output_spreads = ns.has_spread_ports ? ns.spread_out_ports.data() : nullptr;
                audio_ctx.custom_inputs = ns.has_custom_input_ports ? ns.custom_input_values.data() : nullptr;
                audio_ctx.custom_input_count = static_cast<uint32_t>(ns.custom_input_values.size());
                audio_ctx.input_string_values = ns.has_string_input_ports ? ns.c_input_string_values.data() : nullptr;
                audio_ctx.input_float_values = ns.float_input_values.empty()
                    ? ns.float_input_scratch_ : ns.float_input_values.data();
                audio_ctx.output_float_values = ns.float_output_values.empty()
                    ? ns.float_output_scratch_ : ns.float_output_values.data();
                audio_ctx.custom_outputs = ns.custom_output_ptrs.empty() ? nullptr : ns.custom_output_ptrs.data();
                audio_ctx.custom_output_count = ns.custom_output_count;
                audio_ctx.file_param_values = nullptr;
                audio_ctx.file_param_count = 0;
                audio_ctx.shared_handles = vivid::shared_handle_service();
                // Role binding configs
                if (ni < static_cast<uint32_t>(snap.role_bindings.size())) {
                    audio_ctx.role_binding_count  = snap.role_bindings[ni].count;
                    audio_ctx.role_binding_configs = snap.role_bindings[ni].configs;
                } else {
                    audio_ctx.role_binding_count  = 0;
                    audio_ctx.role_binding_configs = nullptr;
                }

                if (!ns.errored) {
                    try {
                        CrashGuard guard(ns.node_id.c_str());
                        ns.loader->process_audio(ns.instance, &audio_ctx);
                    } catch (const std::exception& e) {
                        ns.errored = true;
                        std::snprintf(ns.error_message, sizeof(ns.error_message), "%s", e.what());
                        for (auto& buf : ns.output_buffers)
                            std::memset(buf.data(), 0, chunk * sizeof(float));
                    } catch (...) {
                        ns.errored = true;
                        std::snprintf(ns.error_message, sizeof(ns.error_message), "Unknown exception");
                        for (auto& buf : ns.output_buffers)
                            std::memset(buf.data(), 0, chunk * sizeof(float));
                    }
                } else {
                    // Errored nodes produce silence
                    for (auto& buf : ns.output_buffers)
                        std::memset(buf.data(), 0, chunk * sizeof(float));
                }
            }

            // Read back spread output lengths
            if (ns.has_spread_ports) {
                for (uint32_t p = 0; p < ns.output_port_count; ++p) {
                    ns.spread_outputs[p].length = ns.spread_out_ports[p].length;
                    // Data is already in spread_outputs[p].data via pointer alias
                }
            }

            // Auto-extract last sample from buffer-backed SIGNAL outputs only when the
            // operator left the scalar slot untouched. Scalar-writing audio operators
            // such as Clock intentionally write ctx->output_float_values directly.
            for (const auto& se : ns.signal_output_extractions) {
                if (se.float_ordinal >= ns.float_output_values.size())
                    continue;
                if (std::isnan(ns.float_output_values[se.float_ordinal]) &&
                    se.port_idx < ns.output_buffers.size() && chunk > 0) {
                    ns.float_output_values[se.float_ordinal] =
                        ns.output_buffers[se.port_idx][chunk - 1];
                }
            }
            for (auto& out : ns.float_output_values) {
                if (std::isnan(out))
                    out = 0.0f;
            }

            // Route spread outputs to downstream audio nodes via audio spread wires
            for (const auto& sw : audio_spread_wires_) {
                if (sw.from_node_idx == ni) {
                    const auto& src = ns.spread_outputs[sw.from_port_idx];
                    auto& dst = nodes_[sw.to_node_idx].spread_inputs[sw.to_port_idx];
                    dst.length = src.length;
                    if (src.length > 0)
                        std::memcpy(dst.data, src.data, src.length * sizeof(float));
                }
            }

            // Route float outputs to downstream audio nodes via audio float port wires
            for (const auto& fw : audio_float_wires_) {
                if (fw.from_node_idx == ni) {
                    float val = ns.float_output_values[fw.from_float_port_idx] * fw.scale;
                    auto& to_ns = nodes_[fw.to_node_idx];
                    if (fw.to_float_port_idx < to_ns.float_input_values.size())
                        to_ns.float_input_values[fw.to_float_port_idx] = val;
                }
            }

            // Route custom outputs to downstream audio nodes via audio custom wires
            for (const auto& cw : audio_custom_wires_) {
                if (cw.from_node_idx == ni) {
                    auto& to_ns = nodes_[cw.to_node_idx];
                    if (cw.from_port_idx < ns.custom_output_ptrs.size() &&
                        cw.to_port_idx < to_ns.custom_input_values.size()) {
                        to_ns.custom_input_values[cw.to_port_idx] = ns.custom_output_ptrs[cw.from_port_idx];
                    }
                }
            }
        }

        // Copy sink node's audio to interleaved stereo device buffer
        float* dst = output + frames_written * 2;
        if (sink_node_idx_ >= 0) {
            auto& sink = nodes_[sink_node_idx_];
            if (sink.output_port_count > 0) {
                // Traditional sink (last node with outputs) — check channel count
                uint8_t ch = sink.output_channel_counts.empty() ? 1 : sink.output_channel_counts[0];
                // Auto-dup resets channel counts to 1, but buffer retains multi-channel layout
                if (sink.is_mono_autodup)
                    ch = static_cast<uint8_t>(sink.output_buffers[0].size() / kBufferSize);
                const float* buf = sink.output_buffers[0].data();
                if (ch >= 2) {
                    for (uint32_t s = 0; s < chunk; ++s) {
                        dst[s * 2]     = buf[s];
                        dst[s * 2 + 1] = buf[kBufferSize + s];
                    }
                } else {
                    for (uint32_t s = 0; s < chunk; ++s) {
                        dst[s * 2]     = buf[s];
                        dst[s * 2 + 1] = buf[s];
                    }
                }
            } else if (sink.input_port_count > 0) {
                // audio_out sink node — read from input port 0
                uint8_t ch = sink.input_channel_counts.empty() ? 1 : sink.input_channel_counts[0];
                // Auto-dup resets channel counts to 1, but buffer retains multi-channel layout
                if (sink.is_mono_autodup)
                    ch = static_cast<uint8_t>(sink.input_buffers[0].size() / kBufferSize);
                const float* buf = sink.input_buffers[0].data();
                if (ch >= 2) {
                    // Planar stereo: L at [0..255], R at [256..511]
                    for (uint32_t s = 0; s < chunk; ++s) {
                        dst[s * 2]     = buf[s];
                        dst[s * 2 + 1] = buf[kBufferSize + s];
                    }
                } else {
                    // Mono → duplicate to both device channels
                    for (uint32_t s = 0; s < chunk; ++s) {
                        dst[s * 2]     = buf[s];
                        dst[s * 2 + 1] = buf[s];
                    }
                }
            } else {
                std::memset(dst, 0, chunk * 2 * sizeof(float));
            }
        } else {
            std::memset(dst, 0, chunk * 2 * sizeof(float));
        }

        // Recording tap: copy interleaved stereo output to ring buffer
        if (recording_tap_.active.load(std::memory_order_relaxed)) {
            uint64_t wp = recording_tap_.write_pos.load(std::memory_order_relaxed);
            uint64_t rp = recording_tap_.read_pos.load(std::memory_order_acquire);
            uint64_t available = RecordingTap::kRingSize - (wp - rp);
            uint64_t samples_to_copy = chunk * 2; // stereo interleaved
            if (samples_to_copy > available) {
                if (recording_overrun_count_ == 0)
                    std::fprintf(stderr, "[vivid] Recording tap overrun — samples dropped\n");
                recording_overrun_count_++;
                samples_to_copy = available;
            }
            for (uint64_t i = 0; i < samples_to_copy; ++i) {
                recording_tap_.ring[(wp + i) % RecordingTap::kRingSize] = dst[i];
            }
            recording_tap_.write_pos.store(wp + samples_to_copy, std::memory_order_release);
        }

        frames_written += chunk;
    }

    // Underrun detection: check if processing exceeded the buffer budget
    auto cb_end = std::chrono::steady_clock::now();
    double budget_us = static_cast<double>(frame_count) / kSampleRate * 1e6;
    double elapsed_us = std::chrono::duration<double, std::micro>(cb_end - cb_start).count();
    audio_load_.store(static_cast<float>(elapsed_us / budget_us), std::memory_order_relaxed);
    if (elapsed_us > budget_us) {
        underrun_count_.fetch_add(1, std::memory_order_relaxed);
        last_buffer_underrun_.store(true, std::memory_order_relaxed);
        // Zero output to avoid glitchy audio
        std::memset(output, 0, frame_count * 2 * sizeof(float));
    } else {
        last_buffer_underrun_.store(false, std::memory_order_relaxed);
    }

    // Compute RMS, peak, and waveform for each audio node, write to analysis snapshot
    int write_idx = 1 - analysis_active_.load(std::memory_order_acquire);
    auto& analysis = analysis_snapshots_[write_idx];
    for (uint32_t ni = 0; ni < static_cast<uint32_t>(nodes_.size()); ++ni) {
        // For the sink node (audio_out), analyze from input buffers since it has no outputs.
        const bool is_sink = (static_cast<int>(ni) == sink_node_idx_);
        if (nodes_[ni].output_port_count > 0 || (is_sink && nodes_[ni].input_port_count > 0)) {
            const float* buf = is_sink ? nodes_[ni].input_buffers[0].data()
                                       : nodes_[ni].output_buffers[0].data();
            float sum_sq = 0.0f, pk = 0.0f;
            for (uint32_t s = 0; s < frame_count; ++s) {
                sum_sq += buf[s] * buf[s];
                float a = buf[s] < 0 ? -buf[s] : buf[s];
                if (a > pk) pk = a;
            }
            analysis.rms[ni] = std::sqrt(sum_sq / frame_count);
            analysis.peak[ni] = pk;

            // Write raw output samples into ring buffer
            auto& ring = waveform_rings_[ni];
            uint32_t& pos = waveform_ring_pos_[ni];
            for (uint32_t s = 0; s < frame_count; ++s) {
                ring[pos] = buf[s];
                pos = (pos + 1) % 1024;
            }

            // Linearize ring buffer into analysis waveform
            constexpr uint32_t kWaveN = AnalysisSnapshot::kWaveformSamples;
            auto& wave = analysis.waveform[ni];
            for (uint32_t w = 0; w < kWaveN; ++w) {
                wave[w] = ring[(pos + w) % 1024];
            }
        }

        // Copy spread outputs to analysis snapshot
        if (nodes_[ni].has_spread_ports) {
            for (uint32_t p = 0; p < nodes_[ni].output_port_count; ++p) {
                analysis.spread_outputs[ni][p] = nodes_[ni].spread_outputs[p];
            }
        }

        // Copy float outputs to analysis snapshot
        if (nodes_[ni].float_output_count > 0) {
            auto& fo = analysis.float_outputs[ni];
            for (uint32_t p = 0; p < nodes_[ni].float_output_count && p < fo.size(); ++p) {
                fo[p] = nodes_[ni].float_output_values[p];
            }
        }

        // Propagate error state to analysis snapshot (no heap allocation)
        bool err = nodes_[ni].errored;
        analysis.errored[ni] = err;
        if (err) {
            std::memcpy(analysis.error_msgs[ni].data(), nodes_[ni].error_message, 256);
        } else {
            analysis.error_msgs[ni][0] = '\0';
        }
    }
    analysis_active_.store(write_idx, std::memory_order_release);

    audio_frame_ += frame_count;
}

// ---------------------------------------------------------------------------
// Recording tap — main thread API
// ---------------------------------------------------------------------------

void AudioEngine::start_recording_tap() {
    recording_tap_.read_pos.store(0, std::memory_order_relaxed);
    recording_tap_.write_pos.store(0, std::memory_order_relaxed);
    recording_tap_.active.store(true, std::memory_order_release);
}

void AudioEngine::stop_recording_tap() {
    recording_tap_.active.store(false, std::memory_order_release);
}

uint64_t AudioEngine::available_recorded_samples() const {
    uint64_t wp = recording_tap_.write_pos.load(std::memory_order_acquire);
    uint64_t rp = recording_tap_.read_pos.load(std::memory_order_relaxed);
    return wp - rp;
}

uint64_t AudioEngine::pop_recorded_samples(float* dst, uint64_t max_samples) {
    uint64_t wp = recording_tap_.write_pos.load(std::memory_order_acquire);
    uint64_t rp = recording_tap_.read_pos.load(std::memory_order_relaxed);
    uint64_t avail = wp - rp;
    uint64_t to_read = avail < max_samples ? avail : max_samples;
    for (uint64_t i = 0; i < to_read; ++i) {
        dst[i] = recording_tap_.ring[(rp + i) % RecordingTap::kRingSize];
    }
    recording_tap_.read_pos.store(rp + to_read, std::memory_order_release);
    return to_read;
}

float AudioEngine::float_input_value_for_test(int node_idx, int port_idx) const {
    if (node_idx < 0 || node_idx >= static_cast<int>(nodes_.size())) return 0.0f;
    const auto& fv = nodes_[node_idx].float_input_values;
    if (port_idx < 0 || port_idx >= static_cast<int>(fv.size())) return 0.0f;
    return fv[port_idx];
}

void AudioEngine::process_audio_for_test(float* output, uint32_t frame_count) {
    audio_callback(output, frame_count);
}

} // namespace vivid
