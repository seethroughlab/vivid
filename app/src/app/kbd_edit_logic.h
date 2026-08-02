#pragma once
// UX Ph4 F3: the PURE decision logic behind keyboard editing (input_kbd_edit.cpp), split out so it is
// unit-testable without the GLFW/session/App stack. Kept in lock-step with the mouse wiring path
// (app/src/app/input_graph.cpp AudioNodeGraph::on_up + the wire-start guard at :722).
#include <cmath>
#include <vector>

namespace vivid::input {

// Node "kind" values as reported by session_track_audio_graph_node_kind (the fuller GNKind range, not
// just the 0/1/2 the header comment implies): 0 instrument · 1 effect · 2 output · 3 MIDI-in ·
// 4 note effect · 5 modulator · 6 clip · 7 selector · 8 generator.

// Can a node of this kind START a wire from its output? Engine-managed note infrastructure (2/3/6/7/8)
// has no user-drawable output (reconcile_note_subgraph owns it). Mirrors input_graph.cpp:722.
inline bool audio_can_source(int kind) { return kind == 0 || kind == 1 || kind == 4 || kind == 5; }

// The signal a keyboard wire carries, inferred from the source+target kinds (mirrors on_up:817/855-863).
// Returns the session_audio_graph_connect_kind() `kind`: 1 = note, 0 = audio; or -1 = illegal (skip).
// Modulator sources (5) go to a specific PARAM port, which needs a picker — not handled here (-1).
inline int audio_wire_kind(int from_kind, int to_kind) {
    if (from_kind == 5) return -1;                          // modulator -> param: not a kind-connect
    if (from_kind == 3 || from_kind == 4)                   // note source: lands on an instrument / note fx
        return (to_kind == 0 || to_kind == 4) ? 1 : -1;
    return (to_kind != 0 && to_kind != 3 && to_kind != 4) ? 0 : -1;   // audio: may not land on a source
}

// Index of the nearest node to `from` in the screen direction (dx,dy ∈ {-1,0,1}), or -1 if none lie
// that way. Favours on-axis candidates (perp distance weighted), then nearest. xs/ys are parallel
// per-node position arrays (screen coords, y down). Used for arrow-key spatial selection.
inline int nearest_in_dir(const std::vector<float>& xs, const std::vector<float>& ys,
                          int from, float dx, float dy) {
    if (from < 0 || from >= static_cast<int>(xs.size())) return -1;
    int best = -1; float best_score = 0.f;
    for (int i = 0; i < static_cast<int>(xs.size()); ++i) {
        if (i == from) continue;
        const float ox = xs[i] - xs[from], oy = ys[i] - ys[from];
        const float primary = ox * dx + oy * dy;            // signed distance along the arrow
        if (primary <= 1.f) continue;                        // must be in that direction
        const float perp = std::fabs(ox * dy + oy * dx);     // off-axis distance
        const float score = primary + 2.f * perp;
        if (best < 0 || score < best_score) { best = i; best_score = score; }
    }
    return best;
}

}  // namespace vivid::input
