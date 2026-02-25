#include "runtime/scheduler.h"
#include <algorithm>
#include <queue>
#include <cstdio>

namespace vivid {

bool Scheduler::build(const Graph& graph, OperatorRegistry& registry) {
    nodes_.clear();
    wires_.clear();

    // Map node id -> index for lookup
    std::unordered_map<std::string, uint32_t> node_index;

    // 1. Create NodeStates
    for (const auto& ndef : graph.nodes()) {
        OperatorLoader* loader = registry.find(ndef.type);
        if (!loader) {
            std::fprintf(stderr, "[vivid] Scheduler: unknown operator type '%s'\n", ndef.type.c_str());
            return false;
        }

        const VividOperatorDescriptor* desc = loader->descriptor();

        NodeState ns;
        ns.node_id = ndef.id;
        ns.loader = loader;
        ns.instance = loader->create_instance();
        ns.input_port_count = 0;
        ns.output_port_count = 0;

        // Count and index ports by direction
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            if (desc->ports[i].direction == VIVID_PORT_INPUT) {
                ns.input_port_indices[desc->ports[i].name] = ns.input_port_count;
                ns.input_port_count++;
            } else {
                ns.output_port_indices[desc->ports[i].name] = ns.output_port_count;
                ns.output_port_count++;
            }
        }

        ns.input_values.resize(ns.input_port_count, 0.0f);
        ns.output_values.resize(ns.output_port_count, 0.0f);
        ns.input_spreads.resize(ns.input_port_count);
        ns.output_spreads.resize(ns.output_port_count);

        // Init param_values from descriptor defaults, then override from JSON
        ns.param_values.resize(desc->param_count);
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            ns.param_values[i] = desc->params[i].default_value;
        }
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            ns.param_indices[desc->params[i].name] = i;
        }
        for (const auto& [pname, pval] : ndef.params) {
            auto pi = ns.param_indices.find(pname);
            if (pi != ns.param_indices.end()) {
                ns.param_values[pi->second] = pval;
            }
        }

        // Generation-based cooking metadata
        ns.time_dependent = desc->time_dependent != 0;
        ns.is_gpu = (desc->domain == VIVID_DOMAIN_GPU);
        ns.is_audio = (desc->domain == VIVID_DOMAIN_AUDIO);
        ns.generation = 0;
        ns.prev_output_values.resize(ns.output_port_count, 0.0f);

        // Implicit analysis ports for audio-domain nodes
        if (ns.is_audio) {
            ns.output_port_indices["rms"] = ns.output_port_count++;
            ns.output_port_indices["peak"] = ns.output_port_count++;
            ns.output_port_indices["waveform"] = ns.output_port_count++;
            ns.output_values.resize(ns.output_port_count, 0.0f);
            ns.prev_output_values.resize(ns.output_port_count, 0.0f);
            ns.output_spreads.resize(ns.output_port_count);
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

        auto fp_it = from_ns.output_port_indices.find(conn.from_port);
        if (fp_it == from_ns.output_port_indices.end()) {
            std::fprintf(stderr, "[vivid] Scheduler: node '%s' has no output port '%s'\n",
                conn.from_node.c_str(), conn.from_port.c_str());
            return false;
        }

        Wire w;
        w.from_node_idx = fi;
        w.from_port_idx = fp_it->second;
        w.to_node_idx   = ti;

        auto tp_it = to_ns.input_port_indices.find(conn.to_port);
        if (tp_it != to_ns.input_port_indices.end()) {
            w.to_port_idx = tp_it->second;
            w.targets_param = false;
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
        wires_.push_back(w);

        adj[fi].push_back(ti);
        in_degree[ti]++;
    }

    // 3. Kahn's algorithm — topological sort
    std::queue<uint32_t> q;
    for (uint32_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0)
            q.push(i);
    }

    std::vector<uint32_t> sorted_order;
    sorted_order.reserve(n);
    while (!q.empty()) {
        uint32_t cur = q.front();
        q.pop();
        sorted_order.push_back(cur);
        for (uint32_t next : adj[cur]) {
            if (--in_degree[next] == 0)
                q.push(next);
        }
    }

    if (sorted_order.size() != n) {
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

        // Zero input values and clear input spreads (unwired ports default to 0.0)
        std::fill(ns.input_values.begin(), ns.input_values.end(), 0.0f);
        for (auto& sp : ns.input_spreads) sp.clear();

        // Copy upstream outputs into this node's inputs / params
        for (const auto& w : wires_) {
            if (w.to_node_idx == ni) {
                float val = nodes_[w.from_node_idx].output_values[w.from_port_idx];
                if (w.targets_param) {
                    ns.param_values[w.to_port_idx] = val;
                } else {
                    ns.input_values[w.to_port_idx] = val;
                    // Spread propagation
                    const auto& src_spread = nodes_[w.from_node_idx].output_spreads[w.from_port_idx];
                    if (!src_spread.empty()) {
                        ns.input_spreads[w.to_port_idx] = src_spread;
                        ns.input_values[w.to_port_idx] = src_spread[0];  // scalar fallback
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
            out_spread_storage[p].resize(1024, 0.0f);
            out_spreads[p].data     = out_spread_storage[p].data();
            out_spreads[p].length   = 0;
            out_spreads[p].capacity = 1024;
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

        // GPU state only for GPU-domain operators
        ctx.gpu = ns.is_gpu ? gpu_state : nullptr;

        ns.loader->process(ns.instance, &ctx);

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
            on_gpu_node(ni, ns.node_id);
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
        to_ns.param_values[w.to_port_idx] =
            nodes_[w.from_node_idx].output_values[w.from_port_idx];
    }
}

bool Scheduler::has_gpu_operators() const {
    for (const auto& ns : nodes_) {
        const VividOperatorDescriptor* desc = ns.loader->descriptor();
        if (desc->domain == VIVID_DOMAIN_GPU)
            return true;
    }
    return false;
}

bool Scheduler::has_audio_operators() const {
    for (const auto& ns : nodes_) {
        const VividOperatorDescriptor* desc = ns.loader->descriptor();
        if (desc->domain == VIVID_DOMAIN_AUDIO)
            return true;
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

        // Rebuild port/param indices from new descriptor
        ns.input_port_count = 0;
        ns.output_port_count = 0;
        ns.input_port_indices.clear();
        ns.output_port_indices.clear();
        ns.param_indices.clear();

        for (uint32_t p = 0; p < new_desc->port_count; ++p) {
            if (new_desc->ports[p].direction == VIVID_PORT_INPUT) {
                ns.input_port_indices[new_desc->ports[p].name] = ns.input_port_count++;
            } else {
                ns.output_port_indices[new_desc->ports[p].name] = ns.output_port_count++;
            }
        }

        ns.input_values.assign(ns.input_port_count, 0.0f);
        ns.output_values.assign(ns.output_port_count, 0.0f);
        ns.prev_output_values.assign(ns.output_port_count, 0.0f);

        ns.input_spreads.resize(ns.input_port_count);
        ns.output_spreads.resize(ns.output_port_count);

        // Implicit analysis ports for audio-domain nodes
        if (new_desc->domain == VIVID_DOMAIN_AUDIO) {
            ns.output_port_indices["rms"] = ns.output_port_count++;
            ns.output_port_indices["peak"] = ns.output_port_count++;
            ns.output_port_indices["waveform"] = ns.output_port_count++;
            ns.output_values.resize(ns.output_port_count, 0.0f);
            ns.prev_output_values.resize(ns.output_port_count, 0.0f);
            ns.output_spreads.resize(ns.output_port_count);
        }

        // Reconcile params by name: preserve values that still exist, use defaults for new
        ns.param_values.resize(new_desc->param_count);
        for (uint32_t p = 0; p < new_desc->param_count; ++p) {
            ns.param_indices[new_desc->params[p].name] = p;
            auto it = sp.values.find(new_desc->params[p].name);
            if (it != sp.values.end()) {
                ns.param_values[p] = it->second;  // preserve old value
            } else {
                ns.param_values[p] = new_desc->params[p].default_value;  // new param
            }
        }

        ns.time_dependent = new_desc->time_dependent != 0;
        ns.is_gpu = (new_desc->domain == VIVID_DOMAIN_GPU);
        ns.is_audio = (new_desc->domain == VIVID_DOMAIN_AUDIO);

        // Bump generation to force downstream recompute
        ns.generation++;
    }

    return true;
}

void Scheduler::shutdown() {
    for (auto& ns : nodes_) {
        if (ns.instance) {
            ns.loader->destroy_instance(ns.instance);
            ns.instance = nullptr;
        }
    }
    nodes_.clear();
    wires_.clear();
}

} // namespace vivid
