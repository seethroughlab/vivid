// ADR-0048: the clip/sample editor control substrate — pure hit-test geometry. Proves a point maps to
// the right segmented cell / stepper part, so draw and hit share one bounded Rect (no magic-offset
// mismatch like the old header controls). The geometry lives in ui/layout.h (wgpu-free); the drawing
// side (editor_controls.h) needs a GPU renderer and is verified visually, not here.
#include "ui/layout.h"
#include "test_helpers.h"
#include <cstdint>

using vivid::ui::Rect;
using vivid::ui::segmented_hit;
using vivid::ui::stepper_hit;
using vivid::ui::sampler_playhead_norm;

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

    // ---- sampler_playhead_norm: concatenated-region position -> SOURCE-normalized position ----
    // One region = a melodic trim: the old inN + ph*(outN-inN) behaviour, unchanged.
    {
        const uint32_t s1[] = { 200 }, e1[] = { 600 };   // source of 1000 frames, played window [0.2,0.6]
        CHECK_NEAR(sampler_playhead_norm(s1, e1, 1, 0.0,  1000), 0.20, 1e-9);
        CHECK_NEAR(sampler_playhead_norm(s1, e1, 1, 0.5,  1000), 0.40, 1e-9);
        CHECK_NEAR(sampler_playhead_norm(s1, e1, 1, 1.0,  1000), 0.60, 1e-9);
    }
    // A drum rack whose slices do NOT tile the source: head/tail and the gap must be skipped, so a
    // playhead halfway through the CONCATENATION lands at the start of the second slice, not at 0.5.
    {
        const uint32_t s2[] = { 100, 600 }, e2[] = { 300, 800 };   // two 200-frame slices of a 1000 source
        CHECK_NEAR(sampler_playhead_norm(s2, e2, 2, 0.0,  1000), 0.10, 1e-9);   // start of slice 1
        CHECK_NEAR(sampler_playhead_norm(s2, e2, 2, 0.25, 1000), 0.20, 1e-9);   // mid slice 1
        CHECK_NEAR(sampler_playhead_norm(s2, e2, 2, 0.5,  1000), 0.60, 1e-9);   // jumps the gap → slice 2 start
        CHECK_NEAR(sampler_playhead_norm(s2, e2, 2, 0.75, 1000), 0.70, 1e-9);   // mid slice 2
        CHECK_NEAR(sampler_playhead_norm(s2, e2, 2, 1.0,  1000), 0.80, 1e-9);   // end of slice 2, not 1.0
    }
    // Degenerate inputs never draw a bogus playhead.
    {
        const uint32_t s3[] = { 0 }, e3[] = { 0 };
        CHECK(sampler_playhead_norm(s3, e3, 1, 0.5, 1000) < 0.0);   // zero-length region
        CHECK(sampler_playhead_norm(s3, e3, 0, 0.5, 1000) < 0.0);   // no regions
        CHECK(sampler_playhead_norm(nullptr, nullptr, 1, 0.5, 1000) < 0.0);
        const uint32_t s4[] = { 0 }, e4[] = { 100 };
        CHECK(sampler_playhead_norm(s4, e4, 1, 0.5, 0) < 0.0);      // unknown source length
        CHECK(sampler_playhead_norm(s4, e4, 1, -1.0, 1000) < 0.0);  // nothing sounding
    }

    return vivid::test::summary("test_editor_controls");
}
