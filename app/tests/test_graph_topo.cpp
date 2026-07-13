// Headless test for the visuals executor's topological order + cycle handling
// (gpu/graph_topo.h). Pure adjacency in, order out — no GPU. Guards the N-input
// generalization: the DFS must walk ALL input ports and never loop on a cycle.
#include "gpu/graph_topo.h"
#include "test_helpers.h"

#include <algorithm>
#include <vector>

namespace {
// index of `v` in `order`, or -1
int pos(const std::vector<int>& order, int v) {
    auto it = std::find(order.begin(), order.end(), v);
    return it == order.end() ? -1 : static_cast<int>(it - order.begin());
}
}  // namespace

int main() {
    using vivid::topo_order;

    // Linear chain 0<-1<-2<-3 (3 feeds Output). Post-order: inputs before consumers.
    {
        std::vector<std::vector<int>> adj = { {}, {0}, {1}, {2} };
        auto o = topo_order(adj, 3);
        CHECK(o.size() == 4);
        CHECK(pos(o, 0) < pos(o, 1) && pos(o, 1) < pos(o, 2) && pos(o, 2) < pos(o, 3));
    }

    // 2-input op: node 2 <- {0, 1}. Both inputs precede it; each appears once.
    {
        std::vector<std::vector<int>> adj = { {}, {}, {0, 1} };
        auto o = topo_order(adj, 2);
        CHECK(o.size() == 3);
        CHECK(pos(o, 0) < pos(o, 2) && pos(o, 1) < pos(o, 2));
    }

    // N-input (4) op: node 4 <- {0,1,2,3}. All four precede it; visited once each.
    {
        std::vector<std::vector<int>> adj = { {}, {}, {}, {}, {0, 1, 2, 3} };
        auto o = topo_order(adj, 4);
        CHECK(o.size() == 5);
        for (int s = 0; s < 4; ++s) CHECK(pos(o, s) < pos(o, 4));
    }

    // Shared subgraph: 1<-0, 2<-0, 3<-{1,2}. Node 0 rendered once (not twice).
    {
        std::vector<std::vector<int>> adj = { {}, {0}, {0}, {1, 2} };
        auto o = topo_order(adj, 3);
        CHECK(o.size() == 4);
        CHECK(std::count(o.begin(), o.end(), 0) == 1);
        CHECK(pos(o, 0) < pos(o, 1) && pos(o, 0) < pos(o, 2));
    }

    // Cycle through a second input port: 2 <- {1, 2-cycle via 0<-2}. Must terminate,
    // visiting each node at most once (the back-edge is dropped, not followed forever).
    {
        std::vector<std::vector<int>> adj = { {2}, {0}, {1} };   // 0<-2, 1<-0, 2<-1 : a 3-cycle
        auto o = topo_order(adj, 2);
        CHECK(o.size() == 3);   // terminated; every node once
        CHECK(pos(o, 0) >= 0 && pos(o, 1) >= 0 && pos(o, 2) >= 0);
    }

    // Self-loop on an input port must not hang.
    {
        std::vector<std::vector<int>> adj = { {}, {1, 0} };   // node 1 lists itself as an input
        auto o = topo_order(adj, 1);
        CHECK(o.size() == 2);
    }

    // Out-of-range / -1 edges are ignored (unconnected ports).
    {
        std::vector<std::vector<int>> adj = { {-1, 99}, {0, -1} };
        auto o = topo_order(adj, 1);
        CHECK(o.size() == 2 && pos(o, 0) < pos(o, 1));
    }

    return vivid::test::summary("test_graph_topo");
}
