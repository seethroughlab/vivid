#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "runtime/audio_engine.h"
#include "runtime/audio_executor.h"
#include "runtime/cadence_bridge.h"
#include "runtime/compiled_graph.h"
#include "runtime/crash_guard.h"
#include "operator_api/port_type_registry.h"
#include "operator_api/type_id.h"
#include "runtime/shared_handle_registry.h"
#include "runtime/scheduler.h"
#include "common/topo_sort.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vivid {

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

    // Store references to the cadence-aware runtime (adapter layer)
    compiled_graph_ = const_cast<CompiledGraph*>(scheduler.compiled_graph());
    cadence_bridge_ = &const_cast<Scheduler&>(scheduler).cadence_bridge();

    // Share AudioEngine's operator instances with CompiledGraph audio nodes.
    // GraphCompiler created its own instances, but AudioEngine's are the ones
    // that have been properly initialized (main_thread_update, file params, etc.).
    if (compiled_graph_) {
        for (size_t i = 0; i < nodes_.size(); ++i) {
            auto* cn = compiled_graph_->find_node(nodes_[i].node_id);
            if (!cn || cn->active_cadence != Cadence::Audio) continue;
            // Destroy GraphCompiler's instance (it's redundant)
            if (cn->instance && cn->loader)
                cn->loader->destroy_instance(cn->instance);
            // Share AudioEngine's instance
            cn->instance = nodes_[i].instance;
            cn->loader = nodes_[i].loader;
            // Sync param values
            cn->param_values = nodes_[i].param_values;
        }
    }

    // Build AudioExecutor and activate the new audio path
    if (compiled_graph_ && cadence_bridge_) {
        audio_executor_ = std::make_unique<AudioExecutor>();
        if (audio_executor_->build(*compiled_graph_)) {
            // Start with null device — AudioEngine owns the real miniaudio device.
            // This sets bridge_ and graph_ pointers so process_audio_for_test() works.
            audio_executor_->start(*cadence_bridge_, *compiled_graph_, /*use_null_device=*/true);
            use_new_audio_path_ = true;
        } else {
            audio_executor_.reset();
        }
    }

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
    // Delegate to CadenceBridge (AudioExecutor drives the audio callback)
    if (cadence_bridge_ && compiled_graph_ && use_new_audio_path_) {
        // Sync frame-rate node outputs from NodeState → CompiledNode
        // (external code may have modified output_values directly, e.g. tests)
        for (const auto& ns : scheduler.nodes()) {
            auto* cn = compiled_graph_->find_node(ns.node_id);
            if (!cn || cn->active_cadence == Cadence::Audio) continue;
            cn->output_values = ns.output_values;
            cn->output_spreads = ns.output_spreads;
            cn->param_values = ns.param_values;
        }
        cadence_bridge_->push_to_audio(*compiled_graph_);
        return;
    }

    // AudioExecutor should always be active after build().
    std::fprintf(stderr, "[vivid] AudioEngine::push_params() skipped: new audio path not active\n");
}

void AudioEngine::update_sources(double time, const Scheduler& scheduler) {
    if (cadence_bridge_ && compiled_graph_ && use_new_audio_path_) {
        cadence_bridge_->update_sources(time, *compiled_graph_);
        return;
    }

    // AudioExecutor should always be active after build().
    std::fprintf(stderr, "[vivid] AudioEngine::update_sources() skipped: new audio path not active\n");
}

void AudioEngine::inject_analysis(Scheduler& scheduler) {
    // Delegate to CadenceBridge when AudioExecutor is driving the callback
    if (cadence_bridge_ && compiled_graph_ && use_new_audio_path_) {
        cadence_bridge_->pull_from_audio(*compiled_graph_);
        // Also sync analysis results to NodeState for inspector/UI
        for (const auto& pm : param_mappings_) {
            auto* cn = compiled_graph_->find_node(nodes_[pm.audio_engine_idx].node_id);
            if (!cn) continue;
            auto& ns = scheduler.nodes_mut()[pm.scheduler_node_idx];
            ns.errored = cn->errored;
            ns.error_message = cn->error_message;
            // Float outputs
            for (const auto& fm : pm.float_output_mappings) {
                if (fm.audio_float_ordinal < cn->float_output_values.size() &&
                    fm.scheduler_port_idx < ns.output_values.size()) {
                    float val = cn->float_output_values[fm.audio_float_ordinal];
                    scheduler.inject_external_output(pm.scheduler_node_idx,
                                                     fm.scheduler_port_idx, val);
                }
            }
            // Spread outputs
            for (const auto& sm : pm.spread_output_mappings) {
                if (sm.audio_port_idx < cn->output_spreads.size() &&
                    sm.scheduler_port_idx < ns.output_spreads.size()) {
                    const auto& src = cn->output_spreads[sm.audio_port_idx];
                    ns.output_spreads[sm.scheduler_port_idx] = src;
                    scheduler.inject_external_spread(pm.scheduler_node_idx,
                                                     sm.scheduler_port_idx,
                                                     src.data(),
                                                     static_cast<uint32_t>(src.size()));
                }
            }
        }
        // Analysis ports (rms/peak/waveform)
        for (const auto& am : analysis_mappings_) {
            auto* cn = compiled_graph_->find_node(nodes_[am.audio_engine_idx].node_id);
            if (!cn) continue;
            auto rms_it = cn->analysis_output_port_indices.find("rms");
            auto peak_it = cn->analysis_output_port_indices.find("peak");
            if (rms_it != cn->analysis_output_port_indices.end() &&
                rms_it->second < cn->output_values.size())
                scheduler.inject_external_output(am.scheduler_node_idx,
                                                 am.rms_port_idx,
                                                 cn->output_values[rms_it->second]);
            if (peak_it != cn->analysis_output_port_indices.end() &&
                peak_it->second < cn->output_values.size())
                scheduler.inject_external_output(am.scheduler_node_idx,
                                                 am.peak_port_idx,
                                                 cn->output_values[peak_it->second]);
        }
        return;
    }

    // AudioExecutor should always be active after build().
    std::fprintf(stderr, "[vivid] AudioEngine::inject_analysis() skipped: new audio path not active\n");
}

const AnalysisSnapshot& AudioEngine::analysis_read() const {
    if (use_new_audio_path_ && cadence_bridge_) {
        return cadence_bridge_->active_analysis();
    }
    // Fallback: return empty analysis (new audio path should always be active)
    return analysis_snapshots_[analysis_active_.load(std::memory_order_acquire)];
}

int AudioEngine::audio_node_index(const std::string& node_id) const {
    if (use_new_audio_path_ && compiled_graph_) {
        // Return the audio_order index (position in CadenceBridge snapshot arrays)
        for (uint32_t i = 0; i < static_cast<uint32_t>(compiled_graph_->audio_order.size()); ++i) {
            if (compiled_graph_->nodes[compiled_graph_->audio_order[i]].node_id == node_id)
                return static_cast<int>(i);
        }
        return -1;
    }
    // Fallback for legacy path (should not be reached after build())
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
            // Also null out the shared CompiledNode instance to prevent dangling pointer
            if (compiled_graph_) {
                auto* cn = compiled_graph_->find_node(ns.node_id);
                if (cn) cn->instance = nullptr;
            }
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

        // Share new instance with CompiledGraph
        if (compiled_graph_) {
            auto* cn = compiled_graph_->find_node(ns.node_id);
            if (cn && cn->active_cadence == Cadence::Audio) {
                cn->instance = ns.instance;
                cn->loader = ns.loader;
                cn->param_values = ns.param_values;
            }
        }
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

    // Shutdown AudioExecutor adapter and clear shared instance pointers
    // to prevent double-free (AudioEngine owns the instances, CompiledGraph borrowed them)
    if (compiled_graph_) {
        for (auto& ns : nodes_) {
            auto* cn = compiled_graph_->find_node(ns.node_id);
            if (cn && cn->active_cadence == Cadence::Audio)
                cn->instance = nullptr;  // AudioEngine will destroy it below
        }
    }
    if (audio_executor_) {
        audio_executor_->shutdown();
        audio_executor_.reset();
    }
    use_new_audio_path_ = false;
    compiled_graph_ = nullptr;
    cadence_bridge_ = nullptr;

    std::fprintf(stderr, "[vivid] AudioEngine: shutdown\n");
}

void AudioEngine::ma_data_callback(ma_device* device_ptr, void* output, const void* /*input*/, ma_uint32 frame_count) {
    auto* engine = static_cast<AudioEngine*>(device_ptr->pUserData);
    engine->audio_callback(static_cast<float*>(output), frame_count);
}

void AudioEngine::audio_callback(float* output, uint32_t frame_count) {
    // Delegate to AudioExecutor (always active after build())
    if (use_new_audio_path_ && audio_executor_) {
        audio_executor_->process_audio_for_test(output, frame_count);
        return;
    }

    // Fallback: silence (AudioExecutor should always be active)
    std::memset(output, 0, frame_count * 2 * sizeof(float));
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
    // When new audio path is active, read from CompiledGraph audio nodes
    if (use_new_audio_path_ && compiled_graph_ && node_idx >= 0 &&
        node_idx < static_cast<int>(nodes_.size())) {
        auto* cn = compiled_graph_->find_node(nodes_[node_idx].node_id);
        if (cn) {
            const auto& fv = cn->float_input_values;
            if (port_idx >= 0 && port_idx < static_cast<int>(fv.size()))
                return fv[port_idx];
        }
    }
    if (node_idx < 0 || node_idx >= static_cast<int>(nodes_.size())) return 0.0f;
    const auto& fv = nodes_[node_idx].float_input_values;
    if (port_idx < 0 || port_idx >= static_cast<int>(fv.size())) return 0.0f;
    return fv[port_idx];
}

void AudioEngine::process_audio_for_test(float* output, uint32_t frame_count) {
    audio_callback(output, frame_count);
}

} // namespace vivid
