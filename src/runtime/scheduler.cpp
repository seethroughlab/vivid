#include "runtime/scheduler.h"
#include "common/gpu_util.h"
#include "common/topo_sort.h"
#include "operator_api/gpu_operator.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vivid {

static constexpr uint32_t kMaxSpreadCapacity = 1024;

void Scheduler::init_node_state(NodeState& ns, const VividOperatorDescriptor* desc,
                                const std::unordered_map<std::string, float>* param_overrides,
                                const std::unordered_map<std::string, std::string>* string_overrides) {
    // Count and index ports by direction
    ns.input_port_count = 0;
    ns.output_port_count = 0;
    ns.input_port_indices.clear();
    ns.output_port_indices.clear();
    ns.param_indices.clear();

    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_INPUT) {
            ns.input_port_indices[desc->ports[i].name] = ns.input_port_count++;
        } else {
            ns.output_port_indices[desc->ports[i].name] = ns.output_port_count++;
        }
    }

    ns.input_values.assign(ns.input_port_count, 0.0f);
    ns.output_values.assign(ns.output_port_count, 0.0f);
    ns.input_spreads.resize(ns.input_port_count);
    ns.output_spreads.resize(ns.output_port_count);

    // Init param_values from descriptor defaults, then apply overrides
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

    // Init file params from descriptor
    ns.file_param_storage.clear();
    ns.file_param_ptrs.clear();
    ns.file_param_indices.clear();
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        if (desc->params[i].type == VIVID_PARAM_FILE) {
            uint32_t fidx = static_cast<uint32_t>(ns.file_param_storage.size());
            ns.file_param_indices[desc->params[i].name] = fidx;
            const char* def = desc->params[i].default_string;
            ns.file_param_storage.push_back(def ? def : "");
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
    ns.file_param_ptrs.resize(ns.file_param_storage.size());
    for (size_t i = 0; i < ns.file_param_storage.size(); ++i) {
        ns.file_param_ptrs[i] = ns.file_param_storage[i].c_str();
    }

    // Identify GPU_TEXTURE input ports and detect GPU sinks
    ns.texture_input_port_indices.clear();
    ns.is_gpu_sink = false;
    if (ns.is_gpu) {
        uint32_t input_idx = 0;
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            if (desc->ports[i].direction == VIVID_PORT_INPUT) {
                if (desc->ports[i].type == VIVID_PORT_GPU_TEXTURE) {
                    ns.texture_input_port_indices.push_back(input_idx);
                }
                input_idx++;
            }
        }
        bool has_tex_output = false;
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            if (desc->ports[i].direction == VIVID_PORT_OUTPUT &&
                desc->ports[i].type == VIVID_PORT_GPU_TEXTURE) {
                has_tex_output = true;
                break;
            }
        }
        ns.is_gpu_sink = !ns.texture_input_port_indices.empty() && !has_tex_output;
    }
}

bool Scheduler::build(const Graph& graph, OperatorRegistry& registry) {
    nodes_.clear();
    wires_.clear();

    // Map node id -> index for lookup
    std::unordered_map<std::string, uint32_t> node_index;

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

        if (!loader) {
            std::fprintf(stderr, "[vivid] Scheduler: unknown operator type '%s'\n", ndef.type.c_str());
            return false;
        }

        const VividOperatorDescriptor* desc = loader->descriptor();

        NodeState ns;
        ns.node_id = ndef.id;
        ns.loader = loader;
        ns.owned_loader = std::move(owned);
        ns.instance = loader->create_instance();
        ns.generation = 0;
        init_node_state(ns, desc, &ndef.params,
                        ndef.string_params.empty() ? nullptr : &ndef.string_params);

        // Per-node GPU texture resolution from graph definition
        if (ns.is_gpu) {
            ns.gpu_tex_width  = ndef.tex_width;
            ns.gpu_tex_height = ndef.tex_height;
        }

        node_index[ndef.id] = static_cast<uint32_t>(nodes_.size());
        nodes_.push_back(std::move(ns));
    }

    // 2. Resolve connections -> Wires
    // Also build adjacency for topological sort
    uint32_t n = static_cast<uint32_t>(nodes_.size());
    std::vector<std::vector<uint32_t>> adj(n);     // adj[from] = list of to
    std::vector<uint32_t> in_degree(n, 0);

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
            from_port_type = VIVID_PORT_CONTROL_FLOAT;  // all params are float
        }

        Wire w;
        w.from_node_idx = fi;
        w.from_port_idx = from_port_idx;
        w.sources_param = source_is_param;
        w.to_node_idx   = ti;

        auto tp_it = to_ns.input_port_indices.find(conn.to_port);
        if (tp_it != to_ns.input_port_indices.end()) {
            w.to_port_idx = tp_it->second;
            w.targets_param = false;

            // Determine destination port type
            VividPortType to_port_type = VIVID_PORT_CONTROL_FLOAT;
            const auto* to_op_desc = to_ns.loader->descriptor();
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

            // Validate texture wire: both ends must be GPU_TEXTURE
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
        w.scale = conn.scale;
        wires_.push_back(w);

        adj[fi].push_back(ti);
        in_degree[ti]++;
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
    for (auto& ns : nodes_)
        ns.upstream_gens_cached.resize(ns.upstream_nodes.size(), 0);

    // Print evaluation order
    std::fprintf(stderr, "[vivid] Evaluation order:");
    for (uint32_t i = 0; i < n; ++i) {
        std::fprintf(stderr, "%s%s", (i == 0 ? " " : " → "), nodes_[i].node_id.c_str());
    }
    std::fprintf(stderr, "\n");

    return true;
}

void Scheduler::tick(double time, double delta_time, uint64_t frame, void* gpu_state,
                     PostNodeFn on_gpu_node) {
    for (uint32_t ni = 0; ni < static_cast<uint32_t>(nodes_.size()); ++ni) {
        auto& ns = nodes_[ni];

        // Skip audio-domain nodes — they run on the audio thread
        if (ns.is_audio) continue;

        // Skip errored nodes — zero outputs and move on
        if (ns.errored) {
            std::fill(ns.output_values.begin(), ns.output_values.end(), 0.0f);
            for (auto& sp : ns.output_spreads) sp.clear();
            continue;
        }

        // Zero input values and clear input spreads (unwired ports default to 0.0)
        std::fill(ns.input_values.begin(), ns.input_values.end(), 0.0f);
        for (auto& sp : ns.input_spreads) sp.clear();

        // Copy upstream outputs into this node's inputs / params
        // (skip texture-type wires — those are resolved separately)
        for (const auto& w : wires_) {
            if (w.to_node_idx == ni) {
                if (w.is_texture_wire) continue;
                float val;
                if (w.sources_param)
                    val = nodes_[w.from_node_idx].param_values[w.from_port_idx] * w.scale;
                else
                    val = nodes_[w.from_node_idx].output_values[w.from_port_idx] * w.scale;
                if (w.targets_param) {
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
                        if (dst_spread.empty()) {
                            // First wire into this port: direct copy
                            dst_spread.resize(src_len);
                            for (size_t si = 0; si < src_len; ++si)
                                dst_spread[si] = src_spread[si] * w.scale;
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
                                dst_spread[si] += src_spread[si % src_len] * w.scale;
                        }
                        ns.input_values[w.to_port_idx] = dst_spread[0];
                    }
                }
            }
        }

        // Generation-based skip: if not time-dependent and all upstream
        // generations match what we saw last cook, skip processing.
        if (!ns.time_dependent && !ns.upstream_nodes.empty()) {
            bool all_match = true;
            for (size_t i = 0; i < ns.upstream_nodes.size(); ++i) {
                if (nodes_[ns.upstream_nodes[i]].generation != ns.upstream_gens_cached[i]) {
                    all_match = false;
                    break;
                }
            }
            if (all_match) {
                // Outputs unchanged — skip processing
                continue;
            }
        }

        // Build spread port arrays for process context
        std::vector<VividSpreadPort> in_spreads(ns.input_port_count);
        for (uint32_t p = 0; p < ns.input_port_count; ++p) {
            auto& isp = ns.input_spreads[p];
            in_spreads[p].data     = isp.empty() ? nullptr : isp.data();
            in_spreads[p].length   = static_cast<uint32_t>(isp.size());
            in_spreads[p].capacity = static_cast<uint32_t>(isp.size());
        }

        std::vector<VividSpreadPort> out_spreads(ns.output_port_count);
        // Pre-allocate output spread buffers
        std::vector<std::vector<float>> out_spread_storage(ns.output_port_count);
        for (uint32_t p = 0; p < ns.output_port_count; ++p) {
            out_spread_storage[p].resize(kMaxSpreadCapacity, 0.0f);
            out_spreads[p].data     = out_spread_storage[p].data();
            out_spreads[p].length   = 0;
            out_spreads[p].capacity = kMaxSpreadCapacity;
        }

        // Build process context and tick
        VividProcessContext ctx{};
        ctx.time          = time;
        ctx.delta_time    = delta_time;
        ctx.frame         = frame;
        ctx.param_values  = ns.param_values.data();
        ctx.input_values  = ns.input_values.data();
        ctx.output_values = ns.output_values.data();
        ctx.input_spreads  = in_spreads.data();
        ctx.output_spreads = out_spreads.data();
        ctx.file_param_values = ns.file_param_ptrs.empty()
                                    ? nullptr : ns.file_param_ptrs.data();
        ctx.file_param_count  = static_cast<uint32_t>(ns.file_param_ptrs.size());

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
            ns.resolved_tex_inputs.clear();
            ns.resolved_tex_inputs.resize(ns.texture_input_port_indices.size(), nullptr);
            for (size_t ti = 0; ti < ns.texture_input_port_indices.size(); ++ti) {
                uint32_t port_idx = ns.texture_input_port_indices[ti];
                for (const auto& w : wires_) {
                    if (w.to_node_idx == ni && !w.targets_param &&
                        w.to_port_idx == port_idx && w.is_texture_wire) {
                        ns.resolved_tex_inputs[ti] = nodes_[w.from_node_idx].gpu_texture_view;
                        break;
                    }
                }
            }
            per_node_gpu.input_texture_views = ns.resolved_tex_inputs.empty()
                                                ? nullptr : ns.resolved_tex_inputs.data();
            per_node_gpu.input_texture_count = static_cast<uint32_t>(ns.resolved_tex_inputs.size());
            per_node_gpu.operators_src_dir = operators_src_dir_.empty()
                                                ? nullptr : operators_src_dir_.c_str();

            ctx.gpu = &per_node_gpu;
        } else {
            ctx.gpu = nullptr;
        }

        try {
            ns.loader->process(ns.instance, &ctx);
        } catch (const std::exception& e) {
            ns.errored = true;
            ns.error_message = e.what();
            std::fill(ns.output_values.begin(), ns.output_values.end(), 0.0f);
            for (auto& sp : ns.output_spreads) sp.clear();
            std::fprintf(stderr, "[vivid] operator '%s' threw: %s\n",
                         ns.node_id.c_str(), e.what());
        } catch (...) {
            ns.errored = true;
            ns.error_message = "Unknown exception";
            std::fill(ns.output_values.begin(), ns.output_values.end(), 0.0f);
            for (auto& sp : ns.output_spreads) sp.clear();
            std::fprintf(stderr, "[vivid] operator '%s' threw unknown exception\n",
                         ns.node_id.c_str());
        }

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
            if (out_spreads[p].length > 0) {
                ns.output_spreads[p].assign(
                    out_spreads[p].data,
                    out_spreads[p].data + out_spreads[p].length);
            } else {
                ns.output_spreads[p].clear();
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
                if (!ns.output_spreads[p].empty()) {
                    outputs_changed = true;
                    break;
                }
            }
        }

        if (outputs_changed) {
            ns.generation++;
            ns.prev_output_values = ns.output_values;
        }

        // Cache upstream generations for next tick's comparison
        for (size_t i = 0; i < ns.upstream_nodes.size(); ++i) {
            ns.upstream_gens_cached[i] = nodes_[ns.upstream_nodes[i]].generation;
        }
    }

    // Propagate control→audio param wires for inspector display.
    // Audio nodes are skipped in the main loop (they run on the audio thread),
    // but their scheduler-side param_values must reflect modulation so the
    // inspector shows animated values.
    for (const auto& w : wires_) {
        if (!w.targets_param) continue;
        auto& to_ns = nodes_[w.to_node_idx];
        if (!to_ns.is_audio) continue;
        float val = w.sources_param
            ? nodes_[w.from_node_idx].param_values[w.from_port_idx]
            : nodes_[w.from_node_idx].output_values[w.from_port_idx];
        to_ns.param_values[w.to_port_idx] = val;
    }
}

bool Scheduler::has_gpu_operators() const {
    for (const auto& ns : nodes_) {
        const VividOperatorDescriptor* desc = ns.loader->descriptor();
        if (!desc) continue;
        if (desc->domain == VIVID_DOMAIN_GPU)
            return true;
    }
    return false;
}

bool Scheduler::has_audio_operators() const {
    for (const auto& ns : nodes_) {
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
            return nodes_[wire.from_node_idx].gpu_texture;
        }
    }
    return nullptr;
}

bool Scheduler::is_audio_type(const std::string& type_name) const {
    for (const auto& ns : nodes_) {
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
    auto& ns = nodes_[node_idx];
    if (ns.output_values[port_idx] != value) {
        ns.output_values[port_idx] = value;
        ns.prev_output_values[port_idx] = value;
        ns.generation++;
    }
}

void Scheduler::inject_external_spread(uint32_t node_idx, uint32_t port_idx,
                                       const float* data, uint32_t length) {
    auto& ns = nodes_[node_idx];
    if (port_idx < ns.output_spreads.size()) {
        ns.output_spreads[port_idx].assign(data, data + length);
        ns.generation++;
    }
}

NodeState* Scheduler::find_node_mut(const std::string& id) {
    for (auto& ns : nodes_) {
        if (ns.node_id == id) return &ns;
    }
    return nullptr;
}

std::string Scheduler::type_name(uint32_t node_idx) const {
    if (node_idx >= nodes_.size()) return {};
    const auto* desc = nodes_[node_idx].loader->descriptor();
    return desc ? desc->name : std::string{};
}

bool Scheduler::reload_operator(const std::string& type_name, OperatorRegistry& registry,
                                const std::string& new_dylib_path) {
    // 1. Find all nodes of this type and save their param values by name
    struct SavedParams {
        uint32_t node_idx;
        std::unordered_map<std::string, float> values;
        std::unordered_map<std::string, std::string> string_values;
    };
    std::vector<SavedParams> saved;

    for (uint32_t i = 0; i < static_cast<uint32_t>(nodes_.size()); ++i) {
        auto& ns = nodes_[i];
        const auto* desc = ns.loader->descriptor();
        if (!desc || std::string(desc->name) != type_name) continue;

        SavedParams sp;
        sp.node_idx = i;
        // Save param values by name
        for (const auto& [name, idx] : ns.param_indices) {
            sp.values[name] = ns.param_values[idx];
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

        // Release existing texture if any
        if (ns.gpu_texture_view) { wgpuTextureViewRelease(ns.gpu_texture_view); ns.gpu_texture_view = nullptr; }
        if (ns.gpu_texture) { wgpuTextureRelease(ns.gpu_texture); ns.gpu_texture = nullptr; }

        // GPU sinks read input textures but don't produce their own
        if (ns.is_gpu_sink) {
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
                       | WGPUTextureUsage_CopySrc | extra_usage;
        ns.gpu_texture = wgpuDeviceCreateTexture(device, &tex_desc);

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
        // Release per-node GPU textures
        if (ns.gpu_texture_view) { wgpuTextureViewRelease(ns.gpu_texture_view); ns.gpu_texture_view = nullptr; }
        if (ns.gpu_texture) { wgpuTextureRelease(ns.gpu_texture); ns.gpu_texture = nullptr; }

        if (ns.instance) {
            ns.loader->destroy_instance(ns.instance);
            ns.instance = nullptr;
        }
    }
    nodes_.clear();
    wires_.clear();
}

} // namespace vivid
