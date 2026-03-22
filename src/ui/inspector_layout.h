#pragma once

#include "ui/node_graph_constants.h"
#include "operator_api/types.h"
#include <cstdint>

namespace vivid::ui {

// Supported inspector row modes after normalization.
enum class RowMode : uint8_t {
    kFull,      // single column, full content width
    kTwoUp,    // two columns side by side
    kCompound, // full width for compound widgets (XY pad, color picker)
};

// Tracks cursor position and column state for inspector parameter layout.
// Passed by reference through draw_one_inspector_param and related functions.
struct InspectorLayout {
    float x;          // left edge of current column (changes per-column)
    float y;          // current vertical cursor (advances after each row)
    float col_w;      // width of current column
    float full_w;     // full panel width (kInspContentW)
    float base_x;     // left edge of the full panel (constant)

    // Row tracking for multi-column
    uint8_t row_columns = 0;    // total columns in current row (0 = single column)
    float   row_start_y = 0.0f; // y at start of current row (for aligning columns)
    float   row_max_h = 0.0f;   // tallest param in current row

    // Knob pairing state for normalized layout (columns >= 3 knob path).
    // Alternates 0/1 to pair consecutive knobs into two-up rows.
    uint8_t two_up_next_col = 0;

    // -----------------------------------------------------------------------
    // Semantic entry point — normalizes legacy N-column metadata into
    // full-width or two-up rows based on widget family rules.
    //
    // Rules:
    //   columns < 2           → full width
    //   columns == 2          → two-up (pass through col_index)
    //   columns >= 3 + KNOB   → two-up (auto-paired via two_up_next_col)
    //   columns >= 3 + other  → full width (sliders, dropdowns, bools, etc.)
    // -----------------------------------------------------------------------
    void begin_param_normalized(uint8_t columns, uint8_t col_index,
                                VividDisplayHint hint, VividParamType type,
                                uint32_t choice_count) {
        if (columns < 2) {
            two_up_next_col = 0;
            begin_param(0, 0);
            return;
        }

        if (columns == 2) {
            two_up_next_col = 0;
            begin_param(2, col_index);
            return;
        }

        // columns >= 3: semantic normalization

        // Knobs (non-dropdown, non-bool, non-file/text) → two-up auto-paired
        if (hint == VIVID_DISPLAY_KNOB &&
            choice_count == 0 &&
            type != VIVID_PARAM_BOOL &&
            type != VIVID_PARAM_FILE &&
            type != VIVID_PARAM_TEXT) {
            uint8_t col = two_up_next_col;
            two_up_next_col ^= 1;
            begin_param(2, col);
            return;
        }

        // Everything else (sliders, dropdowns, bools, file, text) → full width
        two_up_next_col = 0;
        begin_param(0, 0);
    }

    // -----------------------------------------------------------------------
    // Low-level layout primitive. Callers should prefer begin_param_normalized()
    // which applies semantic rules. This method trusts its inputs directly.
    //
    // Params sharing a multi-column row must be emitted contiguously
    // and in column order (col 0, 1, 2, ...).
    // -----------------------------------------------------------------------
    void begin_param(uint8_t columns, uint8_t col_index) {
        if (columns < 2) {
            // Full-width param — flush any pending multi-column row first
            flush_row();
            x = base_x;
            col_w = full_w;
            row_columns = 0;
        } else {
            if (col_index == 0) {
                // Starting a new multi-column row
                flush_row();
                row_start_y = y;
                row_max_h = 0.0f;
                row_columns = columns;
            }
            float total_gap = kInspColGap * (columns - 1);
            col_w = (full_w - total_gap) / columns;
            x = base_x + col_index * (col_w + kInspColGap);
            y = row_start_y; // reset y to row start for each column
        }
    }

    // Call after drawing a param to record its height.
    void end_param(float param_h) {
        if (row_columns < 2) {
            y += param_h;
        } else {
            if (param_h > row_max_h)
                row_max_h = param_h;
        }
    }

    // Flush a completed multi-column row — advance y by the tallest column.
    void flush_row() {
        if (row_columns >= 2 && row_max_h > 0.0f) {
            y = row_start_y + row_max_h;
            row_columns = 0;
            row_max_h = 0.0f;
        }
    }
};

} // namespace vivid::ui
