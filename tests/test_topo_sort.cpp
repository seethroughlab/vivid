#include "test_helpers.h"
#include "common/topo_sort.h"
#include <algorithm>
#include <set>

// Verify that `order` is a valid topological ordering of a DAG with `n` nodes.
// For each edge u→v in adj, u must appear before v.
static bool is_valid_topo_order(const std::vector<uint32_t>& order,
                                const std::vector<std::vector<uint32_t>>& adj) {
    // Build position map
    std::vector<int> pos(order.size(), -1);
    for (size_t i = 0; i < order.size(); ++i)
        pos[order[i]] = static_cast<int>(i);

    for (uint32_t u = 0; u < adj.size(); ++u) {
        for (uint32_t v : adj[u]) {
            if (pos[u] < 0 || pos[v] < 0) return false;
            if (pos[u] >= pos[v]) return false;
        }
    }
    return true;
}

int main() {
    // =================================================================
    // Test 1: Empty graph (n=0)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Empty graph ===\n");
        auto result = vivid::kahn_sort(0, {}, {});
        check(result.empty(), "empty graph produces empty order");
    }

    // =================================================================
    // Test 2: Single node
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Single node ===\n");
        std::vector<std::vector<uint32_t>> adj(1);  // no edges
        std::vector<uint32_t> in_degree = {0};
        auto result = vivid::kahn_sort(1, adj, in_degree);
        check(result.size() == 1, "single node: size == 1");
        check(result[0] == 0, "single node: contains node 0");
    }

    // =================================================================
    // Test 3: Linear chain A(0)→B(1)→C(2)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: Linear chain ===\n");
        std::vector<std::vector<uint32_t>> adj = {{1}, {2}, {}};
        std::vector<uint32_t> in_degree = {0, 1, 1};
        auto result = vivid::kahn_sort(3, adj, in_degree);
        check(result.size() == 3, "chain: all 3 nodes");
        check(result[0] == 0 && result[1] == 1 && result[2] == 2,
              "chain: order is 0,1,2");
    }

    // =================================================================
    // Test 4: Diamond  A→B, A→C, B→D, C→D
    //         0→1, 0→2, 1→3, 2→3
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: Diamond ===\n");
        std::vector<std::vector<uint32_t>> adj = {{1, 2}, {3}, {3}, {}};
        std::vector<uint32_t> in_degree = {0, 1, 1, 2};
        auto result = vivid::kahn_sort(4, adj, in_degree);
        check(result.size() == 4, "diamond: all 4 nodes");
        check(result[0] == 0, "diamond: node 0 first");
        check(result[3] == 3, "diamond: node 3 last");
        check(is_valid_topo_order(result, adj), "diamond: valid topological order");
    }

    // =================================================================
    // Test 5: Cycle detection (hard mode, default)
    //         0→1→2→0
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: Cycle (hard) ===\n");
        std::vector<std::vector<uint32_t>> adj = {{1}, {2}, {0}};
        std::vector<uint32_t> in_degree = {1, 1, 1};
        auto result = vivid::kahn_sort(3, adj, in_degree);
        check(result.empty(), "cycle hard: returns empty");
    }

    // =================================================================
    // Test 6: Cycle with soft_on_cycle=true
    //         0→1→2→0 (all in a cycle)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: Cycle (soft) ===\n");
        std::vector<std::vector<uint32_t>> adj = {{1}, {2}, {0}};
        std::vector<uint32_t> in_degree = {1, 1, 1};
        auto result = vivid::kahn_sort(3, adj, in_degree, true);
        check(result.size() == 3, "cycle soft: all 3 nodes present");

        // All nodes should be included
        std::set<uint32_t> nodes(result.begin(), result.end());
        check(nodes.count(0) && nodes.count(1) && nodes.count(2),
              "cycle soft: contains nodes 0,1,2");
    }

    // =================================================================
    // Test 7: Partial cycle with soft_on_cycle=true
    //         0→1→2→1 (cycle on 1-2), 0 is not in the cycle
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: Partial cycle (soft) ===\n");
        std::vector<std::vector<uint32_t>> adj = {{1}, {2}, {1}};
        std::vector<uint32_t> in_degree = {0, 2, 1};
        auto result = vivid::kahn_sort(3, adj, in_degree, true);
        check(result.size() == 3, "partial cycle soft: all 3 nodes present");
        check(result[0] == 0, "partial cycle soft: non-cycle node 0 comes first");

        std::set<uint32_t> nodes(result.begin(), result.end());
        check(nodes.count(0) && nodes.count(1) && nodes.count(2),
              "partial cycle soft: all nodes included");
    }

    // =================================================================
    // Test 8: Disconnected components
    //         0→1, 2→3 (two separate chains)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 8: Disconnected components ===\n");
        std::vector<std::vector<uint32_t>> adj = {{1}, {}, {3}, {}};
        std::vector<uint32_t> in_degree = {0, 1, 0, 1};
        auto result = vivid::kahn_sort(4, adj, in_degree);
        check(result.size() == 4, "disconnected: all 4 nodes");
        check(is_valid_topo_order(result, adj), "disconnected: valid topological order");
    }

    // =================================================================
    // Test 9: Wide fan-out  0→1, 0→2, 0→3, 0→4
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 9: Wide fan-out ===\n");
        std::vector<std::vector<uint32_t>> adj = {{1, 2, 3, 4}, {}, {}, {}, {}};
        std::vector<uint32_t> in_degree = {0, 1, 1, 1, 1};
        auto result = vivid::kahn_sort(5, adj, in_degree);
        check(result.size() == 5, "fan-out: all 5 nodes");
        check(result[0] == 0, "fan-out: root first");
        check(is_valid_topo_order(result, adj), "fan-out: valid topological order");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
