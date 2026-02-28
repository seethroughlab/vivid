#pragma once

#include "ui/node_graph_constants.h"
#include <cstdint>

namespace vivid::ui {

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

    // Begin a new param. Call before drawing each param.
    // columns/col_index come from ParamInfo metadata.
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
