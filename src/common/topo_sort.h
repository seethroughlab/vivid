#pragma once
#include <cstdint>
#include <queue>
#include <utility>
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

// Detect back-edges in a directed graph via iterative DFS.
//
// Returns a mask sized to `edges.size()`. An entry is `true` when the edge
// forms a back-edge (points to an ancestor in the DFS tree) — i.e. it is part
// of a cycle and cannot participate in a topological order. Self-loops
// (u == v) are back-edges. Tree/forward/cross edges are `false`.
//
// Parameters:
//   n     — number of nodes; edges reference nodes in [0, n).
//   edges — directed edges as (from, to) pairs. Order is preserved in the
//           returned mask so callers can map back to their edge indices.
inline std::vector<bool> detect_back_edges(
    uint32_t n,
    const std::vector<std::pair<uint32_t, uint32_t>>& edges)
{
    std::vector<bool> result(edges.size(), false);
    if (n == 0) return result;

    std::vector<std::vector<std::pair<uint32_t, size_t>>> adj(n);
    for (size_t ei = 0; ei < edges.size(); ++ei) {
        uint32_t u = edges[ei].first;
        uint32_t v = edges[ei].second;
        if (u >= n || v >= n) continue;
        if (u == v) { result[ei] = true; continue; }
        adj[u].push_back({v, ei});
    }

    enum : uint8_t { WHITE = 0, GRAY = 1, BLACK = 2 };
    std::vector<uint8_t> color(n, WHITE);

    for (uint32_t start = 0; start < n; ++start) {
        if (color[start] != WHITE) continue;
        std::vector<std::pair<uint32_t, size_t>> stack;
        stack.push_back({start, 0});
        color[start] = GRAY;
        while (!stack.empty()) {
            auto& frame = stack.back();
            uint32_t u = frame.first;
            if (frame.second < adj[u].size()) {
                uint32_t v = adj[u][frame.second].first;
                size_t ei = adj[u][frame.second].second;
                ++frame.second;
                if (color[v] == WHITE) {
                    color[v] = GRAY;
                    stack.push_back({v, 0});
                } else if (color[v] == GRAY) {
                    result[ei] = true;  // points to an ancestor on the DFS stack
                }
                // BLACK: forward/cross — not a back-edge.
            } else {
                color[u] = BLACK;
                stack.pop_back();
            }
        }
    }

    return result;
}

} // namespace vivid
