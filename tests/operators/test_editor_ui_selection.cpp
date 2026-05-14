// Focused unit tests for the shared vivid::ui::Selection geometry +
// cursor-movement helpers. These primitives are used by both
// DrumSequencer and Sequencer grid editors; operator-specific tests
// cover higher-level behaviour, this file locks down the arithmetic.

#include "operator_api/editor_ui/selection.h"

#include <cstdio>

#include "test_helpers.h"

namespace ui = ::vivid::ui;

int main() {
    std::fprintf(stderr, "=== Test: vivid::ui::Selection helpers ===\n\n");

    // --- selection_from_point ---
    {
        auto p = ui::selection_from_point(2, 5);
        check(p.row_lo == 2 && p.row_hi == 2 &&
              p.col_lo == 5 && p.col_hi == 5,
              "selection_from_point collapses to single cell");
        check(ui::selection_cell_count(p) == 1,
              "point selection counts as 1 cell");
    }

    // --- selection_from_anchor_tip normalizes reversed bounds ---
    {
        auto fwd = ui::selection_from_anchor_tip(0, 0, 3, 7);
        check(fwd.row_lo == 0 && fwd.row_hi == 3 &&
              fwd.col_lo == 0 && fwd.col_hi == 7,
              "forward rect preserves bounds");

        auto rev = ui::selection_from_anchor_tip(3, 7, 0, 0);
        check(rev.row_lo == 0 && rev.row_hi == 3 &&
              rev.col_lo == 0 && rev.col_hi == 7,
              "reversed rect yields identical bounds");

        auto diag = ui::selection_from_anchor_tip(2, 8, 5, 3);
        check(diag.row_lo == 2 && diag.row_hi == 5 &&
              diag.col_lo == 3 && diag.col_hi == 8,
              "cross-diagonal rect normalises correctly");
    }

    // --- selection_cell_count ---
    {
        const auto sel = ui::selection_from_anchor_tip(1, 2, 3, 5);  // 3×4
        check(ui::selection_cell_count(sel) == 12, "3×4 rect = 12 cells");
    }

    // --- selection_contains ---
    {
        const auto sel = ui::selection_from_anchor_tip(2, 2, 4, 6);
        check(ui::selection_contains(sel, 3, 4),   "interior cell contained");
        check(ui::selection_contains(sel, 2, 2),   "low-left corner contained");
        check(ui::selection_contains(sel, 4, 6),   "high-right corner contained");
        check(!ui::selection_contains(sel, 1, 4),  "cell above rect excluded");
        check(!ui::selection_contains(sel, 3, 7),  "cell right of rect excluded");
    }

    // --- selection_extend is idempotent when already contained ---
    {
        const auto sel  = ui::selection_from_anchor_tip(2, 2, 4, 6);
        const auto grew = ui::selection_extend(sel, 0, 8);
        check(grew.row_lo == 0 && grew.row_hi == 4 &&
              grew.col_lo == 2 && grew.col_hi == 8,
              "selection_extend grows to include outside cell");
        const auto same = ui::selection_extend(grew, 2, 4);
        check(same.row_lo == 0 && same.row_hi == 4 &&
              same.col_lo == 2 && same.col_hi == 8,
              "selection_extend no-op when cell already inside");
    }

    // --- cursor_move clamps at all edges ---
    {
        int r = 0, c = 0;
        ui::cursor_move(-1, -1, 5, 15, &r, &c);
        check(r == 0 && c == 0, "move at (0,0) clamps to (0,0)");
        ui::cursor_move(+1, +1, 5, 15, &r, &c);
        check(r == 1 && c == 1, "diagonal move advances both axes");
        r = 5; c = 15;
        ui::cursor_move(+1, +1, 5, 15, &r, &c);
        check(r == 5 && c == 15, "move at (max, max) clamps to corner");
        r = 0; c = 0;
        ui::cursor_move(0, 0, 5, 15, &r, &c);
        check(r == 0 && c == 0, "zero delta is no-op");
    }

    // --- clamp_editor_state: fold a stale rect into shrunken bounds ---
    {
        int cur_r = 4, cur_c = 12;
        int anchor_r = 3, anchor_c = 10;
        ui::Selection sel = ui::selection_from_anchor_tip(3, 10, 4, 12);

        // Shrink bounds to (2, 5) — everything past those bounds clamps in.
        ui::clamp_editor_state(2, 5, &cur_r, &cur_c,
                               &anchor_r, &anchor_c, &sel);
        check(cur_r == 2 && cur_c == 5,     "cursor clamps to bounds");
        check(anchor_r == 2 && anchor_c == 5, "anchor clamps to bounds");
        check(sel.row_lo == 2 && sel.row_hi == 2 &&
              sel.col_lo == 5 && sel.col_hi == 5,
              "rect collapses to corner after clamp");
    }

    // --- clamp_editor_state accepts null pointers (doesn't crash) ---
    {
        ui::Selection sel{};
        ui::clamp_editor_state(2, 2,
                               nullptr, nullptr, nullptr, nullptr, &sel);
        check(sel.row_lo == 0 && sel.col_lo == 0,
              "clamp with null cursor/anchor still normalises rect");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
