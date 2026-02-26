#pragma once
#include <cstdint>
#include <queue>
#include <vector>

namespace vivid {

// Topological sort via Kahn's algorithm.
// adj[i] = successor indices. in_degree passed by value (mutated internally).
// If cycle detected: soft_on_cycle=true appends remaining nodes; false returns empty.
inline std::vector<uint32_t> kahn_sort(
    uint32_t n,
    const std::vector<std::vector<uint32_t>>& adj,
    std::vector<uint32_t> in_degree,
    bool soft_on_cycle = false)
{
    std::queue<uint32_t> q;
    for (uint32_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0)
            q.push(i);
    }

    std::vector<uint32_t> order;
    order.reserve(n);
    while (!q.empty()) {
        uint32_t cur = q.front();
        q.pop();
        order.push_back(cur);
        for (uint32_t next : adj[cur]) {
            if (--in_degree[next] == 0)
                q.push(next);
        }
    }

    if (order.size() == n)
        return order;

    if (soft_on_cycle) {
        // Append remaining nodes that weren't reachable
        std::vector<bool> visited(n, false);
        for (uint32_t idx : order) visited[idx] = true;
        for (uint32_t i = 0; i < n; ++i) {
            if (!visited[i]) order.push_back(i);
        }
        return order;
    }

    return {};  // cycle detected, hard failure
}

} // namespace vivid
