#include "runtime/scheduler.h"
#include "common/gpu_util.h"
#include "common/topo_sort.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/type_id.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vivid {

static constexpr uint32_t kMaxSpreadCapacity = 1024;

// Check if a wire has non-default remap (any field differs from identity mapping)
inline bool has_remap(const Wire& w) {
    return w.from_min != 0.0f || w.from_max != 1.0f ||
           w.to_min  != 0.0f || w.to_max  != 1.0f || w.clamp;
}

// Apply remap: maps val from [from_min, from_max] to [to_min, to_max]
inline float apply_remap(float val, const Wire& w) {
    float range = w.from_max - w.from_min;
    float t;
    if (range != 0.0f) {
        t = (val - w.from_min) / range;
    } else {
        t = 0.5f;
        std::fprintf(stderr, "[vivid] Scheduler: wire remap has zero input range "
                     "(from_min == from_max == %g) — using midpoint\n", w.from_min);
    }
    float out = w.to_min + t * (w.to_max - w.to_min);
    if (w.clamp) {
        float lo = std::min(w.to_min, w.to_max);
        float hi = std::max(w.to_min, w.to_max);
        out = std::max(lo, std::min(hi, out));
    }
    return out;
}

void Scheduler::init_node_state(NodeState& ns, const VividOperatorDescriptor* desc,
                                const std::unordered_map<std::string, float>* param_overrides,
                                const std::unordered_map<std::string, std::string>* string_overrides) {
    // Count and index ports by direction
    ns.input_port_count = 0;
    ns.output_port_count = 0;
    ns.input_port_indices.clear();
    ns.output_port_indices.clear();
    ns.param_indices.clear();
    ns.input_port_types.clear();
    ns.output_port_types.clear();

    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            ns.input_port_indices[desc->ports[i].name] = ns.input_port_count++;
            ns.input_port_types.push_back(desc->ports[i].type);
        } else {
            ns.output_port_indices[desc->ports[i].name] = ns.output_port_count++;
            ns.output_port_types.push_back(desc->ports[i].type);
        }
    }

    ns.input_values.assign(ns.input_port_count, 0.0f);
    ns.output_values.assign(ns.output_port_count, 0.0f);
    ns.input_string_values.assign(ns.input_port_count, "");
    ns.output_string_values.assign(ns.output_port_count, "");
    ns.c_input_string_values.assign(ns.input_port_count, nullptr);
    ns.c_output_string_values.assign(ns.output_port_count, nullptr);
    ns.input_spreads.resize(ns.input_port_count);
    ns.output_spreads.resize(ns.output_port_count);
    ns.input_string_spreads.resize(ns.input_port_count);
    ns.output_string_spreads.resize(ns.output_port_count);

    // Init param_values from descriptor defaults, then apply overrides
    ns.param_values.resize(desc->param_count);
    ns.param_lock_flags.assign(desc->param_count, PARAM_LOCK_NONE);
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

    // Domain flags
    ns.time_dependent = desc->time_dependent != 0;
    ns.is_gpu = (desc->has_process_gpu || desc->domain == 2u /*GPU*/);
    ns.is_audio = (desc->has_process_audio || desc->domain == 1u /*AUDIO*/);
    ns.prev_output_values.assign(ns.output_port_count, 0.0f);

    // Implicit analysis ports for audio-domain nodes (kept separate from signal outputs)
    if (ns.is_audio) {
        ns.analysis_output_port_indices["rms"]      = ns.output_port_count++;
        ns.analysis_output_port_indices["peak"]     = ns.output_port_count++;
        ns.analysis_output_port_indices["waveform"] = ns.output_port_count++;
        ns.output_values.resize(ns.output_port_count, 0.0f);
        ns.prev_output_values.resize(ns.output_port_count, 0.0f);
        ns.output_spreads.resize(ns.output_port_count);
    }

    // Pre-allocate spread port arrays (avoids per-frame heap allocations in tick)
    ns.c_in_spreads.resize(ns.input_port_count);
    ns.c_out_spreads.resize(ns.output_port_count);
    ns.out_spread_buf.resize(ns.output_port_count);
    ns.c_in_string_spreads.resize(ns.input_port_count);
    ns.c_out_string_spreads.resize(ns.output_port_count);
    ns.in_string_spread_ptrs.resize(ns.input_port_count);
    ns.out_string_spread_ptr_buf.resize(ns.output_port_count);
    for (uint32_t p = 0; p < ns.output_port_count; ++p) {
        ns.out_spread_buf[p].resize(kMaxSpreadCapacity, 0.0f);
        ns.out_string_spread_ptr_buf[p].resize(kMaxSpreadCapacity, nullptr);
    }
    for (uint32_t p = 0; p < ns.input_port_count; ++p) {
        ns.in_string_spread_ptrs[p].resize(kMaxSpreadCapacity, nullptr);
    }

    // Init file params from descriptor
    ns.file_param_storage.clear();
    ns.file_param_ptrs.clear();
    ns.file_param_indices.clear();
    ns.file_param_is_path.clear();
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        if (desc->params[i].type == VIVID_PARAM_FILE ||
            desc->params[i].type == VIVID_PARAM_TEXT) {
            uint32_t fidx = static_cast<uint32_t>(ns.file_param_storage.size());
            ns.file_param_indices[desc->params[i].name] = fidx;
            const char* def = desc->params[i].default_string;
            ns.file_param_storage.push_back(def ? def : "");
            ns.file_param_is_path.push_back(desc->params[i].type == VIVID_PARAM_FILE ? 1 : 0);
        }
    }
    if (string_overrides) {
        for (const auto& [pname, pval] : *string_overrides) {
            auto fi = ns.file_param_indices.find(pname);
            if (fi != ns.file_param_indices.end()) {
                ns.file_param_storage[fi->second] = pval;
            }
        }
    }
    // Resolve relative file paths against the graph's directory
    if (!graph_base_dir_.empty()) {
        for (size_t i = 0; i < ns.file_param_storage.size(); ++i) {
            if (!ns.file_param_is_path.empty() && !ns.file_param_is_path[i]) continue;
            auto& val = ns.file_param_storage[i];
            if (!val.empty() && std::filesystem::path(val).is_relative()) {
                auto resolved = graph_base_dir_ / val;
                if (std::filesystem::exists(resolved))
                    val = std::filesystem::canonical(resolved).string();
                else
                    val = resolved.lexically_normal().string();
            }
        }
    }
    ns.file_param_ptrs.resize(ns.file_param_storage.size());
    for (size_t i = 0; i < ns.file_param_storage.size(); ++i) {
        ns.file_param_ptrs[i] = ns.file_param_storage[i].c_str();
    }

    // Identify special input ports and sink/output capabilities
    ns.texture_input_port_indices.clear();
    ns.custom_input_port_indices.clear();
    ns.custom_output_port_indices.clear();
    ns.string_input_port_indices.clear();
    ns.string_spread_input_port_indices.clear();
    ns.is_gpu_sink = false;
    ns.has_texture_output = false;
    ns.has_string_output = false;
    ns.has_string_spread_output = false;
    ns.aux_texture_output_port_indices.clear();
    ns.aux_gpu_textures.clear();
    ns.aux_gpu_texture_views.clear();
    uint32_t input_idx = 0;
    uint32_t out_idx = 0;
    uint32_t gpu_tex_out_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            switch (desc->ports[i].type) {
                case VIVID_PORT_TEXTURE:
                    ns.texture_input_port_indices.push_back(input_idx);
                    break;
                case VIVID_PORT_STRING:
                    ns.string_input_port_indices.push_back(input_idx);
                    break;
                case VIVID_PORT_STRING_SPREAD:
                    ns.string_spread_input_port_indices.push_back(input_idx);
                    break;
                default:
                    if (vivid_is_custom_port_type(desc->ports[i].type))
                        ns.custom_input_port_indices.push_back(input_idx);
                    break;
            }
            input_idx++;
        } else {
            switch (desc->ports[i].type) {
                case VIVID_PORT_TEXTURE:
                    ns.has_texture_output = true;
                    if (gpu_tex_out_count > 0) {  // 2nd+ texture output → aux
                        ns.aux_texture_output_port_indices.push_back(static_cast<int32_t>(out_idx));
                        ns.aux_gpu_textures.push_back(nullptr);
                        ns.aux_gpu_texture_views.push_back(nullptr);
                    }
                    ++gpu_tex_out_count;
                    break;
                case VIVID_PORT_STRING:
                    ns.has_string_output = true;
                    break;
                case VIVID_PORT_STRING_SPREAD:
                    ns.has_string_spread_output = true;
                    break;
                default:
                    if (vivid_is_custom_port_type(desc->ports[i].type))
                        ns.custom_output_port_indices.push_back(out_idx);
                    break;
            }
            out_idx++;
        }
    }
    if (ns.is_gpu) {
        ns.is_gpu_sink = !ns.texture_input_port_indices.empty()
                      && !ns.has_texture_output
                      && ns.custom_output_port_indices.empty();
    }

    // Pre-allocate custom output buffers (one slot per custom-type output port)
    ns.custom_output_buf.assign(ns.custom_output_port_indices.size(), nullptr);
    ns.custom_outputs.assign(ns.custom_output_port_indices.size(), nullptr);
    ns.resolved_custom_inputs.assign(ns.custom_input_port_indices.size(), nullptr);

}

bool Scheduler::build(const Graph& graph, OperatorRegistry& registry) {
    nodes_.clear();
    wires_.clear();
    solo_node_idx_ = -1;
    solo_active_set_.clear();

    // Extract graph base directory for resolving relative file paths
    graph_base_dir_ = std::filesystem::path(graph.source_path()).parent_path();

    // Map node id -> index for lookup
    std::unordered_map<std::string, uint32_t> node_index;
    std::unordered_map<std::string, std::vector<std::string>> incoming_ports;
    std::unordered_map<std::string, std::vector<std::string>> outgoing_ports;

    auto push_unique = [](std::vector<std::string>& v, const std::string& s) {
        if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
    };
    for (const auto& conn : graph.connections()) {
        push_unique(outgoing_ports[conn.from_node], conn.from_port);
        push_unique(incoming_ports[conn.to_node], conn.to_port);
    }

    // 1. Create NodeStates
    for (const auto& ndef : graph.nodes()) {
        OperatorLoader* loader = registry.find(ndef.type);
        std::unique_ptr<OperatorLoader> owned;

        if (!loader && registry.is_wgsl_preset(ndef.type)) {
            // Backward compat: old graph with "type": "HSV" → per-instance loader
            auto* cfg = registry.wgsl_config(ndef.type);
            if (cfg) {
                owned = std::make_unique<OperatorLoader>();
                owned->init_data_driven(*cfg);
                loader = owned.get();
            }
        } else if (loader && ndef.type == "WGSLFilter") {
            // New format: read filter from string_params
            auto it = ndef.string_params.find("filter");
            if (it != ndef.string_params.end()) {
                auto* cfg = registry.wgsl_config(it->second);
                if (cfg) {
                    owned = std::make_unique<OperatorLoader>();
                    owned->init_data_driven(*cfg);
                    loader = owned.get();
                }
            }
        }

        const VividOperatorDescriptor* desc = loader ? loader->descriptor() : nullptr;

        NodeState ns;
        ns.node_id = ndef.id;
        ns.type_name = ndef.type;
        ns.loader = loader;
        ns.owned_loader = std::move(owned);
        ns.generation = 0;
        if (loader && desc) {
            ns.instance = loader->create_instance();
            init_node_state(ns, desc, &ndef.params,
                            ndef.string_params.empty() ? nullptr : &ndef.string_params);

            // Apply per-parameter lock flags from graph definition
            for (const auto& [pname, flags] : ndef.param_lock_flags) {
                auto pi = ns.param_indices.find(pname);
                if (pi != ns.param_indices.end())
                    ns.param_lock_flags[pi->second] = flags;
            }

            // Per-node GPU texture resolution from graph definition
            if (ns.is_gpu) {
                ns.gpu_tex_width  = ndef.tex_width;
                ns.gpu_tex_height = ndef.tex_height;
            }
        } else {
            // Missing operator placeholder (e.g. package uninstalled while graph still references it).
            ns.missing_operator = true;
            ns.instance = nullptr;
            ns.time_dependent = false;
            ns.is_gpu = false;
            ns.is_audio = false;
            ns.is_gpu_sink = false;
            ns.has_texture_output = false;

            const auto& in_names = incoming_ports[ndef.id];
            const auto& out_names = outgoing_ports[ndef.id];
            ns.input_port_count = static_cast<uint32_t>(in_names.size());
            ns.output_port_count = static_cast<uint32_t>(out_names.size());
            for (uint32_t i = 0; i < ns.input_port_count; ++i)
                ns.input_port_indices[in_names[i]] = i;
            for (uint32_t i = 0; i < ns.output_port_count; ++i)
                ns.output_port_indices[out_names[i]] = i;
            ns.input_port_types.assign(ns.input_port_count, VIVID_PORT_SIGNAL);
            ns.output_port_types.assign(ns.output_port_count, VIVID_PORT_SIGNAL);

            ns.input_values.assign(ns.input_port_count, 0.0f);
            ns.output_values.assign(ns.output_port_count, 0.0f);
            ns.input_string_values.assign(ns.input_port_count, "");
            ns.output_string_values.assign(ns.output_port_count, "");
            ns.c_input_string_values.assign(ns.input_port_count, nullptr);
            ns.c_output_string_values.assign(ns.output_port_count, nullptr);
            ns.prev_output_values.assign(ns.output_port_count, 0.0f);
            ns.input_spreads.resize(ns.input_port_count);
            ns.output_spreads.resize(ns.output_port_count);
            ns.input_string_spreads.resize(ns.input_port_count);
            ns.output_string_spreads.resize(ns.output_port_count);

            uint32_t pidx = 0;
            for (const auto& [pname, pval] : ndef.params) {
                ns.param_indices[pname] = pidx++;
                ns.param_values.push_back(pval);
            }
            ns.param_lock_flags.assign(ns.param_values.size(), PARAM_LOCK_NONE);
            for (const auto& [pname, flags] : ndef.param_lock_flags) {
                auto pi = ns.param_indices.find(pname);
                if (pi != ns.param_indices.end())
                    ns.param_lock_flags[pi->second] = flags;
            }

            ns.c_in_spreads.resize(ns.input_port_count);
            ns.c_out_spreads.resize(ns.output_port_count);
            ns.out_spread_buf.resize(ns.output_port_count);
            ns.c_in_string_spreads.resize(ns.input_port_count);
            ns.c_out_string_spreads.resize(ns.output_port_count);
            ns.in_string_spread_ptrs.resize(ns.input_port_count);
            ns.out_string_spread_ptr_buf.resize(ns.output_port_count);
            for (uint32_t p = 0; p < ns.output_port_count; ++p)
                ns.out_spread_buf[p].resize(kMaxSpreadCapacity, 0.0f);
            for (uint32_t p = 0; p < ns.input_port_count; ++p)
                ns.in_string_spread_ptrs[p].resize(kMaxSpreadCapacity, nullptr);
            for (uint32_t p = 0; p < ns.output_port_count; ++p)
                ns.out_string_spread_ptr_buf[p].resize(kMaxSpreadCapacity, nullptr);

            if (registry.has_abi_mismatch_diagnostics()) {
                std::fprintf(stderr,
                             "[vivid] Scheduler: missing operator type '%s' (node '%s') — using placeholder"
                             " (possible plugin ABI mismatch; rebuild vivid and rerun package rebuild)\n",
                             ndef.type.c_str(), ndef.id.c_str());
            } else {
                std::fprintf(stderr, "[vivid] Scheduler: missing operator type '%s' (node '%s') — using placeholder\n",
                             ndef.type.c_str(), ndef.id.c_str());
            }

            // Warn if this looks like a GPU-domain operator — texture chains will be broken.
            for (const auto& name : out_names) {
                if (name == "texture" || name == "output") {
                    std::fprintf(stderr,
                        "[vivid] WARNING: missing operator '%s' (node '%s') appears to be GPU-domain"
                        " (has output port '%s'). GPU texture chain will be broken — outputs will be null.\n",
                        ndef.type.c_str(), ndef.id.c_str(), name.c_str());
                    break;
                }
            }
        }

        node_index[ndef.id] = static_cast<uint32_t>(nodes_.size());
        nodes_.push_back(std::move(ns));
    }

    // 2. Resolve connections -> Wires
    // Also build adjacency for topological sort
    uint32_t n = static_cast<uint32_t>(nodes_.size());
    std::vector<std::vector<uint32_t>> adj(n);     // adj[from] = list of to
    std::vector<uint32_t> in_degree(n, 0);
    std::vector<std::vector<uint32_t>> string_in_fanin(n);
    std::vector<std::vector<uint32_t>> string_spread_in_fanin(n);
    for (uint32_t i = 0; i < n; ++i) {
        string_in_fanin[i].assign(nodes_[i].input_port_count, 0);
        string_spread_in_fanin[i].assign(nodes_[i].input_port_count, 0);
    }

    for (const auto& conn : graph.connections()) {
        auto from_it = node_index.find(conn.from_node);
        auto to_it   = node_index.find(conn.to_node);
        if (from_it == node_index.end()) {
            std::fprintf(stderr, "[vivid] Scheduler: unknown source node '%s' — skipping wire\n", conn.from_node.c_str());
            continue;
        }
        if (to_it == node_index.end()) {
            std::fprintf(stderr, "[vivid] Scheduler: unknown target node '%s' — skipping wire\n", conn.to_node.c_str());
            continue;
        }

        uint32_t fi = from_it->second;
        uint32_t ti = to_it->second;

        auto& from_ns = nodes_[fi];
        auto& to_ns   = nodes_[ti];

        VividPortType from_port_type = VIVID_PORT_SIGNAL;
        bool source_is_param = false;
        uint32_t from_port_idx = 0;

        auto fp_it = from_ns.output_port_indices.find(conn.from_port);
        if (fp_it != from_ns.output_port_indices.end()) {
            from_port_idx = fp_it->second;
            // Determine source port type from descriptor
            if (from_ns.loader && from_ns.loader->descriptor()) {
                const auto* from_desc = from_ns.loader->descriptor();
                uint32_t out_idx = 0;
                for (uint32_t pi = 0; pi < from_desc->port_count; ++pi) {
                    if (from_desc->ports[pi].direction == VIVID_PORT_OUTPUT) {
                        if (out_idx == fp_it->second) {
                            from_port_type = from_desc->ports[pi].type;
                            break;
                        }
                        out_idx++;
                    }
                }
            }
        } else {
            // Fallback: try param
            auto pp_it = from_ns.param_indices.find(conn.from_port);
            if (pp_it == from_ns.param_indices.end()) {
                std::fprintf(stderr, "[vivid] Scheduler: node '%s' has no output port or parameter '%s' — skipping wire\n",
                    conn.from_node.c_str(), conn.from_port.c_str());
                continue;
            }
            from_port_idx = pp_it->second;
            source_is_param = true;
            // Check actual param type — file/text params carry string data
            auto fp_src_it = from_ns.file_param_indices.find(conn.from_port);
            if (fp_src_it != from_ns.file_param_indices.end()) {
                from_port_type = VIVID_PORT_STRING;
            } else {
                from_port_type = VIVID_PORT_SIGNAL;
            }
        }

        Wire w;
        w.from_node_idx = fi;
        w.from_port_idx = from_port_idx;
        w.sources_param = source_is_param;
        if (source_is_param && from_port_type == VIVID_PORT_STRING) {
            auto fp_src_it2 = from_ns.file_param_indices.find(conn.from_port);
            w.sources_file_param = true;
            w.from_file_param_idx = fp_src_it2->second;
        }
        w.to_node_idx   = ti;

        auto tp_it = to_ns.input_port_indices.find(conn.to_port);
        if (tp_it != to_ns.input_port_indices.end()) {
            w.to_port_idx = tp_it->second;
            w.targets_param = false;

            // Determine destination port type
            VividPortType to_port_type = VIVID_PORT_SIGNAL;
            const VividOperatorDescriptor* to_op_desc = nullptr;
            if (to_ns.loader && to_ns.loader->descriptor()) {
                to_op_desc = to_ns.loader->descriptor();
                uint32_t inp_idx = 0;
                for (uint32_t pi = 0; pi < to_op_desc->port_count; ++pi) {
                    if (to_op_desc->ports[pi].direction == VIVID_PORT_INPUT) {
                        if (inp_idx == tp_it->second) {
                            to_port_type = to_op_desc->ports[pi].type;
                            break;
                        }
                        inp_idx++;
                    }
                }
            }

            if (!from_ns.missing_operator && !to_ns.missing_operator) {
                if (from_port_type == VIVID_PORT_STRING &&
                    to_port_type == VIVID_PORT_STRING) {
                    w.is_string_wire = true;
                    string_in_fanin[ti][w.to_port_idx]++;
                } else if (from_port_type == VIVID_PORT_STRING_SPREAD &&
                           to_port_type == VIVID_PORT_STRING_SPREAD) {
                    w.is_string_spread_wire = true;
                    string_spread_in_fanin[ti][w.to_port_idx]++;
                } else if (from_port_type == VIVID_PORT_STRING ||
                           from_port_type == VIVID_PORT_STRING_SPREAD ||
                           to_port_type == VIVID_PORT_STRING ||
                           to_port_type == VIVID_PORT_STRING_SPREAD) {
                    std::fprintf(stderr, "[vivid] Scheduler: type mismatch on wire %s/%s -> %s/%s "
                        "(string port types must match exactly)\n",
                        conn.from_node.c_str(), conn.from_port.c_str(),
                        conn.to_node.c_str(), conn.to_port.c_str());
                    return false;
                }
            }

            // Validate texture wire: both ends must be GPU_TEXTURE
            if (!from_ns.missing_operator && !to_ns.missing_operator) {
                if (from_port_type == VIVID_PORT_TEXTURE &&
                    to_port_type == VIVID_PORT_TEXTURE) {
                    w.is_texture_wire = true;
                } else if (from_port_type == VIVID_PORT_TEXTURE ||
                           to_port_type == VIVID_PORT_TEXTURE) {
                    std::fprintf(stderr, "[vivid] Scheduler: type mismatch on wire %s/%s -> %s/%s "
                        "(GPU_TEXTURE on only one end)\n",
                        conn.from_node.c_str(), conn.from_port.c_str(),
                        conn.to_node.c_str(), conn.to_port.c_str());
                    continue;  // skip this wire
                }
            }

            // Validate custom wire: both ends must be custom port types with matching type + transport
            if (!from_ns.missing_operator && !to_ns.missing_operator &&
                vivid_is_custom_port_type(from_port_type) && vivid_is_custom_port_type(to_port_type)) {
                const VividPortDescriptor* from_pd = nullptr;
                const VividPortDescriptor* to_pd = nullptr;
                {
                    const auto* from_op_desc = from_ns.loader->descriptor();
                    uint32_t oi = 0;
                    for (uint32_t pi2 = 0; pi2 < from_op_desc->port_count; ++pi2) {
                        if (from_op_desc->ports[pi2].direction == VIVID_PORT_OUTPUT) {
                            if (oi == fp_it->second) { from_pd = &from_op_desc->ports[pi2]; break; }
                            oi++;
                        }
                    }
                }
                {
                    uint32_t ii = 0;
                    for (uint32_t pi2 = 0; pi2 < to_op_desc->port_count; ++pi2) {
                        if (to_op_desc->ports[pi2].direction == VIVID_PORT_INPUT) {
                            if (ii == tp_it->second) { to_pd = &to_op_desc->ports[pi2]; break; }
                            ii++;
                        }
                    }
                }
                if (!from_pd || !to_pd || from_pd->type != to_pd->type ||
                                           from_pd->transport != to_pd->transport) {
                    std::fprintf(stderr, "[vivid] Scheduler: custom port type/transport mismatch on wire "
                        "%s/%s -> %s/%s\n",
                        conn.from_node.c_str(), conn.from_port.c_str(),
                        conn.to_node.c_str(), conn.to_port.c_str());
                    continue;
                }
                w.is_custom_wire = true;
            } else if (!from_ns.missing_operator && !to_ns.missing_operator &&
                       (vivid_is_custom_port_type(from_port_type) != vivid_is_custom_port_type(to_port_type))) {
                std::fprintf(stderr, "[vivid] Scheduler: type mismatch on wire %s/%s -> %s/%s "
                    "(custom port on only one end)\n",
                    conn.from_node.c_str(), conn.from_port.c_str(),
                    conn.to_node.c_str(), conn.to_port.c_str());
                continue;  // skip this wire
            }
        } else {
            // Check if target is a file/text param first
            auto fp_it2 = to_ns.file_param_indices.find(conn.to_port);
            if (fp_it2 != to_ns.file_param_indices.end()) {
                // String param target — validate source is string-compatible
                if (from_port_type != VIVID_PORT_STRING) {
                    std::fprintf(stderr, "[vivid] Scheduler: type mismatch on wire %s/%s -> %s/%s "
                        "(float source cannot wire to string param)\n",
                        conn.from_node.c_str(), conn.from_port.c_str(),
                        conn.to_node.c_str(), conn.to_port.c_str());
                    continue;
                }
                w.targets_file_param = true;
                w.to_file_param_idx = fp_it2->second;
                w.is_string_wire = true;
                // to_port_idx unused for file param targets, but set for safety
                auto pp_it = to_ns.param_indices.find(conn.to_port);
                w.to_port_idx = (pp_it != to_ns.param_indices.end()) ? pp_it->second : 0;
                w.targets_param = true;
            } else {
                auto pp_it = to_ns.param_indices.find(conn.to_port);
                if (pp_it == to_ns.param_indices.end()) {
                    std::fprintf(stderr, "[vivid] Scheduler: node '%s' has no input port or parameter '%s' — skipping wire\n",
                        conn.to_node.c_str(), conn.to_port.c_str());
                    continue;
                }
                w.to_port_idx = pp_it->second;
                w.targets_param = true;
            }
        }
        w.from_min = conn.from_min;
        w.from_max = conn.from_max;
        w.to_min   = conn.to_min;
        w.to_max   = conn.to_max;
        w.clamp    = conn.clamp;
        wires_.push_back(w);

        adj[fi].push_back(ti);
        in_degree[ti]++;
    }

    for (uint32_t ni = 0; ni < n; ++ni) {
        for (uint32_t pi = 0; pi < nodes_[ni].input_port_count; ++pi) {
            if (string_in_fanin[ni][pi] > 1) {
                std::fprintf(stderr, "[vivid] Scheduler: invalid fan-in on string input '%s/%u' "
                    "(%u wires, max 1)\n",
                    nodes_[ni].node_id.c_str(), pi, string_in_fanin[ni][pi]);
                return false;
            }
            if (string_spread_in_fanin[ni][pi] > 1) {
                std::fprintf(stderr, "[vivid] Scheduler: invalid fan-in on string spread input '%s/%u' "
                    "(%u wires, max 1)\n",
                    nodes_[ni].node_id.c_str(), pi, string_spread_in_fanin[ni][pi]);
                return false;
            }
        }
    }

    // 3. Topological sort
    auto sorted_order = kahn_sort(n, adj, in_degree);
    if (sorted_order.empty()) {
        std::fprintf(stderr, "[vivid] Scheduler: cycle detected in graph\n");
        return false;
    }

    // 4. Reindex: build old->new index mapping, reorder nodes_, remap wires
    std::vector<uint32_t> old_to_new(n);
    for (uint32_t i = 0; i < n; ++i) {
        old_to_new[sorted_order[i]] = i;
    }

    std::vector<NodeState> sorted_nodes(n);
    for (uint32_t i = 0; i < n; ++i) {
        sorted_nodes[old_to_new[i]] = std::move(nodes_[i]);
    }
    nodes_ = std::move(sorted_nodes);

    for (auto& w : wires_) {
        w.from_node_idx = old_to_new[w.from_node_idx];
        w.to_node_idx   = old_to_new[w.to_node_idx];
    }

    // Re-point loader for nodes with per-instance owned loaders after reorder
    for (auto& ns : nodes_) {
        if (ns.owned_loader) ns.loader = ns.owned_loader.get();
    }

    // Build per-node list of upstream node indices for generation tracking
    for (auto& ns : nodes_)
        ns.upstream_nodes.clear();
    for (const auto& w : wires_) {
        auto& ups = nodes_[w.to_node_idx].upstream_nodes;
        bool found = false;
        for (auto idx : ups) {
            if (idx == w.from_node_idx) { found = true; break; }
        }
        if (!found)
            ups.push_back(w.from_node_idx);
    }
    // (upstream_nodes built above; no per-upstream cached state needed)

    // Print evaluation order (gated on VIVID_VERBOSE to avoid noise on every hot-reload)
    if (std::getenv("VIVID_VERBOSE")) {
        std::fprintf(stderr, "[vivid] Evaluation order:");
        for (uint32_t i = 0; i < n; ++i) {
            std::fprintf(stderr, "%s%s", (i == 0 ? " " : " → "), nodes_[i].node_id.c_str());
        }
        std::fprintf(stderr, "\n");
    }

    // ── Cadence-aware runtime: compile the graph in parallel ────────────
    // The CompiledGraph is built alongside the legacy NodeState/Wire arrays.
    // Both exist during the adapter phase; the CompiledGraph will eventually
    // replace NodeState entirely.
    {
        GraphCompiler::Options opts;
        opts.graph_base_dir = graph_base_dir_;
        opts.operators_src_dir = operators_src_dir_;
        compiled_graph_ = GraphCompiler::compile(graph, registry, opts);
        if (compiled_graph_) {
            cadence_bridge_.build(*compiled_graph_);
            frame_executor_.set_operators_src_dir(operators_src_dir_);
            if (std::getenv("VIVID_VERBOSE")) {
                std::fprintf(stderr, "[vivid] CompiledGraph: %zu nodes (%zu frame, %zu audio), "
                             "%zu edges (%zu snapshot)\n",
                             compiled_graph_->nodes.size(),
                             compiled_graph_->frame_order.size(),
                             compiled_graph_->audio_order.size(),
                             compiled_graph_->edges.size(),
                             compiled_graph_->frame_to_audio_edges.size() +
                             compiled_graph_->audio_to_frame_edges.size());
            }
        } else {
            std::fprintf(stderr, "[vivid] CompiledGraph: compile failed (non-fatal, legacy path active)\n");
        }
    }

    return true;
}

void Scheduler::tick(double time, double delta_time, uint64_t frame, void* gpu_state,
                     PostNodeFn on_gpu_node, const VividInputState* input) {
    // ── Cadence-aware executor path ─────────────────────────────────────
    if (compiled_graph_) {
        // Sync NodeState → CompiledNode (external modifications since last tick)
        for (uint32_t i = 0; i < static_cast<uint32_t>(nodes_.size()); ++i) {
            auto* cn = compiled_graph_->find_node(nodes_[i].node_id);
            if (!cn) continue;
            if (cn->active_cadence == Cadence::Audio) continue;  // audio state managed by AudioEngine
            // Instance/loader (may change on hot-reload)
            cn->loader = nodes_[i].loader;
            cn->instance = nodes_[i].instance;
            cn->missing_operator = nodes_[i].missing_operator;
            cn->param_values = nodes_[i].param_values;
            cn->param_lock_flags = nodes_[i].param_lock_flags;
            cn->file_param_storage = nodes_[i].file_param_storage;
            cn->file_param_ptrs.resize(cn->file_param_storage.size());
            for (size_t j = 0; j < cn->file_param_storage.size(); ++j)
                cn->file_param_ptrs[j] = cn->file_param_storage[j].c_str();
            cn->generation = nodes_[i].generation;
            cn->errored = nodes_[i].errored;
            cn->error_message = nodes_[i].error_message;
            // GPU texture handles (allocated by allocate_gpu_textures on NodeState)
            cn->gpu_texture = nodes_[i].gpu_texture;
            cn->gpu_texture_view = nodes_[i].gpu_texture_view;
            cn->gpu_tex_width = nodes_[i].gpu_tex_width;
            cn->gpu_tex_height = nodes_[i].gpu_tex_height;
            cn->gpu_tex_inherited = nodes_[i].gpu_tex_inherited;
            cn->aux_gpu_textures = nodes_[i].aux_gpu_textures;
            cn->aux_gpu_texture_views = nodes_[i].aux_gpu_texture_views;
        }

        // Run the new executor
        frame_executor_.tick(*compiled_graph_, time, delta_time, frame,
                             gpu_state, on_gpu_node, input);

        // Sync CompiledNode → NodeState (results for inspector/MCP/UI)
        for (uint32_t i = 0; i < static_cast<uint32_t>(nodes_.size()); ++i) {
            auto* cn = compiled_graph_->find_node(nodes_[i].node_id);
            if (!cn) continue;
            nodes_[i].input_values = cn->input_values;
            nodes_[i].output_values = cn->output_values;
            nodes_[i].output_spreads = cn->output_spreads;
            nodes_[i].output_string_values = cn->output_string_values;
            nodes_[i].output_string_spreads = cn->output_string_spreads;
            nodes_[i].param_values = cn->param_values;
            nodes_[i].errored = cn->errored;
            nodes_[i].error_message = cn->error_message;
            nodes_[i].gpu_shader_error = cn->gpu_shader_error;
            nodes_[i].gpu_shader_error_msg = cn->gpu_shader_error_msg;
            nodes_[i].generation = cn->generation;
            nodes_[i].last_processed_gen = cn->last_processed_gen;
            nodes_[i].processed_this_tick = cn->processed_this_tick;
            nodes_[i].gpu_texture = cn->gpu_texture;
            nodes_[i].gpu_texture_view = cn->gpu_texture_view;
            nodes_[i].gpu_tex_width = cn->gpu_tex_width;
            nodes_[i].gpu_tex_height = cn->gpu_tex_height;
        }

        needs_gpu_realloc_ = frame_executor_.needs_gpu_realloc();
        if (needs_gpu_realloc_) frame_executor_.clear_gpu_realloc();

        // Propagate control→audio param wires for inspector display and push_params staging.
        // Audio nodes are skipped by the frame executor but their scheduler-side param_values
        // must reflect modulation so the inspector shows animated values and push_params()
        // can read the current modulated value.
        for (const auto& w : wires_) {
            if (!w.targets_param) continue;
            auto& to_ns = nodes_[w.to_node_idx];
            if (!to_ns.is_audio) continue;
            if (w.targets_file_param) {
                const std::string& src = w.sources_file_param
                    ? nodes_[w.from_node_idx].file_param_storage[w.from_file_param_idx]
                    : nodes_[w.from_node_idx].output_string_values[w.from_port_idx];
                to_ns.file_param_storage[w.to_file_param_idx] = src;
                to_ns.file_param_ptrs[w.to_file_param_idx] =
                    to_ns.file_param_storage[w.to_file_param_idx].c_str();
                continue;
            }
            float raw = w.sources_param
                ? nodes_[w.from_node_idx].param_values[w.from_port_idx]
                : nodes_[w.from_node_idx].output_values[w.from_port_idx];
            float val = has_remap(w) ? apply_remap(raw, w) : raw;
            if (!(to_ns.param_lock_flags[w.to_port_idx] & PARAM_LOCK_WIRES))
                to_ns.param_values[w.to_port_idx] = val;
        }
        return;
    }

    // CompiledGraph should always be available after build().
    // If we reach here, it means GraphCompiler::compile() failed — log and bail.
    std::fprintf(stderr, "[vivid] Scheduler::tick() skipped: CompiledGraph not available\n");
}

bool Scheduler::has_gpu_operators() const {
    for (const auto& ns : nodes_) {
        if (!ns.loader) continue;
        const VividOperatorDescriptor* desc = ns.loader->descriptor();
        if (!desc) continue;
        if (desc->has_process_gpu)
            return true;
    }
    return false;
}

bool Scheduler::has_audio_operators() const {
    for (const auto& ns : nodes_) {
        if (!ns.loader) continue;
        const VividOperatorDescriptor* desc = ns.loader->descriptor();
        if (!desc) continue;
        if (desc->has_process_audio)
            return true;
    }
    return false;
}

bool Scheduler::gpu_sink_source_size(int sink_idx, uint32_t& w, uint32_t& h) const {
    for (const auto& wire : wires_) {
        if (wire.to_node_idx == static_cast<uint32_t>(sink_idx) &&
            wire.is_texture_wire && !wire.targets_param) {
            w = nodes_[wire.from_node_idx].gpu_tex_width;
            h = nodes_[wire.from_node_idx].gpu_tex_height;
            return w > 0 && h > 0;
        }
    }
    return false;
}

WGPUTexture Scheduler::gpu_sink_source_texture(int sink_idx) const {
    for (const auto& wire : wires_) {
        if (wire.to_node_idx == static_cast<uint32_t>(sink_idx) &&
            wire.is_texture_wire && !wire.targets_param) {
            const auto& up = nodes_[wire.from_node_idx];
            for (size_t ai = 0; ai < up.aux_texture_output_port_indices.size(); ++ai) {
                if (wire.from_port_idx ==
                        static_cast<uint32_t>(up.aux_texture_output_port_indices[ai]))
                    return up.aux_gpu_textures[ai];
            }
            return up.gpu_texture;  // primary
        }
    }
    return nullptr;
}

bool Scheduler::is_audio_type(const std::string& type_name) const {
    for (const auto& ns : nodes_) {
        if (!ns.loader) continue;
        const VividOperatorDescriptor* desc = ns.loader->descriptor();
        if (!desc) continue;
        if (std::string(desc->name) == type_name &&
            desc->has_process_audio) {
            return true;
        }
    }
    return false;
}

void Scheduler::inject_external_output(uint32_t node_idx, uint32_t port_idx, float value) {
    if (node_idx >= nodes_.size()) return;
    auto& ns = nodes_[node_idx];
    if (port_idx >= ns.output_values.size()) return;
    if (ns.output_values[port_idx] != value) {
        ns.output_values[port_idx] = value;
        ns.prev_output_values[port_idx] = value;
        ns.generation++;
    }
    // Also update CompiledNode if available
    if (compiled_graph_ && node_idx < compiled_graph_->nodes.size()) {
        auto& cn = compiled_graph_->nodes[node_idx];
        if (port_idx < cn.output_values.size()) {
            cn.output_values[port_idx] = value;
            if (port_idx < cn.prev_output_values.size())
                cn.prev_output_values[port_idx] = value;
            cn.generation++;
        }
    }
}

void Scheduler::inject_external_spread(uint32_t node_idx, uint32_t port_idx,
                                       const float* data, uint32_t length) {
    if (node_idx >= nodes_.size()) return;
    auto& ns = nodes_[node_idx];
    if (port_idx >= ns.output_spreads.size()) return;
    if (length > kMaxSpreadCapacity) length = kMaxSpreadCapacity;
    ns.output_spreads[port_idx].assign(data, data + length);
    ns.generation++;
    // Also update CompiledNode if available
    if (compiled_graph_ && node_idx < compiled_graph_->nodes.size()) {
        auto& cn = compiled_graph_->nodes[node_idx];
        if (port_idx < cn.output_spreads.size()) {
            cn.output_spreads[port_idx].assign(data, data + length);
            cn.generation++;
        }
    }
}

const NodeState* Scheduler::find_node(const std::string& id) const {
    for (const auto& ns : nodes_) {
        if (ns.node_id == id) return &ns;
    }
    return nullptr;
}

NodeState* Scheduler::find_node_mut(const std::string& id) {
    for (auto& ns : nodes_) {
        if (ns.node_id == id) return &ns;
    }
    return nullptr;
}

std::string Scheduler::type_name(uint32_t node_idx) const {
    if (node_idx >= nodes_.size()) return {};
    const auto& ns = nodes_[node_idx];
    if (!ns.type_name.empty()) return ns.type_name;
    if (!ns.loader) return {};
    const auto* desc = ns.loader->descriptor();
    return desc ? desc->name : std::string{};
}

bool Scheduler::reload_operator(const std::string& type_name, OperatorRegistry& registry,
                                const std::string& new_dylib_path) {
    // 1. Find all nodes of this type and save their param values by name
    struct SavedParams {
        uint32_t node_idx;
        std::unordered_map<std::string, float> values;
        std::unordered_map<std::string, std::string> string_values;
        std::unordered_map<std::string, uint8_t> lock_flags;
    };
    std::vector<SavedParams> saved;

    for (uint32_t i = 0; i < static_cast<uint32_t>(nodes_.size()); ++i) {
        auto& ns = nodes_[i];
        if (!ns.loader) continue;
        const auto* desc = ns.loader->descriptor();
        if (!desc || std::string(desc->name) != type_name) continue;

        SavedParams sp;
        sp.node_idx = i;
        // Save param values and lock flags by name
        for (const auto& [name, idx] : ns.param_indices) {
            sp.values[name] = ns.param_values[idx];
            if (ns.param_lock_flags[idx] != PARAM_LOCK_NONE)
                sp.lock_flags[name] = ns.param_lock_flags[idx];
        }
        for (const auto& [name, idx] : ns.file_param_indices) {
            sp.string_values[name] = ns.file_param_storage[idx];
        }
        saved.push_back(std::move(sp));
    }

    if (saved.empty()) return true;  // no instances to reload

    // 2. Destroy old instances while the old dylib is still loaded (safe to call old destroy)
    for (const auto& sp : saved) {
        auto& ns = nodes_[sp.node_idx];
        if (ns.instance) {
            ns.loader->destroy_instance(ns.instance);
            ns.instance = nullptr;
        }
    }

    // 3. Reload the dylib (atomic swap in OperatorLoader::load() — old dylib remains live on failure)
    if (!registry.reload_operator(type_name, new_dylib_path)) {
        std::fprintf(stderr, "[vivid] Scheduler: dylib reload failed for '%s'\n", type_name.c_str());
        // Old dylib is still loaded (Fix 1). Recreate instances using old loader so nodes keep running.
        OperatorLoader* old_loader = registry.find(type_name);
        if (old_loader && old_loader->is_loaded()) {
            const auto* old_desc = old_loader->descriptor();
            if (old_desc) {
                for (const auto& sp : saved) {
                    auto& ns = nodes_[sp.node_idx];
                    ns.instance = old_loader->create_instance();
                    init_node_state(ns, old_desc, &sp.values,
                                    sp.string_values.empty() ? nullptr : &sp.string_values);
                    for (const auto& [pname, flags] : sp.lock_flags) {
                        auto pi = ns.param_indices.find(pname);
                        if (pi != ns.param_indices.end())
                            ns.param_lock_flags[pi->second] = flags;
                    }
                    ns.generation++;
                }
            }
        }
        return false;
    }

    // 4. Update loader pointer and recreate instances with param reconciliation
    OperatorLoader* new_loader = registry.find(type_name);
    if (!new_loader) return false;
    const auto* new_desc = new_loader->descriptor();
    if (!new_desc) return false;

    for (const auto& sp : saved) {
        auto& ns = nodes_[sp.node_idx];
        ns.loader = new_loader;
        ns.instance = new_loader->create_instance();
        init_node_state(ns, new_desc, &sp.values,
                        sp.string_values.empty() ? nullptr : &sp.string_values);

        // Restore lock flags
        for (const auto& [pname, flags] : sp.lock_flags) {
            auto pi = ns.param_indices.find(pname);
            if (pi != ns.param_indices.end())
                ns.param_lock_flags[pi->second] = flags;
        }

        // Clear error state on successful reload
        ns.errored = false;
        ns.error_message.clear();

        // Bump generation to force downstream recompute
        ns.generation++;
    }

    // Recompile the CompiledGraph to pick up new operator instances and descriptors.
    // The port layout may change on reload (new params, new ports).
    if (compiled_graph_) {
        // Rebuild CompiledNode state for reloaded nodes by syncing from updated NodeState.
        // A full recompile would be cleaner, but syncing loader+instance+ports is sufficient
        // since the graph topology hasn't changed — only the operator implementation.
        for (const auto& sp : saved) {
            auto* cn = compiled_graph_->find_node(nodes_[sp.node_idx].node_id);
            if (!cn) continue;
            auto& ns = nodes_[sp.node_idx];
            cn->loader = ns.loader;
            cn->instance = ns.instance;
            cn->param_values = ns.param_values;
            cn->param_lock_flags = ns.param_lock_flags;
            cn->param_indices = ns.param_indices;
            cn->input_port_count = ns.input_port_count;
            cn->output_port_count = ns.output_port_count;
            cn->input_port_indices = ns.input_port_indices;
            cn->output_port_indices = ns.output_port_indices;
            cn->input_port_types = ns.input_port_types;
            cn->output_port_types = ns.output_port_types;
            cn->input_values.resize(ns.input_port_count, 0.0f);
            cn->output_values.resize(ns.output_port_count, 0.0f);
            cn->prev_output_values.resize(ns.output_port_count, 0.0f);
            cn->errored = false;
            cn->error_message.clear();
            cn->generation = ns.generation;
        }
    }

    return true;
}

void Scheduler::allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                                      WGPUTextureFormat format,
                                      WGPUTextureUsage extra_usage) {
    gpu_device_ = device;
    // Iterate nodes in topological order (they're already sorted)
    for (uint32_t ni = 0; ni < static_cast<uint32_t>(nodes_.size()); ++ni) {
        auto& ns = nodes_[ni];
        if (!ns.is_gpu) continue;

        // Release existing primary textures.  Aux texture views/textures are NOT
        // released here — operators may have written their own views (with a
        // different format) into aux slots, so the scheduler must not free them.
        // Operators manage aux resource lifetimes in their own destructors.
        if (ns.gpu_texture_view) { wgpuTextureViewRelease(ns.gpu_texture_view); ns.gpu_texture_view = nullptr; }
        if (ns.gpu_texture) { wgpuTextureRelease(ns.gpu_texture); ns.gpu_texture = nullptr; }
        for (auto& v : ns.aux_gpu_texture_views) v = nullptr;
        for (auto& t : ns.aux_gpu_textures)      t = nullptr;

        // GPU sinks and scene-only nodes don't produce their own textures
        ns.gpu_tex_inherited = false;
        if (ns.is_gpu_sink || !ns.has_texture_output) {
            ns.gpu_tex_width  = 0;
            ns.gpu_tex_height = 0;
            continue;
        }

        // Resolve texture size
        uint32_t w = ns.gpu_tex_width;
        uint32_t h = ns.gpu_tex_height;

        // Nodes with texture inputs always inherit from upstream (filters).
        // Nodes without texture inputs keep their own size (generators).
        if (!ns.texture_input_port_indices.empty()) {
            uint32_t first_tex_port = ns.texture_input_port_indices[0];
            for (const auto& wire : wires_) {
                if (wire.to_node_idx == ni && !wire.targets_param &&
                    wire.to_port_idx == first_tex_port && wire.is_texture_wire) {
                    const auto& upstream = nodes_[wire.from_node_idx];
                    if (upstream.gpu_tex_width > 0 && upstream.gpu_tex_height > 0) {
                        w = upstream.gpu_tex_width;
                        h = upstream.gpu_tex_height;
                        ns.gpu_tex_inherited = true;
                    }
                    break;
                }
            }
        }

        // Fall back to default if still unresolved
        if (w == 0 || h == 0) {
            w = default_w;
            h = default_h;
        }

        ns.gpu_tex_width  = w;
        ns.gpu_tex_height = h;

        // Create texture
        WGPUTextureDescriptor tex_desc{};
        std::string label = "Node Texture [" + ns.node_id + "]";
        tex_desc.label = to_sv(label.c_str());
        tex_desc.size = { w, h, 1 };
        tex_desc.mipLevelCount = 1;
        tex_desc.sampleCount = 1;
        tex_desc.dimension = WGPUTextureDimension_2D;
        tex_desc.format = format;
        tex_desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
                       | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | extra_usage;
        ns.gpu_texture = wgpuDeviceCreateTexture(device, &tex_desc);
        if (!ns.gpu_texture) {
            std::fprintf(stderr, "[vivid] GPU texture alloc failed for node '%s'\n", ns.node_id.c_str());
            continue;
        }

        WGPUTextureViewDescriptor view_desc{};
        std::string view_label = "Node View [" + ns.node_id + "]";
        view_desc.label = to_sv(view_label.c_str());
        view_desc.format = format;
        view_desc.dimension = WGPUTextureViewDimension_2D;
        view_desc.baseMipLevel = 0;
        view_desc.mipLevelCount = 1;
        view_desc.baseArrayLayer = 0;
        view_desc.arrayLayerCount = 1;
        view_desc.aspect = WGPUTextureAspect_All;
        ns.gpu_texture_view = wgpuTextureCreateView(ns.gpu_texture, &view_desc);
        if (!ns.gpu_texture_view) {
            std::fprintf(stderr, "[vivid] GPU texture view creation failed for node '%s'\n", ns.node_id.c_str());
            wgpuTextureRelease(ns.gpu_texture);
            ns.gpu_texture = nullptr;
            continue;
        }

        // Aux texture slots are left null — operators create their own aux
        // textures with the correct format (e.g. R32Float for depth output)
        // and write the views into ctx->aux_output_texture_views during tick.

        std::fprintf(stderr, "[vivid] Allocated %ux%u texture for node '%s'\n",
                     w, h, ns.node_id.c_str());
    }
}

int Scheduler::find_gpu_sink() const {
    for (uint32_t i = 0; i < static_cast<uint32_t>(nodes_.size()); ++i) {
        if (nodes_[i].is_gpu_sink) return static_cast<int>(i);
    }
    return -1;
}

int Scheduler::find_effective_gpu_sink() const {
    if (solo_node_idx_ >= 0 &&
        solo_node_idx_ < static_cast<int>(nodes_.size()) &&
        nodes_[solo_node_idx_].has_texture_output) {
        return solo_node_idx_;
    }
    return find_gpu_sink();
}

void Scheduler::set_solo(int node_idx) {
    if (node_idx == solo_node_idx_) return;
    if (node_idx < 0 || node_idx >= static_cast<int>(nodes_.size())) {
        solo_node_idx_ = -1;
        solo_active_set_.clear();
        return;
    }
    solo_node_idx_ = node_idx;
    solo_active_set_.assign(nodes_.size(), false);

    // BFS: mark solo node and all transitive upstream dependencies
    std::vector<uint32_t> queue;
    queue.push_back(static_cast<uint32_t>(node_idx));
    solo_active_set_[node_idx] = true;
    while (!queue.empty()) {
        uint32_t cur = queue.back();
        queue.pop_back();
        for (uint32_t up : nodes_[cur].upstream_nodes) {
            if (!solo_active_set_[up]) {
                solo_active_set_[up] = true;
                queue.push_back(up);
            }
        }
    }
}

void Scheduler::shutdown() {
    for (auto& ns : nodes_) {
        // Release per-node primary GPU textures.  Aux views/textures are
        // operator-owned and released by destroy_instance below.
        if (ns.gpu_texture_view) { wgpuTextureViewRelease(ns.gpu_texture_view); ns.gpu_texture_view = nullptr; }
        if (ns.gpu_texture) { wgpuTextureRelease(ns.gpu_texture); ns.gpu_texture = nullptr; }
        for (auto& v : ns.aux_gpu_texture_views) v = nullptr;
        for (auto& t : ns.aux_gpu_textures)      t = nullptr;

        if (ns.instance) {
            if (ns.loader)
                ns.loader->destroy_instance(ns.instance);
            ns.instance = nullptr;
        }
    }
    nodes_.clear();
    wires_.clear();

    // Flush deferred wgpu-core resource cleanup.  After destroying operator
    // instances and releasing per-node textures, storage slots for pipelines,
    // bind groups, etc. are only reclaimed during device.maintain().  Resource
    // releases cascade (pipeline → shader module → layout) and each maintain
    // pass only processes one batch, so we poll multiple times to fully drain
    // the suspected → last_resources → cleanup chain.
    if (gpu_device_) {
        wgpuDevicePoll(gpu_device_, true, nullptr);
        gpu_device_ = nullptr;
    }
}

} // namespace vivid
