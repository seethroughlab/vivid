#ifndef VIVID_RUNTIME_SCHEDULER_H
#define VIVID_RUNTIME_SCHEDULER_H

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace vivid {

struct NodeState {
    std::string node_id;
    OperatorLoader* loader;
    void* instance;
    uint32_t input_port_count;
    uint32_t output_port_count;
    std::vector<float> param_values;
    std::vector<float> input_values;
    std::vector<float> output_values;
    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;

    // Generation-based cooking
    bool time_dependent = false;
    bool is_gpu = false;
    uint64_t generation = 0;
    std::vector<uint32_t> upstream_nodes;       // indices of nodes feeding into this one
    std::vector<uint64_t> upstream_gens_cached;  // generation of each upstream at last cook
    std::vector<float> prev_output_values;
};

struct Wire {
    uint32_t from_node_idx, from_port_idx;
    uint32_t to_node_idx, to_port_idx;
};

class Scheduler {
public:
    bool build(const Graph& graph, OperatorRegistry& registry);
    void tick(double time, double delta_time, uint64_t frame, void* gpu_state = nullptr);
    void shutdown();
    const std::vector<NodeState>& nodes() const { return nodes_; }
    bool has_gpu_operators() const;

private:
    std::vector<NodeState> nodes_;
    std::vector<Wire> wires_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_SCHEDULER_H
