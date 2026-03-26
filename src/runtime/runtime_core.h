#pragma once

#include "runtime/compiled_graph.h"
#include "runtime/cadence_bridge.h"
#include <memory>
#include <vector>

namespace vivid {

// ---------------------------------------------------------------------------
// RuntimeCore — shared runtime state accessed by both frame and audio sides.
//
// Owns the CompiledGraph (the compiled, ready-to-execute graph), the
// CadenceBridge (double-buffered frame↔audio snapshot bridge), and solo state.
// Both Scheduler (frame-side) and AudioEngine (audio-side) hold references
// to a single RuntimeCore instance.
// ---------------------------------------------------------------------------

struct RuntimeCore {
    std::unique_ptr<CompiledGraph> compiled_graph;
    CadenceBridge cadence_bridge;

    // Solo mode (session-only, not serialized).
    // The active set marks the solo node and all its transitive upstream
    // dependencies as active; non-active nodes are muted/skipped.
    int solo_node_idx = -1;
    std::vector<bool> solo_active_set;

    void set_solo(int node_idx) {
        if (!compiled_graph) return;
        if (node_idx == solo_node_idx) return;
        uint32_t n = static_cast<uint32_t>(compiled_graph->nodes.size());
        if (node_idx < 0 || node_idx >= static_cast<int>(n)) {
            solo_node_idx = -1;
            solo_active_set.clear();
            cadence_bridge.set_solo_active_set({});
            return;
        }
        solo_node_idx = node_idx;
        solo_active_set.assign(n, false);

        // BFS: mark solo node and all transitive upstream dependencies
        std::vector<uint32_t> queue;
        queue.push_back(static_cast<uint32_t>(node_idx));
        solo_active_set[node_idx] = true;
        while (!queue.empty()) {
            uint32_t cur = queue.back();
            queue.pop_back();
            for (uint32_t up : compiled_graph->nodes[cur].upstream_nodes) {
                if (!solo_active_set[up]) {
                    solo_active_set[up] = true;
                    queue.push_back(up);
                }
            }
        }

        // Sync to audio side via snapshot bridge
        cadence_bridge.set_solo_active_set(solo_active_set);
    }

    bool is_solo_active() const { return solo_node_idx >= 0; }
};

} // namespace vivid
