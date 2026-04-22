#pragma once
//
// Shared rectangular-selection helpers for grid-based operator editors.
// Promoted out of drum_sequencer_editor_shared + sequencer_editor_shared
// once a second adopter made the right-shape visible; future grid
// editors (Tracker, Arpeggiator, PatternSeq, Euclidean, etc.) reuse
// these primitives instead of rolling their own.
//
// Naming: the rectangle is (row × col) — deliberately generic so
// operators can map it to their own axis semantics (DrumSequencer uses
// row = drum index; Sequencer uses row = {value, gate}). Operators keep
// their own param-name encoding, clipboards, and draw state — this
// module is only the geometric invariants.
//
// Header-only. No link deps beyond <algorithm>.

#include <algorithm>

namespace vivid::editor_ui {

// Inclusive integer rectangle. A "point" selection is the degenerate
// case where *_lo == *_hi. Well-formed selections always satisfy
// row_hi >= row_lo and col_hi >= col_lo — the constructors below
// normalize that invariant; direct mutation must preserve it.
struct Selection {
    int row_lo = 0;
    int row_hi = 0;
    int col_lo = 0;
    int col_hi = 0;
};

inline Selection selection_from_point(int row, int col) {
    Selection s;
    s.row_lo = s.row_hi = row;
    s.col_lo = s.col_hi = col;
    return s;
}

inline Selection selection_from_anchor_tip(int anchor_row, int anchor_col,
                                           int tip_row, int tip_col) {
    Selection s;
    s.row_lo = std::min(anchor_row, tip_row);
    s.row_hi = std::max(anchor_row, tip_row);
    s.col_lo = std::min(anchor_col, tip_col);
    s.col_hi = std::max(anchor_col, tip_col);
    return s;
}

// Grow the rectangle to include (row, col). Idempotent when already
// contained.
inline Selection selection_extend(Selection sel, int row, int col) {
    sel.row_lo = std::min(sel.row_lo, row);
    sel.row_hi = std::max(sel.row_hi, row);
    sel.col_lo = std::min(sel.col_lo, col);
    sel.col_hi = std::max(sel.col_hi, col);
    return sel;
}

inline bool selection_contains(Selection sel, int row, int col) {
    return row >= sel.row_lo && row <= sel.row_hi
        && col >= sel.col_lo && col <= sel.col_hi;
}

// Number of cells covered by the rectangle. Point selections return 1.
inline int selection_cell_count(Selection sel) {
    const int rows = std::max(0, sel.row_hi - sel.row_lo + 1);
    const int cols = std::max(0, sel.col_hi - sel.col_lo + 1);
    const int count = rows * cols;
    return count > 0 ? count : 1;
}

// Arrow-key cursor movement with clamping. dx/dy are typically -1, 0,
// or +1. max_row / max_col are inclusive upper bounds (num_rows - 1,
// num_cols - 1); negative values are treated as 0.
inline void cursor_move(int dx, int dy, int max_row, int max_col,
                        int* cur_row, int* cur_col) {
    if (!cur_row || !cur_col) return;
    if (max_row < 0) max_row = 0;
    if (max_col < 0) max_col = 0;
    *cur_row = std::clamp(*cur_row + dy, 0, max_row);
    *cur_col = std::clamp(*cur_col + dx, 0, max_col);
}

// Clamp cursor + anchor + selection rectangle to [0, max_row] ×
// [0, max_col]. Call at the top of draw_editor() so state that survived
// a bounds shrink (e.g. num_steps dropped between frames) doesn't index
// past the new limits. Any pointer may be null; non-null pointers are
// clamped in place. Rectangle bounds are re-normalized if the clamp
// produced an inverted range.
inline void clamp_editor_state(int max_row, int max_col,
                               int* cur_row, int* cur_col,
                               int* anchor_row, int* anchor_col,
                               Selection* sel) {
    if (max_row < 0) max_row = 0;
    if (max_col < 0) max_col = 0;
    auto clamp_r = [&](int v) { return std::clamp(v, 0, max_row); };
    auto clamp_c = [&](int v) { return std::clamp(v, 0, max_col); };
    if (cur_row)    *cur_row    = clamp_r(*cur_row);
    if (cur_col)    *cur_col    = clamp_c(*cur_col);
    if (anchor_row) *anchor_row = clamp_r(*anchor_row);
    if (anchor_col) *anchor_col = clamp_c(*anchor_col);
    if (sel) {
        sel->row_lo = clamp_r(sel->row_lo);
        sel->row_hi = clamp_r(sel->row_hi);
        sel->col_lo = clamp_c(sel->col_lo);
        sel->col_hi = clamp_c(sel->col_hi);
        if (sel->row_hi < sel->row_lo) sel->row_hi = sel->row_lo;
        if (sel->col_hi < sel->col_lo) sel->col_hi = sel->col_lo;
    }
}

} // namespace vivid::editor_ui
