#pragma once
#include <functional>
#include <vector>

namespace vivid {

// Post-order DFS topological order of the visuals chain, starting from `feed`, over each
// node's texture-input edges (`inputs[i]` = the source node indices feeding node i; -1 or
// out-of-range entries are ignored). Each node lands AFTER all its inputs; a shared node
// appears once. Cycle-safe: a node already in-progress (state 1) or done (state 2) is
// skipped, so a back-edge through ANY input port is simply dropped and the walk terminates.
//
// Pure (no GPU) so the executor's ordering + cycle handling is unit-testable headlessly;
// VisualGraph::render builds the adjacency from its nodes and calls this.
inline std::vector<int> topo_order(const std::vector<std::vector<int>>& inputs, int feed) {
    const int n = static_cast<int>(inputs.size());
    std::vector<int> order;
    std::vector<char> state(n, 0);   // 0 unvisited, 1 in-progress, 2 done
    std::function<void(int)> visit = [&](int i) {
        if (i < 0 || i >= n || state[i]) return;
        state[i] = 1;
        for (int e : inputs[i]) visit(e);
        state[i] = 2;
        order.push_back(i);
    };
    visit(feed);
    return order;
}

}  // namespace vivid
