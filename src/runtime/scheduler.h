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
};

struct Wire {
    uint32_t from_node_idx, from_port_idx;
    uint32_t to_node_idx, to_port_idx;
};

class Scheduler {
public:
    bool build(const Graph& graph, OperatorRegistry& registry);
    void tick(double time, double delta_time, uint64_t frame);
    void shutdown();
    const std::vector<NodeState>& nodes() const { return nodes_; }

private:
    std::vector<NodeState> nodes_;
    std::vector<Wire> wires_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_SCHEDULER_H
