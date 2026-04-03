#pragma once

#include "ui/graph/node_graph_constants.h"
#include "operator_api/types.h"
#include <cstdint>

namespace vivid::ui {

// Supported inspector row modes after normalization.
enum class RowMode : uint8_t {
    kFull,      // single column, full content width
    kTwoUp,    // two columns side by side
    kMultiUp,  // N columns side by side (N >= 2)
    kCompound, // full width for compound widgets (XY pad, color picker)
};

struct ParamLayoutRequest {
    uint8_t columns = 0;
    uint8_t col_index = 0;
    VividDisplayHint hint = VIVID_DISPLAY_DEFAULT;
    VividParamType type = VIVID_PARAM_FLOAT;
    uint32_t choice_count = 0;
    bool metadata_heavy = false;
    bool long_label = false;
};

struct ParamLayoutPlan {
    RowMode row_mode = RowMode::kFull;
    uint8_t columns = 0;    // total columns in row (for kMultiUp)
    uint8_t column = 0;     // col_index within row
    bool compact = false;
    bool allow_secondary_text = true;
    bool allow_semantic_hint = true;
    bool allow_connection_source = true;
    bool allow_inline_midi_badge = true;
    bool allow_inline_lock_badge = true;
    bool allow_inline_value = true;
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

    static bool is_compound_hint(VividDisplayHint hint) {
        return hint == VIVID_DISPLAY_XY_PAD || hint == VIVID_DISPLAY_COLOR;
    }

    static bool param_supports_two_up(const ParamLayoutRequest& req) {
        if (req.type == VIVID_PARAM_BOOL ||
            req.type == VIVID_PARAM_FILE ||
            req.type == VIVID_PARAM_TEXT ||
            req.choice_count > 0) {
            return false;
        }
        if (req.long_label || req.metadata_heavy)
            return false;
        if (req.hint == VIVID_DISPLAY_KNOB)
            return true;
        return req.type == VIVID_PARAM_FLOAT || req.type == VIVID_PARAM_INT;
    }

    static ParamLayoutPlan full_plan() {
        ParamLayoutPlan plan;
        plan.row_mode = RowMode::kFull;
        return plan;
    }

    static ParamLayoutPlan compound_plan() {
        ParamLayoutPlan plan;
        plan.row_mode = RowMode::kCompound;
        return plan;
    }

    static ParamLayoutPlan multi_up_plan(uint8_t columns, uint8_t col_index) {
        ParamLayoutPlan plan;
        plan.row_mode = RowMode::kMultiUp;
        plan.columns = columns;
        plan.column = col_index;
        plan.compact = true;
        plan.allow_secondary_text = false;
        plan.allow_semantic_hint = false;
        plan.allow_connection_source = false;
        plan.allow_inline_midi_badge = false;
        plan.allow_inline_lock_badge = true;
        return plan;
    }

    static ParamLayoutPlan two_up_plan(uint8_t column) {
        return multi_up_plan(2, column);
    }

    static bool requests_form_two_up_pair(const ParamLayoutRequest& lhs,
                                          const ParamLayoutRequest& rhs) {
        if (!param_supports_two_up(lhs) || !param_supports_two_up(rhs))
            return false;
        if (is_compound_hint(lhs.hint) || is_compound_hint(rhs.hint))
            return false;
        if (lhs.columns < 2 || rhs.columns < 2)
            return false;
        if (lhs.columns != rhs.columns)
            return false;

        if (lhs.columns == 2) {
            return lhs.col_index == 0 && rhs.col_index == 1;
        }

        // Legacy 3/4-column layouts only stay compact for explicit adjacent knob pairs.
        if (lhs.hint != VIVID_DISPLAY_KNOB || rhs.hint != VIVID_DISPLAY_KNOB)
            return false;
        if ((lhs.col_index % 2) != 0)
            return false;
        if (rhs.col_index != lhs.col_index + 1)
            return false;
        return (lhs.col_index / 2) == (rhs.col_index / 2);
    }

    // Check whether count consecutive requests form a complete N-column row.
    // All must be multi-up-compatible, share the same column count, and have
    // sequential col_index values 0..count-1.
    static bool requests_form_multi_up_run(const ParamLayoutRequest* reqs, uint8_t count) {
        if (count < 2) return false;
        uint8_t columns = reqs[0].columns;
        if (columns != count) return false;
        for (uint8_t i = 0; i < count; ++i) {
            if (!param_supports_two_up(reqs[i])) return false;
            if (is_compound_hint(reqs[i].hint)) return false;
            if (reqs[i].columns != columns) return false;
            if (reqs[i].col_index != i) return false;
        }
        return true;
    }

    ParamLayoutPlan plan_param(const ParamLayoutRequest& req) {
        if (is_compound_hint(req.hint))
            return compound_plan();
        // Compact/two-up planning is row-aware and must be granted by an
        // explicit adjacent pair check in the draw pass.
        return full_plan();
    }

    void begin_param(const ParamLayoutPlan& plan) {
        switch (plan.row_mode) {
        case RowMode::kFull:
        case RowMode::kCompound:
            begin_param(0, 0);
            break;
        case RowMode::kTwoUp:
            begin_param(2, plan.column);
            break;
        case RowMode::kMultiUp:
            begin_param(plan.columns, plan.column);
            break;
        }
    }

    // -----------------------------------------------------------------------
    // Semantic entry point — normalizes a single param conservatively.
    // Compact/two-up rows must be granted by a row-aware adjacent pair check
    // in the draw pass via requests_form_two_up_pair().
    // -----------------------------------------------------------------------
    void begin_param_normalized(uint8_t columns, uint8_t col_index,
                                VividDisplayHint hint, VividParamType type,
                                uint32_t choice_count) {
        ParamLayoutRequest req;
        req.columns = columns;
        req.col_index = col_index;
        req.hint = hint;
        req.type = type;
        req.choice_count = choice_count;
        begin_param(plan_param(req));
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
            if (col_index == 0 || row_columns != columns) {
                // Starting a new multi-column row. If a caller ever asks for
                // column 1 without a valid column 0 partner, recover by
                // treating the current y as the row start instead of reusing a
                // stale previous row.
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
