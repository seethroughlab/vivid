#include "runtime/scheduler.h"
#include "runtime/crash_guard.h"
#include "runtime/shared_handle_registry.h"
#include "common/gpu_util.h"
#include "common/topo_sort.h"
#include "operator_api/gpu_operator.h"
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
    float t = (range != 0.0f) ? (val - w.from_min) / range : 0.0f;
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
    ns.is_gpu = (desc->domain == VIVID_DOMAIN_GPU);
    ns.is_audio = (desc->domain == VIVID_DOMAIN_AUDIO);
    ns.prev_output_values.assign(ns.output_port_count, 0.0f);

    // Implicit analysis ports for audio-domain nodes
    if (ns.is_audio) {
        ns.output_port_indices["rms"] = ns.output_port_count++;
        ns.output_port_indices["peak"] = ns.output_port_count++;
        ns.output_port_indices["waveform"] = ns.output_port_count++;
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
    ns.data_input_port_indices.clear();
    ns.data_output_port_indices.clear();
    ns.string_input_port_indices.clear();
    ns.string_spread_input_port_indices.clear();
    ns.buffer_input_port_indices.clear();
    ns.buffer_output_port_indices.clear();
    ns.mesh_input_port_indices.clear();
    ns.mesh_output_port_indices.clear();
    ns.compute_input_port_indices.clear();
    ns.compute_output_port_indices.clear();
    ns.is_gpu_sink = false;
    ns.has_texture_output = false;
    ns.has_data_output = false;
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
            if (desc->ports[i].type == VIVID_PORT_GPU_TEXTURE) {
                ns.texture_input_port_indices.push_back(input_idx);
            } else if (desc->ports[i].type == VIVID_PORT_DATA) {
                ns.data_input_port_indices.push_back(input_idx);
            } else if (desc->ports[i].type == VIVID_PORT_CONTROL_STRING) {
                ns.string_input_port_indices.push_back(input_idx);
            } else if (desc->ports[i].type == VIVID_PORT_CONTROL_STRING_SPREAD) {
                ns.string_spread_input_port_indices.push_back(input_idx);
            } else if (desc->ports[i].type == VIVID_PORT_GPU_BUFFER) {
                ns.buffer_input_port_indices.push_back(input_idx);
            } else if (desc->ports[i].type == VIVID_PORT_GPU_MESH) {
                ns.mesh_input_port_indices.push_back(input_idx);
            } else if (desc->ports[i].type == VIVID_PORT_GPU_COMPUTE) {
                ns.compute_input_port_indices.push_back(input_idx);
            }
            input_idx++;
        } else {
            if (desc->ports[i].type == VIVID_PORT_GPU_TEXTURE) {
                ns.has_texture_output = true;
                if (gpu_tex_out_count > 0) {  // 2nd+ texture output → aux
                    ns.aux_texture_output_port_indices.push_back(static_cast<int32_t>(out_idx));
                    ns.aux_gpu_textures.push_back(nullptr);
                    ns.aux_gpu_texture_views.push_back(nullptr);
                }
                ++gpu_tex_out_count;
            } else if (desc->ports[i].type == VIVID_PORT_DATA) {
                ns.has_data_output = true;
                ns.data_output_port_indices.push_back(out_idx);
            } else if (desc->ports[i].type == VIVID_PORT_CONTROL_STRING) {
                ns.has_string_output = true;
            } else if (desc->ports[i].type == VIVID_PORT_CONTROL_STRING_SPREAD) {
                ns.has_string_spread_output = true;
            } else if (desc->ports[i].type == VIVID_PORT_GPU_BUFFER) {
                ns.buffer_output_port_indices.push_back(out_idx);
            } else if (desc->ports[i].type == VIVID_PORT_GPU_MESH) {
                ns.mesh_output_port_indices.push_back(out_idx);
            } else if (desc->ports[i].type == VIVID_PORT_GPU_COMPUTE) {
                ns.compute_output_port_indices.push_back(out_idx);
            }
            out_idx++;
        }
    }
    if (ns.is_gpu) {
        ns.is_gpu_sink = !ns.texture_input_port_indices.empty()
                      && !ns.has_texture_output
                      && !ns.has_data_output;
    }

    // Pre-allocate DATA output buffer (one slot per DATA output port)
    uint32_t data_out_count = static_cast<uint32_t>(ns.data_output_port_indices.size());
    ns.output_data_buf.assign(data_out_count, nullptr);
    ns.gpu_data_outputs.assign(data_out_count, nullptr);

    // Pre-allocate GPU buffer/mesh/compute output buffers
    ns.output_buffer_buf.assign(ns.buffer_output_port_indices.size(), nullptr);
    ns.gpu_buffer_outputs.assign(ns.buffer_output_port_indices.size(), nullptr);
    ns.resolved_buffer_inputs.assign(ns.buffer_input_port_indices.size(), nullptr);

    ns.output_mesh_buf.assign(ns.mesh_output_port_indices.size(), nullptr);
    ns.gpu_mesh_outputs.assign(ns.mesh_output_port_indices.size(), nullptr);
    ns.resolved_mesh_inputs.assign(ns.mesh_input_port_indices.size(), nullptr);

    ns.output_compute_buf.assign(ns.compute_output_port_indices.size(), nullptr);
    ns.gpu_compute_outputs.assign(ns.compute_output_port_indices.size(), nullptr);
    ns.resolved_compute_inputs.assign(ns.compute_input_port_indices.size(), nullptr);
}

bool Scheduler::build(const Graph& graph, OperatorRegistry& registry) {
    nodes_.clear();
    wires_.clear();

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
            ns.input_port_types.assign(ns.input_port_count, VIVID_PORT_CONTROL_FLOAT);
            ns.output_port_types.assign(ns.output_port_count, VIVID_PORT_CONTROL_FLOAT);

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
            std::fprintf(stderr, "[vivid] Scheduler: unknown source node '%s'\n", conn.from_node.c_str());
            return false;
        }
        if (to_it == node_index.end()) {
            std::fprintf(stderr, "[vivid] Scheduler: unknown target node '%s'\n", conn.to_node.c_str());
            return false;
        }

        uint32_t fi = from_it->second;
        uint32_t ti = to_it->second;

        auto& from_ns = nodes_[fi];
        auto& to_ns   = nodes_[ti];

        VividPortType from_port_type = VIVID_PORT_CONTROL_FLOAT;
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
                std::fprintf(stderr, "[vivid] Scheduler: node '%s' has no output port or parameter '%s'\n",
                    conn.from_node.c_str(), conn.from_port.c_str());
                return false;
            }
            from_port_idx = pp_it->second;
            source_is_param = true;
            // Check actual param type — file/text params carry string data
            auto fp_src_it = from_ns.file_param_indices.find(conn.from_port);
            if (fp_src_it != from_ns.file_param_indices.end()) {
                from_port_type = VIVID_PORT_CONTROL_STRING;
            } else {
                from_port_type = VIVID_PORT_CONTROL_FLOAT;
            }
        }

        Wire w;
        w.from_node_idx = fi;
        w.from_port_idx = from_port_idx;
        w.sources_param = source_is_param;
        if (source_is_param && from_port_type == VIVID_PORT_CONTROL_STRING) {
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
            VividPortType to_port_type = VIVID_PORT_CONTROL_FLOAT;
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
                if (from_port_type == VIVID_PORT_CONTROL_STRING &&
                    to_port_type == VIVID_PORT_CONTROL_STRING) {
                    w.is_string_wire = true;
                    string_in_fanin[ti][w.to_port_idx]++;
                } else if (from_port_type == VIVID_PORT_CONTROL_STRING_SPREAD &&
                           to_port_type == VIVID_PORT_CONTROL_STRING_SPREAD) {
                    w.is_string_spread_wire = true;
                    string_spread_in_fanin[ti][w.to_port_idx]++;
                } else if (from_port_type == VIVID_PORT_CONTROL_STRING ||
                           from_port_type == VIVID_PORT_CONTROL_STRING_SPREAD ||
                           to_port_type == VIVID_PORT_CONTROL_STRING ||
                           to_port_type == VIVID_PORT_CONTROL_STRING_SPREAD) {
                    std::fprintf(stderr, "[vivid] Scheduler: type mismatch on wire %s/%s -> %s/%s "
                        "(string port types must match exactly)\n",
                        conn.from_node.c_str(), conn.from_port.c_str(),
                        conn.to_node.c_str(), conn.to_port.c_str());
                    return false;
                }
            }

            // Validate texture wire: both ends must be GPU_TEXTURE
            if (!from_ns.missing_operator && !to_ns.missing_operator) {
                if (from_port_type == VIVID_PORT_GPU_TEXTURE &&
                    to_port_type == VIVID_PORT_GPU_TEXTURE) {
                    w.is_texture_wire = true;
                } else if (from_port_type == VIVID_PORT_GPU_TEXTURE ||
                           to_port_type == VIVID_PORT_GPU_TEXTURE) {
                    std::fprintf(stderr, "[vivid] Scheduler: type mismatch on wire %s/%s -> %s/%s "
                        "(GPU_TEXTURE on only one end)\n",
                        conn.from_node.c_str(), conn.from_port.c_str(),
                        conn.to_node.c_str(), conn.to_port.c_str());
                    continue;  // skip this wire
                }
            }

            // Validate data wire: both ends must be VIVID_PORT_DATA with matching data_type
            if (!from_ns.missing_operator && !to_ns.missing_operator &&
                from_port_type == VIVID_PORT_DATA && to_port_type == VIVID_PORT_DATA) {
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
                const char* from_dt = from_pd ? from_pd->data_type : nullptr;
                const char* to_dt   = to_pd   ? to_pd->data_type   : nullptr;
                if (from_dt && to_dt && std::strcmp(from_dt, to_dt) == 0) {
                    w.is_data_wire = true;
                } else {
                    std::fprintf(stderr, "[vivid] Scheduler: data type mismatch on wire %s/%s -> %s/%s "
                        "('%s' vs '%s')\n",
                        conn.from_node.c_str(), conn.from_port.c_str(),
                        conn.to_node.c_str(), conn.to_port.c_str(),
                        from_dt ? from_dt : "null", to_dt ? to_dt : "null");
                    continue;
                }
            } else if (!from_ns.missing_operator && !to_ns.missing_operator &&
                       (from_port_type == VIVID_PORT_DATA || to_port_type == VIVID_PORT_DATA)) {
                std::fprintf(stderr, "[vivid] Scheduler: type mismatch on wire %s/%s -> %s/%s "
                    "(DATA on only one end)\n",
                    conn.from_node.c_str(), conn.from_port.c_str(),
                    conn.to_node.c_str(), conn.to_port.c_str());
                continue;  // skip this wire
            }

            // GPU buffer / mesh / compute wire classification (enum-backed, exact match required)
            if (!from_ns.missing_operator && !to_ns.missing_operator) {
                if (from_port_type == VIVID_PORT_GPU_BUFFER && to_port_type == VIVID_PORT_GPU_BUFFER) {
                    w.is_buffer_wire = true;
                } else if (from_port_type == VIVID_PORT_GPU_MESH && to_port_type == VIVID_PORT_GPU_MESH) {
                    w.is_mesh_wire = true;
                } else if (from_port_type == VIVID_PORT_GPU_COMPUTE && to_port_type == VIVID_PORT_GPU_COMPUTE) {
                    w.is_compute_wire = true;
                } else if (from_port_type == VIVID_PORT_GPU_BUFFER || to_port_type == VIVID_PORT_GPU_BUFFER ||
                           from_port_type == VIVID_PORT_GPU_MESH   || to_port_type == VIVID_PORT_GPU_MESH   ||
                           from_port_type == VIVID_PORT_GPU_COMPUTE || to_port_type == VIVID_PORT_GPU_COMPUTE) {
                    std::fprintf(stderr, "[vivid] Scheduler: GPU type mismatch on wire %s/%s -> %s/%s\n",
                        conn.from_node.c_str(), conn.from_port.c_str(),
                        conn.to_node.c_str(), conn.to_port.c_str());
                    continue;  // skip this wire
                }
            }
        } else {
            // Check if target is a file/text param first
            auto fp_it2 = to_ns.file_param_indices.find(conn.to_port);
            if (fp_it2 != to_ns.file_param_indices.end()) {
                // String param target — validate source is string-compatible
                if (from_port_type != VIVID_PORT_CONTROL_STRING) {
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
                    std::fprintf(stderr, "[vivid] Scheduler: node '%s' has no input port or parameter '%s'\n",
                        conn.to_node.c_str(), conn.to_port.c_str());
                    return false;
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

    return true;
}

void Scheduler::tick(double time, double delta_time, uint64_t frame, void* gpu_state,
                     PostNodeFn on_gpu_node, const VividInputState* input) {
    // Reset per-tick flags
    for (auto& ns : nodes_) ns.processed_this_tick = false;

    for (uint32_t ni = 0; ni < static_cast<uint32_t>(nodes_.size()); ++ni) {
        auto& ns = nodes_[ni];

        // Skip audio-domain nodes — they run on the audio thread
        if (ns.is_audio) continue;

        // Skip errored nodes — zero outputs and move on
        if (ns.errored) {
            std::fill(ns.output_values.begin(), ns.output_values.end(), 0.0f);
            for (auto& sp : ns.output_spreads) sp.clear();
            for (auto& sp : ns.output_string_spreads) sp.clear();
            std::fill(ns.output_string_values.begin(), ns.output_string_values.end(), "");
            continue;
        }

        // Zero input values and clear input spreads (unwired ports default to 0.0)
        std::fill(ns.input_values.begin(), ns.input_values.end(), 0.0f);
        std::fill(ns.input_string_values.begin(), ns.input_string_values.end(), "");
        for (auto& sp : ns.input_spreads) sp.clear();
        for (auto& sp : ns.input_string_spreads) sp.clear();

        // Copy upstream outputs into this node's inputs / params
        // (skip texture-type wires — those are resolved separately)
        for (const auto& w : wires_) {
            if (w.to_node_idx == ni) {
                if (w.is_texture_wire || w.is_data_wire) continue;
                if (w.targets_file_param) {
                    // String wire into a file/text param
                    const std::string& src = w.sources_file_param
                        ? nodes_[w.from_node_idx].file_param_storage[w.from_file_param_idx]
                        : nodes_[w.from_node_idx].output_string_values[w.from_port_idx];
                    ns.file_param_storage[w.to_file_param_idx] = src;
                    ns.file_param_ptrs[w.to_file_param_idx] = ns.file_param_storage[w.to_file_param_idx].c_str();
                    continue;
                }
                if (w.is_string_wire) {
                    ns.input_string_values[w.to_port_idx] =
                        nodes_[w.from_node_idx].output_string_values[w.from_port_idx];
                    continue;
                }
                if (w.is_string_spread_wire) {
                    ns.input_string_spreads[w.to_port_idx] =
                        nodes_[w.from_node_idx].output_string_spreads[w.from_port_idx];
                    continue;
                }
                float raw;
                if (w.sources_param)
                    raw = nodes_[w.from_node_idx].param_values[w.from_port_idx];
                else
                    raw = nodes_[w.from_node_idx].output_values[w.from_port_idx];
                float val = has_remap(w) ? apply_remap(raw, w) : raw;
                if (w.targets_param) {
                    if (!(ns.param_lock_flags[w.to_port_idx] & PARAM_LOCK_WIRES))
                        ns.param_values[w.to_port_idx] = val;
                } else {
                    ns.input_values[w.to_port_idx] = val;
                    // Spread propagation with broadcast/wrap semantics
                    // (params have no spreads)
                    if (w.sources_param) continue;
                    const auto& src_spread = nodes_[w.from_node_idx].output_spreads[w.from_port_idx];
                    if (!src_spread.empty()) {
                        auto& dst_spread = ns.input_spreads[w.to_port_idx];
                        size_t src_len = std::min(src_spread.size(), (size_t)kMaxSpreadCapacity);
                        bool remap = has_remap(w);
                        if (dst_spread.empty()) {
                            // First wire into this port: direct copy
                            dst_spread.resize(src_len);
                            for (size_t si = 0; si < src_len; ++si)
                                dst_spread[si] = remap ? apply_remap(src_spread[si], w) : src_spread[si];
                        } else {
                            // Multiple wires: broadcast to longer length, wrap both
                            size_t old_len = dst_spread.size();
                            size_t new_len = std::max(old_len, src_len);
                            new_len = std::min(new_len, (size_t)kMaxSpreadCapacity);
                            if (new_len > old_len) {
                                // Extend existing spread with wrapping
                                dst_spread.resize(new_len);
                                for (size_t si = old_len; si < new_len; ++si)
                                    dst_spread[si] = dst_spread[si % old_len];
                            }
                            for (size_t si = 0; si < new_len; ++si)
                                dst_spread[si] += remap ? apply_remap(src_spread[si % src_len], w) : src_spread[si % src_len];
                        }
                        ns.input_values[w.to_port_idx] = dst_spread[0];
                    }
                }
            }
        }

        // Generation-based skip: if not time-dependent, only process when
        // an upstream node was processed this tick or our own generation changed
        // (e.g. params were set externally via set_param).
        if (!ns.time_dependent && !ns.upstream_nodes.empty()) {
            bool should_process = false;
            for (size_t i = 0; i < ns.upstream_nodes.size(); ++i) {
                if (nodes_[ns.upstream_nodes[i]].processed_this_tick) {
                    should_process = true;
                    break;
                }
            }
            if (!should_process && ns.generation != ns.last_processed_gen)
                should_process = true;
            if (!should_process) continue;
        }

        // Set up spread port arrays using pre-allocated buffers
        for (uint32_t p = 0; p < ns.input_port_count; ++p) {
            auto& isp = ns.input_spreads[p];
            ns.c_in_spreads[p].data     = isp.empty() ? nullptr : isp.data();
            ns.c_in_spreads[p].length   = static_cast<uint32_t>(isp.size());
            ns.c_in_spreads[p].capacity = static_cast<uint32_t>(isp.size());

            auto& ssp = ns.input_string_spreads[p];
            auto& ptrs = ns.in_string_spread_ptrs[p];
            uint32_t slen = static_cast<uint32_t>(
                std::min(ssp.size(), static_cast<size_t>(kMaxSpreadCapacity)));
            for (uint32_t si = 0; si < slen; ++si) ptrs[si] = ssp[si].c_str();
            ns.c_in_string_spreads[p].data = slen > 0 ? ptrs.data() : nullptr;
            ns.c_in_string_spreads[p].length = slen;
            ns.c_in_string_spreads[p].capacity = slen;
            ns.c_input_string_values[p] = ns.input_string_values[p].c_str();
        }

        for (uint32_t p = 0; p < ns.output_port_count; ++p) {
            ns.c_out_spreads[p].data     = ns.out_spread_buf[p].data();
            ns.c_out_spreads[p].length   = 0;
            ns.c_out_spreads[p].capacity = kMaxSpreadCapacity;
            ns.c_out_string_spreads[p].data = ns.out_string_spread_ptr_buf[p].data();
            ns.c_out_string_spreads[p].length = 0;
            ns.c_out_string_spreads[p].capacity = kMaxSpreadCapacity;
            ns.c_output_string_values[p] = ns.output_string_values[p].c_str();
        }

        // Build process context and tick
        VividProcessContext ctx{};
        ctx.time          = time;
        ctx.delta_time    = delta_time;
        ctx.frame         = frame;
        ctx.param_values  = ns.param_values.data();
        ctx.input_values  = ns.input_values.data();
        ctx.output_values = ns.output_values.data();
        ctx.input_spreads  = ns.c_in_spreads.data();
        ctx.output_spreads = ns.c_out_spreads.data();
        ctx.input_data = nullptr;
        ctx.input_data_count = 0;
        // Clear and expose the DATA output buffer (operator writes into it)
        std::fill(ns.output_data_buf.begin(), ns.output_data_buf.end(), nullptr);
        ctx.output_data = ns.output_data_buf.empty() ? nullptr : ns.output_data_buf.data();
        ctx.output_data_count = static_cast<uint32_t>(ns.output_data_buf.size());
        ctx.input_string_values = ns.c_input_string_values.empty()
                                    ? nullptr : ns.c_input_string_values.data();
        ctx.output_string_values = ns.c_output_string_values.empty()
                                    ? nullptr : ns.c_output_string_values.data();
        ctx.input_string_spreads = ns.c_in_string_spreads.empty()
                                    ? nullptr : ns.c_in_string_spreads.data();
        ctx.output_string_spreads = ns.c_out_string_spreads.empty()
                                    ? nullptr : ns.c_out_string_spreads.data();
        ctx.file_param_values = ns.file_param_ptrs.empty()
                                    ? nullptr : ns.file_param_ptrs.data();
        ctx.file_param_count  = static_cast<uint32_t>(ns.file_param_ptrs.size());
        ctx.shared_handles = vivid::shared_handle_service();

        // GPU state: build per-node VividGpuState with this node's texture
        VividGpuState per_node_gpu{};
        if (ns.is_gpu && gpu_state) {
            auto* base_gpu = static_cast<VividGpuState*>(gpu_state);
            per_node_gpu.device          = base_gpu->device;
            per_node_gpu.queue           = base_gpu->queue;
            per_node_gpu.command_encoder = base_gpu->command_encoder;
            per_node_gpu.output_format   = base_gpu->output_format;
            per_node_gpu.output_texture      = ns.gpu_texture;
            per_node_gpu.output_texture_view = ns.gpu_texture_view;
            per_node_gpu.output_width    = ns.gpu_tex_width;
            per_node_gpu.output_height   = ns.gpu_tex_height;

            // Resolve texture inputs from upstream nodes
            size_t tex_count = ns.texture_input_port_indices.size();
            ns.resolved_tex_inputs.clear();
            ns.resolved_tex_inputs.resize(tex_count, nullptr);
            ns.resolved_tex_raw.clear();
            ns.resolved_tex_raw.resize(tex_count, nullptr);
            ns.resolved_tex_widths.clear();
            ns.resolved_tex_widths.resize(tex_count, 0);
            ns.resolved_tex_heights.clear();
            ns.resolved_tex_heights.resize(tex_count, 0);
            for (size_t ti = 0; ti < tex_count; ++ti) {
                uint32_t port_idx = ns.texture_input_port_indices[ti];
                for (const auto& w : wires_) {
                    if (w.to_node_idx == ni && !w.targets_param &&
                        w.to_port_idx == port_idx && w.is_texture_wire) {
                        const auto& upstream = nodes_[w.from_node_idx];
                        bool routed_aux = false;
                        for (size_t ai = 0; ai < upstream.aux_texture_output_port_indices.size(); ++ai) {
                            if (w.from_port_idx ==
                                    static_cast<uint32_t>(upstream.aux_texture_output_port_indices[ai])) {
                                ns.resolved_tex_inputs[ti] = upstream.aux_gpu_texture_views[ai];
                                ns.resolved_tex_raw[ti]    = upstream.aux_gpu_textures[ai];
                                routed_aux = true;
                                break;
                            }
                        }
                        if (!routed_aux) {
                            ns.resolved_tex_inputs[ti] = upstream.gpu_texture_view;
                            ns.resolved_tex_raw[ti]    = upstream.gpu_texture;
                        }
                        ns.resolved_tex_widths[ti]  = upstream.gpu_tex_width;
                        ns.resolved_tex_heights[ti] = upstream.gpu_tex_height;
                        break;
                    }
                }
            }
            per_node_gpu.input_texture_views = ns.resolved_tex_inputs.empty()
                                                ? nullptr : ns.resolved_tex_inputs.data();
            per_node_gpu.input_texture_count = static_cast<uint32_t>(tex_count);
            per_node_gpu.input_textures        = ns.resolved_tex_raw.empty()
                                                    ? nullptr : ns.resolved_tex_raw.data();
            per_node_gpu.input_texture_widths   = ns.resolved_tex_widths.empty()
                                                    ? nullptr : ns.resolved_tex_widths.data();
            per_node_gpu.input_texture_heights  = ns.resolved_tex_heights.empty()
                                                    ? nullptr : ns.resolved_tex_heights.data();
            per_node_gpu.operators_src_dir = operators_src_dir_.empty()
                                                ? nullptr : operators_src_dir_.c_str();
            per_node_gpu.aux_output_texture_views = ns.aux_gpu_texture_views.empty()
                ? nullptr : ns.aux_gpu_texture_views.data();
            per_node_gpu.aux_output_texture_count =
                static_cast<uint32_t>(ns.aux_gpu_texture_views.size());

            // Resolve data inputs from upstream nodes
            size_t data_count = ns.data_input_port_indices.size();
            ns.resolved_data_inputs.clear();
            ns.resolved_data_inputs.resize(data_count, nullptr);
            for (size_t di = 0; di < data_count; ++di) {
                uint32_t port_idx = ns.data_input_port_indices[di];
                for (const auto& w : wires_) {
                    if (w.to_node_idx == ni && !w.targets_param &&
                        w.to_port_idx == port_idx && w.is_data_wire) {
                        const auto& upstream = nodes_[w.from_node_idx];
                        // Find which DATA output slot corresponds to this wire's source port
                        void* resolved = nullptr;
                        for (uint32_t s = 0; s < upstream.data_output_port_indices.size(); ++s) {
                            if (upstream.data_output_port_indices[s] == w.from_port_idx) {
                                if (s < upstream.gpu_data_outputs.size())
                                    resolved = upstream.gpu_data_outputs[s];
                                break;
                            }
                        }
                        ns.resolved_data_inputs[di] = resolved;
                        break;
                    }
                }
            }
            per_node_gpu.input_data = ns.resolved_data_inputs.empty()
                                          ? nullptr : ns.resolved_data_inputs.data();
            per_node_gpu.input_data_count = static_cast<uint32_t>(data_count);
            per_node_gpu.output_data = ctx.output_data;
            per_node_gpu.output_data_count = ctx.output_data_count;
            ctx.input_data = per_node_gpu.input_data;
            ctx.input_data_count = per_node_gpu.input_data_count;

            // Resolve GPU buffer inputs from upstream nodes
            for (size_t bi = 0; bi < ns.buffer_input_port_indices.size(); ++bi) {
                uint32_t port_idx = ns.buffer_input_port_indices[bi];
                ns.resolved_buffer_inputs[bi] = nullptr;
                for (const auto& w : wires_) {
                    if (!w.is_buffer_wire || w.to_node_idx != ni || w.targets_param) continue;
                    if (w.to_port_idx != port_idx) continue;
                    const auto& from_ns = nodes_[w.from_node_idx];
                    for (size_t si = 0; si < from_ns.buffer_output_port_indices.size(); ++si) {
                        if (from_ns.buffer_output_port_indices[si] == w.from_port_idx) {
                            ns.resolved_buffer_inputs[bi] = from_ns.gpu_buffer_outputs[si];
                            break;
                        }
                    }
                    break;
                }
            }
            per_node_gpu.output_buffers      = ns.output_buffer_buf.empty() ? nullptr : ns.output_buffer_buf.data();
            per_node_gpu.output_buffer_count = static_cast<uint32_t>(ns.output_buffer_buf.size());
            per_node_gpu.input_buffers       = ns.resolved_buffer_inputs.empty() ? nullptr : ns.resolved_buffer_inputs.data();
            per_node_gpu.input_buffer_count  = static_cast<uint32_t>(ns.resolved_buffer_inputs.size());

            // Resolve GPU mesh inputs from upstream nodes
            for (size_t mi = 0; mi < ns.mesh_input_port_indices.size(); ++mi) {
                uint32_t port_idx = ns.mesh_input_port_indices[mi];
                ns.resolved_mesh_inputs[mi] = nullptr;
                for (const auto& w : wires_) {
                    if (!w.is_mesh_wire || w.to_node_idx != ni || w.targets_param) continue;
                    if (w.to_port_idx != port_idx) continue;
                    const auto& from_ns = nodes_[w.from_node_idx];
                    for (size_t si = 0; si < from_ns.mesh_output_port_indices.size(); ++si) {
                        if (from_ns.mesh_output_port_indices[si] == w.from_port_idx) {
                            ns.resolved_mesh_inputs[mi] = from_ns.gpu_mesh_outputs[si];
                            break;
                        }
                    }
                    break;
                }
            }
            per_node_gpu.output_meshes     = ns.output_mesh_buf.empty() ? nullptr : ns.output_mesh_buf.data();
            per_node_gpu.output_mesh_count = static_cast<uint32_t>(ns.output_mesh_buf.size());
            per_node_gpu.input_meshes      = ns.resolved_mesh_inputs.empty() ? nullptr : ns.resolved_mesh_inputs.data();
            per_node_gpu.input_mesh_count  = static_cast<uint32_t>(ns.resolved_mesh_inputs.size());

            // Resolve GPU compute inputs from upstream nodes
            for (size_t ci = 0; ci < ns.compute_input_port_indices.size(); ++ci) {
                uint32_t port_idx = ns.compute_input_port_indices[ci];
                ns.resolved_compute_inputs[ci] = nullptr;
                for (const auto& w : wires_) {
                    if (!w.is_compute_wire || w.to_node_idx != ni || w.targets_param) continue;
                    if (w.to_port_idx != port_idx) continue;
                    const auto& from_ns = nodes_[w.from_node_idx];
                    for (size_t si = 0; si < from_ns.compute_output_port_indices.size(); ++si) {
                        if (from_ns.compute_output_port_indices[si] == w.from_port_idx) {
                            ns.resolved_compute_inputs[ci] = from_ns.gpu_compute_outputs[si];
                            break;
                        }
                    }
                    break;
                }
            }
            per_node_gpu.output_compute       = ns.output_compute_buf.empty() ? nullptr : ns.output_compute_buf.data();
            per_node_gpu.output_compute_count = static_cast<uint32_t>(ns.output_compute_buf.size());
            per_node_gpu.input_compute        = ns.resolved_compute_inputs.empty() ? nullptr : ns.resolved_compute_inputs.data();
            per_node_gpu.input_compute_count  = static_cast<uint32_t>(ns.resolved_compute_inputs.size());

            ctx.gpu = &per_node_gpu;
        } else {
            ctx.gpu = nullptr;
        }

        // Forward input state to all operators (they ignore it if they don't care)
        ctx.input = input ? const_cast<void*>(static_cast<const void*>(input)) : nullptr;
        const auto prev_output_spreads = ns.output_spreads;
        const auto prev_output_strings = ns.output_string_values;
        const auto prev_output_string_spreads = ns.output_string_spreads;

        try {
            if (ns.missing_operator || !ns.loader) {
                std::fill(ns.output_values.begin(), ns.output_values.end(), 0.0f);
                for (auto& sp : ns.output_spreads) sp.clear();
                for (auto& sp : ns.output_string_spreads) sp.clear();
                std::fill(ns.output_string_values.begin(), ns.output_string_values.end(), "");
            } else {
                CrashGuard guard(ns.node_id.c_str());
                ns.loader->process(ns.instance, &ctx);
            }
        } catch (const std::exception& e) {
            ns.errored = true;
            ns.error_message = e.what();
            std::fill(ns.output_values.begin(), ns.output_values.end(), 0.0f);
            for (auto& sp : ns.output_spreads) sp.clear();
            for (auto& sp : ns.output_string_spreads) sp.clear();
            std::fill(ns.output_string_values.begin(), ns.output_string_values.end(), "");
            std::fprintf(stderr, "[vivid] operator '%s' threw: %s\n",
                         ns.node_id.c_str(), e.what());
        } catch (...) {
            ns.errored = true;
            ns.error_message = "Unknown exception";
            std::fill(ns.output_values.begin(), ns.output_values.end(), 0.0f);
            for (auto& sp : ns.output_spreads) sp.clear();
            for (auto& sp : ns.output_string_spreads) sp.clear();
            std::fill(ns.output_string_values.begin(), ns.output_string_values.end(), "");
            std::fprintf(stderr, "[vivid] operator '%s' threw unknown exception\n",
                         ns.node_id.c_str());
        }

        // Capture opaque data outputs.
        // Both ctx.output_data and per_node_gpu.output_data point to output_data_buf.data(),
        // so the operator's writes are already in ns.output_data_buf — just copy.
        ns.gpu_data_outputs = ns.output_data_buf;

        // Capture GPU buffer/mesh/compute outputs (operator wrote pointers into the buf arrays)
        ns.gpu_buffer_outputs  = ns.output_buffer_buf;
        ns.gpu_mesh_outputs    = ns.output_mesh_buf;
        ns.gpu_compute_outputs = ns.output_compute_buf;

        // Check if the operator requested a texture resize
        if (ctx.preferred_tex_width > 0 && ctx.preferred_tex_height > 0 &&
            (ctx.preferred_tex_width != ns.gpu_tex_width ||
             ctx.preferred_tex_height != ns.gpu_tex_height)) {
            ns.gpu_tex_width  = ctx.preferred_tex_width;
            ns.gpu_tex_height = ctx.preferred_tex_height;
            ns.generation++;
            needs_gpu_realloc_ = true;
        }

        // Read back output spreads
        for (uint32_t p = 0; p < ns.output_port_count; ++p) {
            if (ns.c_out_spreads[p].length > 0) {
                ns.output_spreads[p].assign(
                    ns.c_out_spreads[p].data,
                    ns.c_out_spreads[p].data + ns.c_out_spreads[p].length);
            } else {
                ns.output_spreads[p].clear();
            }

            if (ns.c_output_string_values[p]) {
                ns.output_string_values[p] = ns.c_output_string_values[p];
            } else {
                ns.output_string_values[p].clear();
            }

            auto& out_ss = ns.output_string_spreads[p];
            out_ss.clear();
            uint32_t s_len = std::min(ns.c_out_string_spreads[p].length, kMaxSpreadCapacity);
            if (s_len > 0 && ns.c_out_string_spreads[p].data) {
                out_ss.reserve(s_len);
                for (uint32_t si = 0; si < s_len; ++si) {
                    const char* sv = ns.c_out_string_spreads[p].data[si];
                    out_ss.emplace_back(sv ? sv : "");
                }
            }
        }

        // Invoke post-GPU-node callback (for thumbnail capture)
        if (ns.is_gpu && on_gpu_node) {
            on_gpu_node(ni, ns.node_id, ns.gpu_texture_view);
        }

        // Update generation: bump if outputs changed, spreads changed, or GPU node
        bool outputs_changed = ns.is_gpu;
        if (!outputs_changed) {
            for (size_t i = 0; i < ns.output_values.size(); ++i) {
                if (ns.output_values[i] != ns.prev_output_values[i]) {
                    outputs_changed = true;
                    break;
                }
            }
        }
        if (!outputs_changed) {
            for (uint32_t p = 0; p < ns.output_port_count; ++p) {
                if (p < prev_output_spreads.size() &&
                    ns.output_spreads[p] != prev_output_spreads[p]) {
                    outputs_changed = true;
                    break;
                }
                if (p < prev_output_strings.size() &&
                    ns.output_string_values[p] != prev_output_strings[p]) {
                    outputs_changed = true;
                    break;
                }
                if (p < prev_output_string_spreads.size() &&
                    ns.output_string_spreads[p] != prev_output_string_spreads[p]) {
                    outputs_changed = true;
                    break;
                }
            }
        }

        if (outputs_changed) {
            ns.generation++;
            ns.prev_output_values = ns.output_values;
        }

        ns.last_processed_gen = ns.generation;
        ns.processed_this_tick = true;
    }

    // Propagate control→audio param wires for inspector display and push_params() staging.
    // Audio nodes are skipped in the main loop (they run on the audio thread), but their
    // scheduler-side param_values must reflect modulation so:
    //   (a) the inspector shows animated values, and
    //   (b) push_params() can read the current modulated value and write it to the
    //       lock-free audio snapshot.
    //
    // THREADING: tick() and push_params() both run on the main thread, sequentially in the
    // same frame (tick → push_params). The audio callback never reads scheduler NodeState;
    // it reads only from the double-buffered AudioEngine snapshot. No cross-thread data race.
    for (const auto& w : wires_) {
        if (!w.targets_param) continue;
        auto& to_ns = nodes_[w.to_node_idx];
        if (!to_ns.is_audio) continue;
        if (w.targets_file_param) {
            const std::string& src = w.sources_file_param
                ? nodes_[w.from_node_idx].file_param_storage[w.from_file_param_idx]
                : nodes_[w.from_node_idx].output_string_values[w.from_port_idx];
            to_ns.file_param_storage[w.to_file_param_idx] = src;
            to_ns.file_param_ptrs[w.to_file_param_idx] = to_ns.file_param_storage[w.to_file_param_idx].c_str();
            continue;
        }
        float raw = w.sources_param
            ? nodes_[w.from_node_idx].param_values[w.from_port_idx]
            : nodes_[w.from_node_idx].output_values[w.from_port_idx];
        float val = has_remap(w) ? apply_remap(raw, w) : raw;
        if (!(to_ns.param_lock_flags[w.to_port_idx] & PARAM_LOCK_WIRES))
            to_ns.param_values[w.to_port_idx] = val;
    }
}

bool Scheduler::has_gpu_operators() const {
    for (const auto& ns : nodes_) {
        if (!ns.loader) continue;
        const VividOperatorDescriptor* desc = ns.loader->descriptor();
        if (!desc) continue;
        if (desc->domain == VIVID_DOMAIN_GPU)
            return true;
    }
    return false;
}

bool Scheduler::has_audio_operators() const {
    for (const auto& ns : nodes_) {
        if (!ns.loader) continue;
        const VividOperatorDescriptor* desc = ns.loader->descriptor();
        if (!desc) continue;
        if (desc->domain == VIVID_DOMAIN_AUDIO)
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
            desc->domain == VIVID_DOMAIN_AUDIO) {
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
}

void Scheduler::inject_external_spread(uint32_t node_idx, uint32_t port_idx,
                                       const float* data, uint32_t length) {
    if (node_idx >= nodes_.size()) return;
    auto& ns = nodes_[node_idx];
    if (port_idx >= ns.output_spreads.size()) return;
    if (length > kMaxSpreadCapacity) length = kMaxSpreadCapacity;
    ns.output_spreads[port_idx].assign(data, data + length);
    ns.generation++;
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

    // 2. Destroy old instances (using old dylib's vivid_destroy)
    for (const auto& sp : saved) {
        auto& ns = nodes_[sp.node_idx];
        if (ns.instance) {
            ns.loader->destroy_instance(ns.instance);
            ns.instance = nullptr;
        }
    }

    // 3. Reload the dylib (dlclose old, dlopen new)
    if (!registry.reload_operator(type_name, new_dylib_path)) {
        std::fprintf(stderr, "[vivid] Scheduler: dylib reload failed for '%s'\n", type_name.c_str());
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

    return true;
}

void Scheduler::allocate_gpu_textures(WGPUDevice device, uint32_t default_w, uint32_t default_h,
                                      WGPUTextureFormat format,
                                      WGPUTextureUsage extra_usage) {
    // Iterate nodes in topological order (they're already sorted)
    for (uint32_t ni = 0; ni < static_cast<uint32_t>(nodes_.size()); ++ni) {
        auto& ns = nodes_[ni];
        if (!ns.is_gpu) continue;

        // Release existing textures (primary and aux)
        if (ns.gpu_texture_view) { wgpuTextureViewRelease(ns.gpu_texture_view); ns.gpu_texture_view = nullptr; }
        if (ns.gpu_texture) { wgpuTextureRelease(ns.gpu_texture); ns.gpu_texture = nullptr; }
        for (auto& v : ns.aux_gpu_texture_views) { if (v) { wgpuTextureViewRelease(v); v = nullptr; } }
        for (auto& t : ns.aux_gpu_textures)      { if (t) { wgpuTextureRelease(t); t = nullptr; } }

        // GPU sinks and scene-only nodes don't produce their own textures
        if (ns.is_gpu_sink || !ns.has_texture_output) {
            ns.gpu_tex_width  = 0;
            ns.gpu_tex_height = 0;
            continue;
        }

        // Resolve texture size
        uint32_t w = ns.gpu_tex_width;
        uint32_t h = ns.gpu_tex_height;

        if (w == 0 || h == 0) {
            // Try to inherit from first connected upstream GPU node
            bool inherited = false;
            if (!ns.texture_input_port_indices.empty()) {
                uint32_t first_tex_port = ns.texture_input_port_indices[0];
                for (const auto& wire : wires_) {
                    if (wire.to_node_idx == ni && !wire.targets_param &&
                        wire.to_port_idx == first_tex_port && wire.is_texture_wire) {
                        const auto& upstream = nodes_[wire.from_node_idx];
                        if (upstream.gpu_tex_width > 0 && upstream.gpu_tex_height > 0) {
                            w = upstream.gpu_tex_width;
                            h = upstream.gpu_tex_height;
                            inherited = true;
                        }
                        break;
                    }
                }
            }
            if (!inherited) {
                w = default_w;
                h = default_h;
            }
            ns.gpu_tex_width  = w;
            ns.gpu_tex_height = h;
        }

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

        // Allocate aux textures (same size and format as primary)
        for (size_t ai = 0; ai < ns.aux_texture_output_port_indices.size(); ++ai) {
            std::string aux_label = "Node Aux Texture [" + ns.node_id + "/" + std::to_string(ai) + "]";
            WGPUTextureDescriptor aux_desc = tex_desc;
            aux_desc.label = to_sv(aux_label.c_str());
            ns.aux_gpu_textures[ai] = wgpuDeviceCreateTexture(device, &aux_desc);
            if (!ns.aux_gpu_textures[ai]) {
                std::fprintf(stderr, "[vivid] GPU aux texture alloc failed for node '%s' slot %zu\n",
                             ns.node_id.c_str(), ai);
                continue;
            }

            std::string aux_view_label = "Node Aux View [" + ns.node_id + "/" + std::to_string(ai) + "]";
            WGPUTextureViewDescriptor aux_view_desc = view_desc;
            aux_view_desc.label = to_sv(aux_view_label.c_str());
            ns.aux_gpu_texture_views[ai] = wgpuTextureCreateView(ns.aux_gpu_textures[ai], &aux_view_desc);
            if (!ns.aux_gpu_texture_views[ai]) {
                std::fprintf(stderr, "[vivid] GPU aux texture view creation failed for node '%s' slot %zu\n",
                             ns.node_id.c_str(), ai);
                wgpuTextureRelease(ns.aux_gpu_textures[ai]);
                ns.aux_gpu_textures[ai] = nullptr;
            }
        }

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

void Scheduler::shutdown() {
    for (auto& ns : nodes_) {
        // Release per-node GPU textures (primary and aux)
        if (ns.gpu_texture_view) { wgpuTextureViewRelease(ns.gpu_texture_view); ns.gpu_texture_view = nullptr; }
        if (ns.gpu_texture) { wgpuTextureRelease(ns.gpu_texture); ns.gpu_texture = nullptr; }
        for (auto& v : ns.aux_gpu_texture_views) { if (v) { wgpuTextureViewRelease(v); v = nullptr; } }
        for (auto& t : ns.aux_gpu_textures)      { if (t) { wgpuTextureRelease(t); t = nullptr; } }

        if (ns.instance) {
            if (ns.loader)
                ns.loader->destroy_instance(ns.instance);
            ns.instance = nullptr;
        }
    }
    nodes_.clear();
    wires_.clear();
}

} // namespace vivid
