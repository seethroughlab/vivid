#ifndef VIVID_RUNTIME_SCHEDULER_H
#define VIVID_RUNTIME_SCHEDULER_H

#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

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
    std::unordered_map<std::string, uint32_t> param_indices;

    // Generation-based cooking
    bool time_dependent = false;
    bool is_gpu = false;
    bool is_audio = false;
    uint64_t generation = 0;
    std::vector<uint32_t> upstream_nodes;       // indices of nodes feeding into this one
    std::vector<uint64_t> upstream_gens_cached;  // generation of each upstream at last cook
    std::vector<float> prev_output_values;
};

struct Wire {
    uint32_t from_node_idx, from_port_idx;
    uint32_t to_node_idx, to_port_idx;
    bool targets_param = false;  // true → to_port_idx indexes into param_values
};

// Optional callback invoked after each GPU node's process()
using PostNodeFn = std::function<void(uint32_t node_idx, const std::string& node_id)>;

class Scheduler {
public:
    bool build(const Graph& graph, OperatorRegistry& registry);
    void tick(double time, double delta_time, uint64_t frame, void* gpu_state = nullptr,
              PostNodeFn on_gpu_node = nullptr);
    void shutdown();
    const std::vector<NodeState>& nodes() const { return nodes_; }
    const std::vector<Wire>& wires() const { return wires_; }
    bool has_gpu_operators() const;
    bool has_audio_operators() const;
    void inject_external_output(uint32_t node_idx, uint32_t port_idx, float value);

    // Hot-reload: destroy old instances, swap dylib, recreate with param reconciliation
    bool reload_operator(const std::string& type_name, OperatorRegistry& registry,
                         const std::string& new_dylib_path);
    std::string type_name(uint32_t node_idx) const;

private:
    std::vector<NodeState> nodes_;
    std::vector<Wire> wires_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_SCHEDULER_H
