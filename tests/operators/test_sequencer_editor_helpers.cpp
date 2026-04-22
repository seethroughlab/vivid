// Pure-logic tests for the Sequencer editor shared helpers. Covers
// param-name encoding, rectangular selection math, cursor clamping on
// `steps` shrink, and the SelectionClipboard round-trip. Runs without
// any editor/host plumbing.

#include "sequencer_editor_shared.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace se = ::vivid::sequencer_editor;

namespace {

struct CapturedSet {
    std::string name;
    float value = 0.0f;
};

struct CaptureCtx {
    std::vector<CapturedSet> calls;
};

void capture_set_param(void* opaque, const char* name, float value) {
    auto* ctx = static_cast<CaptureCtx*>(opaque);
    if (!ctx) return;
    ctx->calls.push_back({std::string(name ? name : ""), value});
}
void capture_set_string_param(void*, const char*, const char*) {}

VividInspectorCommandAPI make_commands(CaptureCtx& sink) {
    VividInspectorCommandAPI api{};
    api.opaque            = &sink;
    api.set_param         = capture_set_param;
    api.set_string_param  = capture_set_string_param;
    return api;
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: Sequencer editor helpers ===\n\n");

    // --- param_name_for encoding ---
    check(se::param_name_for(se::RowKind::Value, 0) == "step_value_0",
          "value row step 0 → step_value_0");
    check(se::param_name_for(se::RowKind::Value, 31) == "step_value_31",
          "value row step 31 → step_value_31");
    check(se::param_name_for(se::RowKind::Gate, 0) == "step_gate_0",
          "gate row step 0 → step_gate_0");
    check(se::param_name_for(se::RowKind::Gate, 15) == "step_gate_15",
          "gate row step 15 → step_gate_15");

    // --- param_index_for stability ---
    check(se::param_index_for(se::RowKind::Value, 0) == 2,
          "value base starts at descriptor index 2");
    check(se::param_index_for(se::RowKind::Value, 31) == 33,
          "value row spans indices 2..33");
    check(se::param_index_for(se::RowKind::Gate, 0) == 34,
          "gate base starts at descriptor index 34");
    check(se::param_index_for(se::RowKind::Gate, 31) == 65,
          "gate row spans indices 34..65");

    // --- Selection construction ---
    {
        auto p = se::selection_from_point(1, 5);
        check(p.row_lo == 1 && p.row_hi == 1 &&
              p.col_lo == 5 && p.col_hi == 5,
              "selection_from_point collapses to single cell");

        auto a = se::selection_from_anchor_tip(0, 8, 1, 3);
        check(a.row_lo == 0 && a.row_hi == 1 &&
              a.col_lo == 3 && a.col_hi == 8,
              "selection_from_anchor_tip normalizes reversed bounds");
    }

    // --- selection_cell_count ---
    check(se::selection_cell_count(se::selection_from_point(0, 0)) == 1,
          "point selection counts as 1 cell");
    check(se::selection_cell_count(se::selection_from_anchor_tip(0, 0, 1, 3)) == 8,
          "2×4 rectangle = 8 cells");

    // --- selection_contains ---
    {
        auto sel = se::selection_from_anchor_tip(0, 2, 1, 6);
        check(se::selection_contains(sel, 0, 2), "contains lower-left corner");
        check(se::selection_contains(sel, 1, 6), "contains upper-right corner");
        check(se::selection_contains(sel, 1, 4), "contains interior cell");
        check(!se::selection_contains(sel, 0, 1), "excludes cell left of rect");
        check(!se::selection_contains(sel, 0, 7), "excludes cell right of rect");
    }

    // --- cursor_move clamping ---
    {
        // Sequencer grid is 2 rows × num_steps cols; caller derives bounds.
        constexpr int kMaxRow = se::kRowCount - 1;  // 1
        const int max_col = 15;                      // num_steps = 16
        int row = 0, step = 0;
        se::cursor_move(+1, 0, kMaxRow, max_col, &row, &step);
        check(row == 0 && step == 1, "cursor_move right advances step");
        se::cursor_move(-1, 0, kMaxRow, max_col, &row, &step);
        check(row == 0 && step == 0, "cursor_move left decreases step");
        se::cursor_move(-1, 0, kMaxRow, max_col, &row, &step);
        check(row == 0 && step == 0, "cursor_move left at 0 clamps");
        se::cursor_move(0, +1, kMaxRow, max_col, &row, &step);
        check(row == 1, "cursor_move down to row 1");
        se::cursor_move(0, +1, kMaxRow, max_col, &row, &step);
        check(row == 1, "cursor_move down at row 1 clamps (only 2 rows)");
        row = 1; step = 31;
        se::cursor_move(+1, 0, kMaxRow, max_col, &row, &step);
        check(step == 15, "cursor clamps to max_col when starting past limit");
    }

    // --- clamp_editor_state on steps shrink ---
    {
        int cur_row = 1, cur_step = 20;
        int anchor_row = 1, anchor_col = 25;
        auto sel = se::selection_from_anchor_tip(
            anchor_row, anchor_col, cur_row, cur_step);
        // Before clamp: selection spans steps 20..25.
        check(sel.col_lo == 20 && sel.col_hi == 25,
              "initial selection covers high steps");

        // Steps shrink to 8 → everything should clamp to [0, 7].
        se::clamp_editor_state(se::kRowCount - 1, 7,
                               &cur_row, &cur_step,
                               &anchor_row, &anchor_col, &sel);
        check(cur_step == 7, "cursor clamps to num_steps-1 = 7");
        check(anchor_col == 7, "anchor clamps to num_steps-1 = 7");
        check(sel.col_lo == 7 && sel.col_hi == 7,
              "selection collapses to valid range (step 7)");
        check(cur_row == 1 && anchor_row == 1 && sel.row_lo == 1 && sel.row_hi == 1,
              "rows preserved through steps-shrink");
    }

    // --- SelectionClipboard round-trip (copy then paste) ---
    {
        // Synthesize a param array covering at least indices 2..65.
        std::vector<float> params(80, 0.0f);
        // Put distinctive values in value row steps 3..5 and gate row
        // steps 3..5 so we can see them land on paste.
        for (int s = 3; s <= 5; ++s) {
            params[se::param_index_for(se::RowKind::Value, s)] =
                0.1f + static_cast<float>(s) * 0.01f;
            params[se::param_index_for(se::RowKind::Gate, s)] =
                (s == 4) ? 1.0f : 0.0f;
        }

        auto sel = se::selection_from_anchor_tip(0, 3, 1, 5);  // 2×3
        se::SelectionClipboard clip;
        se::copy_selection(params.data(),
                           static_cast<std::uint32_t>(params.size()),
                           sel, &clip);
        check(clip.has_content, "copy_selection populates clipboard");
        check(clip.rows == 2 && clip.cols == 3,
              "clipboard dimensions match selection");
        // Row 0, col 1 maps to (row=0, step=4) → value 0.14
        check(clip.values[0 * 3 + 1] > 0.139f && clip.values[0 * 3 + 1] < 0.141f,
              "clipboard captured value at (0,4)");
        check(clip.values[1 * 3 + 1] == 1.0f,
              "clipboard captured gate=1 at (1,4)");

        // Paste at origin (0, 10) and confirm 6 set_param calls land on
        // the right names.
        CaptureCtx sink;
        const auto commands = make_commands(sink);
        const bool ok = se::paste_selection(commands, clip, 0, 10);
        check(ok, "paste_selection returns true for valid write");
        check(sink.calls.size() == 6u,
              "paste writes 6 set_param calls for 2×3 clipboard");

        // Spot-check destinations: value row at origin column 1 → step 11 → step_value_11
        bool found_value_11 = false, found_gate_11 = false;
        for (const auto& c : sink.calls) {
            if (c.name == "step_value_11") found_value_11 = true;
            if (c.name == "step_gate_11"  && c.value == 1.0f) found_gate_11 = true;
        }
        check(found_value_11, "paste emitted step_value_11");
        check(found_gate_11,  "paste emitted step_gate_11 = 1.0");
    }

    // --- paste_selection rejects empty clipboard ---
    {
        CaptureCtx sink;
        const auto commands = make_commands(sink);
        se::SelectionClipboard empty;
        check(!se::paste_selection(commands, empty, 0, 0),
              "paste of empty clipboard returns false");
        check(sink.calls.empty(),
              "paste of empty clipboard emits no commands");
    }

    // --- paste_selection clips out-of-range cells ---
    {
        // Build a 1×4 clipboard and paste at step 30 — cols 30..33 → only
        // 30 and 31 are in range, 32 and 33 should be clipped.
        std::vector<float> params(80, 0.0f);
        params[se::param_index_for(se::RowKind::Value, 10)] = 0.25f;
        params[se::param_index_for(se::RowKind::Value, 11)] = 0.50f;
        params[se::param_index_for(se::RowKind::Value, 12)] = 0.75f;
        params[se::param_index_for(se::RowKind::Value, 13)] = 1.00f;
        auto sel = se::selection_from_anchor_tip(0, 10, 0, 13);
        se::SelectionClipboard clip;
        se::copy_selection(params.data(),
                           static_cast<std::uint32_t>(params.size()),
                           sel, &clip);
        check(clip.cols == 4, "1×4 clipboard built");

        CaptureCtx sink;
        const auto commands = make_commands(sink);
        se::paste_selection(commands, clip, 0, 30);
        check(sink.calls.size() == 2u,
              "paste at step 30 writes only the 2 cells in range (30, 31)");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
