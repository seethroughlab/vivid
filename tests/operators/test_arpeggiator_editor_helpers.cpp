// Pure-logic tests for the Arpeggiator editor's shared helpers:
// param-index math, Note Override label resolver, and editor-state
// clamping on mod_steps shrink.

#include "arpeggiator_editor_shared.h"

#include <cstdio>
#include <cstring>

#include "test_helpers.h"

namespace ae = ::vivid::arpeggiator_editor;

int main() {
    std::fprintf(stderr, "=== Test: Arpeggiator editor helpers ===\n\n");

    // --- Param index layout ---
    check(ae::param_index_for(ae::Lane::Velocity, 0) == 8,
          "vel_0 at legacy index 8");
    check(ae::param_index_for(ae::Lane::Velocity, 7) == 15,
          "vel_7 at legacy index 15");
    check(ae::param_index_for(ae::Lane::Velocity, 8) == 25,
          "vel_8 at follow-up base 25");
    check(ae::param_index_for(ae::Lane::Velocity, 15) == 32,
          "vel_15 at 32");

    check(ae::param_index_for(ae::Lane::Transpose, 0) == 16,
          "tr_0 at legacy index 16");
    check(ae::param_index_for(ae::Lane::Transpose, 7) == 23,
          "tr_7 at legacy index 23");
    check(ae::param_index_for(ae::Lane::Transpose, 8) == 33,
          "tr_8 at follow-up base 33");
    check(ae::param_index_for(ae::Lane::Transpose, 15) == 40,
          "tr_15 at 40");

    check(ae::param_index_for(ae::Lane::NoteOverride, 0) == 41,
          "note_override_0 at 41");
    check(ae::param_index_for(ae::Lane::NoteOverride, 15) == 56,
          "note_override_15 at 56");

    check(ae::param_index_for(ae::Lane::Gate, 0) == 57,
          "gt_0 at 57");
    check(ae::param_index_for(ae::Lane::Gate, 15) == 72,
          "gt_15 at 72");

    // --- Canonical param names ---
    check(ae::param_name_for(ae::Lane::Velocity, 3) == "vel_3",
          "vel name encodes step");
    check(ae::param_name_for(ae::Lane::Transpose, 12) == "tr_12",
          "tr name encodes step");
    check(ae::param_name_for(ae::Lane::NoteOverride, 9) == "note_override_9",
          "note_override name encodes step");
    check(ae::param_name_for(ae::Lane::Gate, 0) == "gt_0",
          "gt name encodes step");

    // --- Note Override labels ---
    check(std::strcmp(ae::note_override_label(0), "—") == 0,
          "0 → em-dash (follow mode)");
    check(std::strcmp(ae::note_override_label(1), "1") == 0,
          "1 → '1'");
    check(std::strcmp(ae::note_override_label(8), "8") == 0,
          "8 → '8'");
    check(std::strcmp(ae::note_override_label(9), "M") == 0,
          "9 → 'M' (mute)");

    // --- clamp_note_override ---
    check(ae::clamp_note_override(-5) == 0,  "negative clamps to 0");
    check(ae::clamp_note_override(0)  == 0,  "0 unchanged");
    check(ae::clamp_note_override(4)  == 4,  "mid unchanged");
    check(ae::clamp_note_override(9)  == 9,  "9 unchanged");
    check(ae::clamp_note_override(15) == 9,  "15 clamps to 9");

    // --- Editor state clamp on mod_steps shrink ---
    {
        int cur_row = 3, cur_step = 14;
        int anchor_row = 2, anchor_col = 10;
        ae::SelectionLike sel{2, 3, 10, 14};

        // Shrink to 8 steps — everything should fit within [0, 7].
        ae::clamp_editor_state(8,
                               &cur_row, &cur_step,
                               &anchor_row, &anchor_col, &sel);
        check(cur_step == 7,   "cursor step clamps to num_steps-1");
        check(anchor_col == 7, "anchor col clamps to num_steps-1");
        check(sel.col_lo == 7 && sel.col_hi == 7,
              "selection col range collapses to 7..7");
        check(cur_row == 3 && sel.row_hi == 3,
              "rows preserved (within bounds)");
    }

    // --- clamp respects row bounds too ---
    {
        int cur_row = 10, cur_step = 0;
        ae::clamp_editor_state(16, &cur_row, &cur_step, nullptr, nullptr, nullptr);
        check(cur_row == 3, "cursor row clamps to kRowCount-1 = 3");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
