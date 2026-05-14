#pragma once
//
// Shared rectangular-selection helpers for grid, tracker, piano-roll, and
// lane-style editor surfaces.

#include <algorithm>

namespace vivid::ui {

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

inline int selection_cell_count(Selection sel) {
    const int rows = std::max(0, sel.row_hi - sel.row_lo + 1);
    const int cols = std::max(0, sel.col_hi - sel.col_lo + 1);
    const int count = rows * cols;
    return count > 0 ? count : 1;
}

inline void cursor_move(int dx, int dy, int max_row, int max_col,
                        int* cur_row, int* cur_col) {
    if (!cur_row || !cur_col) return;
    if (max_row < 0) max_row = 0;
    if (max_col < 0) max_col = 0;
    *cur_row = std::clamp(*cur_row + dy, 0, max_row);
    *cur_col = std::clamp(*cur_col + dx, 0, max_col);
}

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

} // namespace vivid::ui

// Legacy compatibility namespace for external packages built against the
// original selection-helper location. This is intentionally undocumented:
// first-party and new package code should use vivid::ui.
namespace vivid::editor_ui {
using ::vivid::ui::Selection;
using ::vivid::ui::selection_from_point;
using ::vivid::ui::selection_from_anchor_tip;
using ::vivid::ui::selection_extend;
using ::vivid::ui::selection_contains;
using ::vivid::ui::selection_cell_count;
using ::vivid::ui::cursor_move;
using ::vivid::ui::clamp_editor_state;
} // namespace vivid::editor_ui
