#pragma once
namespace vivid {

// Is the active Output node's primary input wired to a real, in-range producer node?
// This is the single STRUCTURAL predicate behind "the Output has a feed" (P2-03): it
// distinguishes an intentionally-empty canvas (nothing wired to Output — benign, "empty
// by design") from a graph that IS feeding Output (whose result may separately be blank —
// a heuristic question answered by reading pixels). Pure (no GPU), so the render path
// (VisualGraph::present_to / read_output_pixels), the health snapshot, and the MCP
// `nonblank_visual_output` check all agree on the same rule and it stays unit-testable.
//
// `output_idx` is VisualGraph::output_index() (-1 = no Output node); `feed` is that Output
// node's in(0); `node_count` is the graph's node count.
inline bool output_is_fed(int output_idx, int feed, int node_count) {
    return output_idx >= 0 && feed >= 0 && feed < node_count;
}

}  // namespace vivid
