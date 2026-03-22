#include "test_helpers.h"
#include "ui/inspector_layout.h"
#include <cstdio>

using namespace vivid::ui;

// Convenience: expected two-up column width at kInspContentW = 368
static constexpr float kTwoUpColW = (kInspContentW - kInspColGap) / 2.0f;  // 180.0

static InspectorLayout make_layout(float start_y = 0.0f) {
    InspectorLayout l;
    l.base_x = 16.0f;
    l.x = 16.0f;
    l.y = start_y;
    l.full_w = kInspContentW;
    l.col_w = kInspContentW;
    return l;
}

int main() {
    // =================================================================
    // Test 1: Full-width params (columns < 2)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Full-width params ===\n");
        auto layout = make_layout();
        layout.begin_param_normalized(0, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kInspContentW, "col_w == kInspContentW");
        check_float(layout.x, layout.base_x, "x == base_x");
        check(layout.row_columns == 0, "row_columns == 0 (single column)");
    }

    // =================================================================
    // Test 2: Two-up passthrough (columns == 2)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Two-up passthrough ===\n");
        auto layout = make_layout();

        layout.begin_param_normalized(2, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kTwoUpColW, "col 0: col_w == two-up width");
        check_float(layout.x, layout.base_x, "col 0: x == base_x");
        float col0_x = layout.x;
        layout.end_param(20.0f);

        layout.begin_param_normalized(2, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kTwoUpColW, "col 1: col_w == two-up width");
        check_float(layout.x, col0_x + kTwoUpColW + kInspColGap, "col 1: x offset correct");
        layout.end_param(20.0f);
    }

    // =================================================================
    // Test 3: Knobs in 4-col → two-up auto-paired (Envelope pattern)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: 4-col knobs → two-up pairs ===\n");
        auto layout = make_layout(100.0f);

        // Param 0 (KNOB, 4, 0) → two-up col 0
        layout.begin_param_normalized(4, 0, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kTwoUpColW, "knob 0: two-up width");
        check_float(layout.x, layout.base_x, "knob 0: col 0 position");
        float row1_y = layout.y;
        layout.end_param(40.0f);

        // Param 1 (KNOB, 4, 1) → two-up col 1
        layout.begin_param_normalized(4, 1, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        check_float(layout.x, layout.base_x + kTwoUpColW + kInspColGap, "knob 1: col 1 position");
        check_float(layout.y, row1_y, "knob 1: y reset to row start");
        layout.end_param(40.0f);

        // Param 2 (KNOB, 4, 2) → two-up col 0 (new row)
        layout.begin_param_normalized(4, 2, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        check_float(layout.x, layout.base_x, "knob 2: col 0 position (new row)");
        check(layout.y > row1_y, "knob 2: y advanced past first row");
        float row2_y = layout.y;
        layout.end_param(40.0f);

        // Param 3 (KNOB, 4, 3) → two-up col 1
        layout.begin_param_normalized(4, 3, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        check_float(layout.x, layout.base_x + kTwoUpColW + kInspColGap, "knob 3: col 1 position");
        check_float(layout.y, row2_y, "knob 3: y reset to row 2 start");
        layout.end_param(40.0f);
    }

    // =================================================================
    // Test 4: Mixed hints in 4-col (LFO pattern)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: Mixed 4-col (DEFAULT, KNOB, KNOB, DEFAULT) ===\n");
        auto layout = make_layout(0.0f);

        // Param 0: DEFAULT slider → full width
        layout.begin_param_normalized(4, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kInspContentW, "param 0: full width (slider)");
        layout.end_param(30.0f);
        float after_p0 = layout.y;

        // Param 1: KNOB → two-up col 0
        layout.begin_param_normalized(4, 1, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kTwoUpColW, "param 1: two-up width (knob)");
        check_float(layout.x, layout.base_x, "param 1: col 0");
        layout.end_param(40.0f);

        // Param 2: KNOB → two-up col 1
        layout.begin_param_normalized(4, 2, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kTwoUpColW, "param 2: two-up width (knob)");
        check_float(layout.x, layout.base_x + kTwoUpColW + kInspColGap, "param 2: col 1");
        layout.end_param(40.0f);

        // Param 3: DEFAULT slider → full width (flushes knob row)
        layout.begin_param_normalized(4, 3, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kInspContentW, "param 3: full width (slider)");
        check(layout.y >= after_p0 + 40.0f, "param 3: y past knob row");
        layout.end_param(30.0f);
    }

    // =================================================================
    // Test 5: Sliders in 3-col → all full width
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: 3-col sliders → full width ===\n");
        auto layout = make_layout();

        layout.begin_param_normalized(3, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kInspContentW, "slider 0: full width");
        layout.end_param(20.0f);

        layout.begin_param_normalized(3, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kInspContentW, "slider 1: full width");
        layout.end_param(20.0f);

        layout.begin_param_normalized(3, 2, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kInspContentW, "slider 2: full width");
        layout.end_param(20.0f);
    }

    // =================================================================
    // Test 6: Orphan knob (single knob then full-width param)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: Orphan knob ===\n");
        auto layout = make_layout(50.0f);

        // Single KNOB in >=3 col slot
        layout.begin_param_normalized(4, 0, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kTwoUpColW, "orphan knob: two-up width");
        check_float(layout.x, layout.base_x, "orphan knob: col 0");
        layout.end_param(40.0f);

        // Next param is full-width → flushes the incomplete knob row
        layout.begin_param_normalized(0, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kInspContentW, "next param: full width");
        check_float(layout.y, 50.0f + 40.0f, "y advanced past orphan knob row");
        layout.end_param(20.0f);
    }

    // =================================================================
    // Test 7: Dropdown in >=3 col → full width
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: Dropdown in 4-col → full width ===\n");
        auto layout = make_layout();

        // KNOB at (4, 0) → two-up col 0
        layout.begin_param_normalized(4, 0, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        check_float(layout.col_w, kTwoUpColW, "knob: two-up width");
        layout.end_param(40.0f);

        // Dropdown (choice_count > 0) at (4, 1) → full width
        layout.begin_param_normalized(4, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_INT, 3);
        check_float(layout.col_w, kInspContentW, "dropdown: full width despite 4-col");
        check(layout.two_up_next_col == 0, "two_up_next_col reset after dropdown");
        layout.end_param(20.0f);
    }

    // =================================================================
    // Test 8: Bool/File/Text in >=3 col → full width
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 8: Bool/File/Text in 3-col → full width ===\n");
        auto layout = make_layout();

        layout.begin_param_normalized(3, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_BOOL, 0);
        check_float(layout.col_w, kInspContentW, "bool: full width");
        layout.end_param(20.0f);

        layout.begin_param_normalized(3, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FILE, 0);
        check_float(layout.col_w, kInspContentW, "file: full width");
        layout.end_param(20.0f);

        layout.begin_param_normalized(3, 2, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_TEXT, 0);
        check_float(layout.col_w, kInspContentW, "text: full width");
        layout.end_param(20.0f);
    }

    // =================================================================
    // Test 9: Row flushing uses tallest column height
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 9: Row flush uses max height ===\n");
        auto layout = make_layout(10.0f);

        layout.begin_param(2, 0);
        layout.end_param(30.0f);  // col 0: 30px tall

        layout.begin_param(2, 1);
        layout.end_param(50.0f);  // col 1: 50px tall

        layout.flush_row();
        check_float(layout.y, 10.0f + 50.0f, "y advanced by tallest column (50)");
    }

    // =================================================================
    // Test 10: Control order preservation (Envelope ADSR)
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 10: Control order (ADSR) ===\n");
        auto layout = make_layout();

        // A: col 0 of first row
        layout.begin_param_normalized(4, 0, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        float a_x = layout.x;
        layout.end_param(40.0f);

        // D: col 1 of first row
        layout.begin_param_normalized(4, 1, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        float d_x = layout.x;
        layout.end_param(40.0f);

        // S: col 0 of second row
        layout.begin_param_normalized(4, 2, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        float s_x = layout.x;
        layout.end_param(40.0f);

        // R: col 1 of second row
        layout.begin_param_normalized(4, 3, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT, 0);
        float r_x = layout.x;
        layout.end_param(40.0f);

        check(a_x < d_x, "A is left of D");
        check(s_x < r_x, "S is left of R");
        check_float(a_x, s_x, "A and S same x (both col 0)");
        check_float(d_x, r_x, "D and R same x (both col 1)");
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
