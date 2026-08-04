// ADR-0048: the clip/sample editor control substrate — pure hit-test geometry. Proves a point maps to
// the right segmented cell / stepper part, so draw and hit share one bounded Rect (no magic-offset
// mismatch like the old header controls). The geometry lives in ui/layout.h (wgpu-free); the drawing
// side (editor_controls.h) needs a GPU renderer and is verified visually, not here.
#include "ui/layout.h"
#include "test_helpers.h"

using vivid::ui::Rect;
using vivid::ui::segmented_hit;
using vivid::ui::stepper_hit;

int main() {
    // ---- segmented_hit: 4 cells across a 200-wide box at x=100 (cell width 50) ----
    const Rect seg{ 100.f, 10.f, 200.f, 24.f };
    CHECK(segmented_hit(seg, 4, 110, 20) == 0);   // first cell
    CHECK(segmented_hit(seg, 4, 160, 20) == 1);   // second (150..200)
    CHECK(segmented_hit(seg, 4, 210, 20) == 2);   // third  (200..250)
    CHECK(segmented_hit(seg, 4, 299, 20) == 3);   // last   (250..300)
    CHECK(segmented_hit(seg, 4, 100, 20) == 0);   // exact left edge = first cell
    CHECK(segmented_hit(seg, 4,  99, 20) == -1);  // left of the box
    CHECK(segmented_hit(seg, 4, 301, 20) == -1);  // right of the box
    CHECK(segmented_hit(seg, 4, 160,  9) == -1);  // above the box
    CHECK(segmented_hit(seg, 4, 160, 35) == -1);  // below the box
    // boundary lands in the higher cell (half-open), and never overflows the last index
    CHECK(segmented_hit(seg, 4, 150, 20) == 1);
    CHECK(segmented_hit(seg, 2, 299, 20) == 1);   // works for any n
    CHECK(segmented_hit(seg, 0, 160, 20) == -1);  // degenerate

    // ---- stepper_hit: [ − | body | + ], ± buttons are h(=24) wide at each end ----
    const Rect stp{ 100.f, 10.f, 120.f, 24.f };   // dec 100..124, body 124..196, inc 196..220
    CHECK(stepper_hit(stp, 105, 20) == -1);   // in the − button
    CHECK(stepper_hit(stp, 160, 20) ==  0);   // in the body
    CHECK(stepper_hit(stp, 210, 20) == +1);   // in the + button
    CHECK(stepper_hit(stp, 124, 20) ==  0);   // dec ends at x+h → body
    CHECK(stepper_hit(stp,  90, 20) ==  0);   // outside the box → no-op (0)
    CHECK(stepper_hit(stp, 160, 40) ==  0);   // below the box → no-op

    return vivid::test::summary("test_editor_controls");
}
