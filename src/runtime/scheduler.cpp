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

        // Init param_values from descriptor defaults, then override from JSON
        ns.param_values.resize(desc->param_count);
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            ns.param_values[i] = desc->params[i].default_value;
        }
        for (const auto& [pname, pval] : ndef.params) {
            for (uint32_t i = 0; i < desc->param_count; ++i) {
                if (pname == desc->params[i].name) {
                    ns.param_values[i] = pval;
                    break;
                }
            }
        }

        // Generation-based cooking metadata
        ns.time_dependent = desc->time_dependent != 0;
        ns.is_gpu = (desc->domain == VIVID_DOMAIN_GPU);
        ns.generation = 0;
        ns.prev_output_values.resize(ns.output_port_count, 0.0f);

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

        auto tp_it = to_ns.input_port_indices.find(conn.to_port);
        if (tp_it == to_ns.input_port_indices.end()) {
            std::fprintf(stderr, "[vivid] Scheduler: node '%s' has no input port '%s'\n",
                conn.to_node.c_str(), conn.to_port.c_str());
            return false;
        }

        Wire w;
        w.from_node_idx = fi;
        w.from_port_idx = fp_it->second;
        w.to_node_idx   = ti;
        w.to_port_idx   = tp_it->second;
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

void Scheduler::tick(double time, double delta_time, uint64_t frame, void* gpu_state) {
    for (uint32_t ni = 0; ni < static_cast<uint32_t>(nodes_.size()); ++ni) {
        auto& ns = nodes_[ni];

        // Zero input values (unwired ports default to 0.0)
        std::fill(ns.input_values.begin(), ns.input_values.end(), 0.0f);

        // Copy upstream outputs into this node's inputs
        for (const auto& w : wires_) {
            if (w.to_node_idx == ni) {
                ns.input_values[w.to_port_idx] =
                    nodes_[w.from_node_idx].output_values[w.from_port_idx];
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

        // Build process context and tick
        VividProcessContext ctx{};
        ctx.time          = time;
        ctx.delta_time    = delta_time;
        ctx.frame         = frame;
        ctx.param_values  = ns.param_values.data();
        ctx.input_values  = ns.input_values.data();
        ctx.output_values = ns.output_values.data();

        // GPU state only for GPU-domain operators
        ctx.gpu = ns.is_gpu ? gpu_state : nullptr;

        ns.loader->process(ns.instance, &ctx);

        // Update generation: bump if outputs changed or this is a GPU node
        // (GPU nodes always produce side effects we can't compare)
        bool outputs_changed = ns.is_gpu;
        if (!outputs_changed) {
            for (size_t i = 0; i < ns.output_values.size(); ++i) {
                if (ns.output_values[i] != ns.prev_output_values[i]) {
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
}

bool Scheduler::has_gpu_operators() const {
    for (const auto& ns : nodes_) {
        const VividOperatorDescriptor* desc = ns.loader->descriptor();
        if (desc->domain == VIVID_DOMAIN_GPU)
            return true;
    }
    return false;
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
