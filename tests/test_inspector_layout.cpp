#include "test_helpers.h"
#include "ui/inspector/inspector_layout.h"
#include <cstdio>

using namespace vivid::ui;

static constexpr float kTwoUpColW = (kInspContentW - kInspColGap) / 2.0f;

static InspectorLayout make_layout(float start_y = 0.0f) {
    InspectorLayout l;
    l.base_x = 16.0f;
    l.x = 16.0f;
    l.y = start_y;
    l.full_w = kInspContentW;
    l.col_w = kInspContentW;
    return l;
}

static ParamLayoutRequest make_request(uint8_t columns, uint8_t col_index,
                                       VividDisplayHint hint,
                                       VividParamType type,
                                       uint32_t choice_count = 0,
                                       bool metadata_heavy = false,
                                       bool long_label = false) {
    ParamLayoutRequest req;
    req.columns = columns;
    req.col_index = col_index;
    req.hint = hint;
    req.type = type;
    req.choice_count = choice_count;
    req.metadata_heavy = metadata_heavy;
    req.long_label = long_label;
    return req;
}

int main() {
    {
        std::fprintf(stderr, "\n=== Test 1: Full-width params ===\n");
        auto layout = make_layout();
        auto plan = layout.plan_param(make_request(0, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT));
        check(plan.row_mode == RowMode::kFull, "full-width plan selected");
        layout.begin_param(plan);
        check_float(layout.col_w, kInspContentW, "col_w == kInspContentW");
        check_float(layout.x, layout.base_x, "x == base_x");
        check(layout.row_columns == 0, "row_columns == 0");
    }

    {
        std::fprintf(stderr, "\n=== Test 2: Two-up compact numeric passthrough ===\n");
        auto layout = make_layout();
        auto lhs = make_request(2, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT);
        auto rhs = make_request(2, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT);
        check(InspectorLayout::requests_form_two_up_pair(lhs, rhs), "adjacent compact pair accepted");
        auto plan0 = InspectorLayout::two_up_plan(0);
        check(plan0.row_mode == RowMode::kMultiUp, "col 0 uses multi-up");
        check(plan0.compact, "col 0 marked compact");
        check(!plan0.allow_secondary_text, "compact row suppresses secondary text");
        layout.begin_param(plan0);
        check_float(layout.col_w, kTwoUpColW, "col 0 width");
        check_float(layout.x, layout.base_x, "col 0 x");
        layout.end_param(20.0f);

        auto plan1 = InspectorLayout::two_up_plan(1);
        check(plan1.row_mode == RowMode::kMultiUp, "col 1 uses multi-up");
        layout.begin_param(plan1);
        check_float(layout.x, layout.base_x + kTwoUpColW + kInspColGap, "col 1 x");
        layout.end_param(20.0f);
    }

    {
        std::fprintf(stderr, "\n=== Test 3: Metadata-heavy two-up request collapses to full ===\n");
        auto lhs = make_request(2, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0, true);
        auto rhs = make_request(2, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT);
        check(!InspectorLayout::requests_form_two_up_pair(lhs, rhs),
              "metadata-heavy row collapses to full");
    }

    {
        std::fprintf(stderr, "\n=== Test 4: Long-label two-up request collapses to full ===\n");
        auto lhs = make_request(2, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT, 0, false, true);
        auto rhs = make_request(2, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT);
        check(!InspectorLayout::requests_form_two_up_pair(lhs, rhs),
              "long-label row collapsed to full");
    }

    {
        std::fprintf(stderr, "\n=== Test 5: 4-col knobs form a single multi-up row ===\n");
        auto layout = make_layout(100.0f);

        auto req0 = make_request(4, 0, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT);
        auto req1 = make_request(4, 1, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT);
        auto req2 = make_request(4, 2, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT);
        auto req3 = make_request(4, 3, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT);

        ParamLayoutRequest reqs[] = {req0, req1, req2, req3};
        check(InspectorLayout::requests_form_multi_up_run(reqs, 4), "4 knobs form multi-up run");

        float expected_col_w = (kInspContentW - 3 * kInspColGap) / 4.0f;

        auto p0 = InspectorLayout::multi_up_plan(4, 0);
        auto p1 = InspectorLayout::multi_up_plan(4, 1);
        auto p2 = InspectorLayout::multi_up_plan(4, 2);
        auto p3 = InspectorLayout::multi_up_plan(4, 3);

        layout.begin_param(p0);
        float row_y = layout.y;
        check_float(layout.col_w, expected_col_w, "4-col width");
        check_float(layout.x, layout.base_x, "col 0 x");
        layout.end_param(40.0f);

        layout.begin_param(p1);
        check_float(layout.y, row_y, "col 1 same row");
        layout.end_param(40.0f);

        layout.begin_param(p2);
        check_float(layout.y, row_y, "col 2 same row");
        layout.end_param(40.0f);

        layout.begin_param(p3);
        check_float(layout.y, row_y, "col 3 same row");
        layout.end_param(40.0f);

        layout.flush_row();
        check_float(layout.y, row_y + 40.0f, "row advanced by tallest column");
    }

    {
        std::fprintf(stderr, "\n=== Test 6: Non-knob legacy 4-col requests normalize to full ===\n");
        auto lhs = make_request(4, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT);
        auto rhs = make_request(4, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FLOAT);
        check(!InspectorLayout::requests_form_two_up_pair(lhs, rhs),
              "legacy slider normalized to full");
    }

    {
        std::fprintf(stderr, "\n=== Test 7: Dropdown/bool/file/text stay full-width ===\n");
        check(!InspectorLayout::requests_form_two_up_pair(
                  make_request(3, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_INT, 3),
                  make_request(3, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_INT, 3)),
              "dropdown full-width");
        check(!InspectorLayout::requests_form_two_up_pair(
                  make_request(3, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_BOOL),
                  make_request(3, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_BOOL)),
              "bool full-width");
        check(!InspectorLayout::requests_form_two_up_pair(
                  make_request(3, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FILE),
                  make_request(3, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_FILE)),
              "file full-width");
        check(!InspectorLayout::requests_form_two_up_pair(
                  make_request(3, 0, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_TEXT),
                  make_request(3, 1, VIVID_DISPLAY_DEFAULT, VIVID_PARAM_TEXT)),
              "text full-width");
    }

    {
        std::fprintf(stderr, "\n=== Test 8: Compound widgets stay compound/full-width ===\n");
        auto layout = make_layout();
        auto xy = layout.plan_param(make_request(0, 0, VIVID_DISPLAY_XY_PAD, VIVID_PARAM_FLOAT));
        auto color = layout.plan_param(make_request(0, 0, VIVID_DISPLAY_COLOR, VIVID_PARAM_FLOAT));
        check(xy.row_mode == RowMode::kCompound, "xy pad uses compound row");
        check(color.row_mode == RowMode::kCompound, "color swatch uses compound row");
    }

    {
        std::fprintf(stderr, "\n=== Test 9: Row flush uses tallest two-up column ===\n");
        auto layout = make_layout(10.0f);
        ParamLayoutPlan col0;
        col0.row_mode = RowMode::kTwoUp;
        col0.column = 0;
        ParamLayoutPlan col1 = col0;
        col1.column = 1;

        layout.begin_param(col0);
        layout.end_param(30.0f);
        layout.begin_param(col1);
        layout.end_param(50.0f);
        layout.flush_row();
        check_float(layout.y, 60.0f, "y advanced by tallest column");
    }

    {
        std::fprintf(stderr, "\n=== Test 10: Multi-up run validation ===\n");
        // Full 4-knob run
        ParamLayoutRequest full4[] = {
            make_request(4, 0, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT),
            make_request(4, 1, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT),
            make_request(4, 2, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT),
            make_request(4, 3, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT),
        };
        check(InspectorLayout::requests_form_multi_up_run(full4, 4), "full 4-knob run accepted");

        // 3-knob run
        ParamLayoutRequest run3[] = {
            make_request(3, 0, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT),
            make_request(3, 1, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT),
            make_request(3, 2, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT),
        };
        check(InspectorLayout::requests_form_multi_up_run(run3, 3), "3-knob run accepted");

        // Incomplete run (3 of 4) rejected
        check(!InspectorLayout::requests_form_multi_up_run(full4, 3), "incomplete 4-col run rejected");

        // Gap in col_index rejected
        ParamLayoutRequest gap[] = {
            make_request(4, 0, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT),
            make_request(4, 2, VIVID_DISPLAY_KNOB, VIVID_PARAM_FLOAT),
        };
        check(!InspectorLayout::requests_form_multi_up_run(gap, 2), "col_index gap rejected");
    }

    {
        std::fprintf(stderr, "\n=== Test 11: Stray right-column begin_param recovers instead of overlapping ===\n");
        auto layout = make_layout(32.0f);
        auto full = InspectorLayout::full_plan();
        layout.begin_param(full);
        layout.end_param(24.0f);
        auto rogue = InspectorLayout::two_up_plan(1);
        layout.begin_param(2, 1);
        check_float(layout.y, 56.0f, "stray right-column row starts from current y");
        layout.end_param(20.0f);
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
