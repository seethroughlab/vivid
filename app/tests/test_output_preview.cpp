// Headless unit test for OutputPreview::clamp (app/src/app/output_preview.h) — the pure geometry that
// keeps the floating output-preview panel inside the visuals column (ADR-0014 / ADR-0025). This is the
// safety-critical bit: an unclamped panel pushes the blit rect off the framebuffer and wgpu aborts the
// process on an out-of-bounds scissor, so pin the placement / width-clamp / aspect-giveback / position
// rules. layout.h is GPU-free, so this runs headlessly.
#include "app/output_preview.h"
#include "test_helpers.h"

using vivid::OutputPreview;
using vivid::ui::Rect;

static void test_first_placement_parks_bottom_right() {
    OutputPreview p;                       // x = -1 (not placed), w = 420, aspect 16:9
    p.clamp(Rect{ 0.f, 0.f, 1000.f, 800.f });
    // body_h = 420 / (16/9) = 236.25; panel h = 22 + 236.25 = 258.25.
    CHECK_NEAR(p.w, 420.0, 1e-3);
    CHECK_NEAR(p.x, 1000.0 - 420.0 - 8.0, 1e-3);       // col.w - panel.w - pad
    CHECK_NEAR(p.y, 800.0 - 258.25 - 8.0, 1e-3);       // col.h - panel.h - pad
}

static void test_width_clamps_to_column() {
    OutputPreview narrow; narrow.clamp(Rect{ 0.f, 0.f, 300.f, 800.f });
    CHECK_NEAR(narrow.w, 300.0 - 2.0 * 8.0, 1e-3);     // clamped to col.w - 2*pad = 284
    OutputPreview tiny;   tiny.clamp(Rect{ 0.f, 0.f, 100.f, 800.f });
    CHECK_NEAR(tiny.w, 160.0, 1e-3);                   // clamped to kPreviewMinW
}

static void test_tall_aspect_gives_width_back() {
    OutputPreview p; p.out_aspect = 9.f / 16.f;        // portrait: a fixed width makes a very tall panel
    p.clamp(Rect{ 0.f, 0.f, 1000.f, 600.f });
    // width first clamps to 420 (fits col.w); panel h = 22 + 420*16/9 = 768.7 > col.h-2*pad = 584,
    // so width is given back: w = (584 - 22) * (9/16) = 316.125.
    CHECK_NEAR(p.w, (584.0 - 22.0) * (9.0 / 16.0), 1e-3);
    // and once given back the panel now fits the column height.
    CHECK(22.0 + p.w / (9.0 / 16.0) <= 584.0 + 1e-3);
}

static void test_position_clamps_inside_column() {
    OutputPreview p; p.x = 2000.f; p.y = 2000.f; p.w = 200.f;   // placed, dragged far off bottom-right
    p.clamp(Rect{ 100.f, 50.f, 800.f, 600.f });
    // panel h = 22 + 200*9/16 = 134.5; x/y clamp to keep the panel inside the column.
    CHECK_NEAR(p.w, 200.0, 1e-3);                       // 200 fits, unchanged
    CHECK_NEAR(p.x, 100.0 + 800.0 - 200.0, 1e-3);       // col.x + col.w - panel.w = 700
    CHECK_NEAR(p.y, 50.0 + 600.0 - 134.5, 1e-3);        // col.y + col.h - panel.h = 515.5
}

static void test_degenerate_column_is_noop() {
    OutputPreview p; p.x = 42.f; p.y = 7.f; p.w = 200.f;
    p.clamp(Rect{ 0.f, 0.f, 0.f, 0.f });               // no column -> leave state untouched
    CHECK_NEAR(p.x, 42.0, 1e-6);
    CHECK_NEAR(p.y, 7.0, 1e-6);
    CHECK_NEAR(p.w, 200.0, 1e-6);
}

int main() {
    test_first_placement_parks_bottom_right();
    test_width_clamps_to_column();
    test_tall_aspect_gives_width_back();
    test_position_clamps_inside_column();
    test_degenerate_column_is_noop();
    return vivid::test::summary("test_output_preview");
}
