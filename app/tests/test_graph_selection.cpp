// Headless unit test for the shared graph multi-selection (app/src/ui/graph_selection.h): the
// stable-id selection set + the pure marquee-intersect math both node editors drive (ADR-0033 P1).
// Pins the set invariants (replace/add/toggle/primary), the rects_overlap predicate, and
// resolve_marquee (replace vs additive, inverted-corner normalization, id-keying) — the interaction
// math that must behave identically in the visuals and audio graphs, so a subtle change can't drift.
#include "ui/graph_selection.h"
#include "test_helpers.h"

using vivid::ui::GraphSelection;
using vivid::ui::SelItem;
using vivid::ui::Rect;
using vivid::ui::rects_overlap;
using vivid::ui::normalize_rect;

static void test_rects_overlap() {
    Rect a{ 0, 0, 10, 10 };
    CHECK(rects_overlap(a, Rect{ 5, 5, 10, 10 }));      // partial overlap
    CHECK(rects_overlap(a, Rect{ 2, 2, 3, 3 }));        // fully contained
    CHECK(rects_overlap(Rect{ 2, 2, 3, 3 }, a));        // symmetric
    CHECK(!rects_overlap(a, Rect{ 10, 0, 5, 5 }));      // touching far edge = no overlap (half-open)
    CHECK(!rects_overlap(a, Rect{ 20, 20, 5, 5 }));     // disjoint
    CHECK(!rects_overlap(a, Rect{ -5, 0, 5, 10 }));     // touching near edge = no overlap
}

static void test_normalize_rect() {
    Rect n = normalize_rect(Rect{ 30, 40, -20, -15 });   // dragged up-left
    CHECK_NEAR(n.x, 10.0, 1e-6);
    CHECK_NEAR(n.y, 25.0, 1e-6);
    CHECK_NEAR(n.w, 20.0, 1e-6);
    CHECK_NEAR(n.h, 15.0, 1e-6);
}

static void test_set_ops() {
    GraphSelection s;
    CHECK(s.empty());
    CHECK(s.primary() == -1);

    s.replace(7);
    CHECK(s.size() == 1);
    CHECK(s.contains(7));
    CHECK(s.primary() == 7);

    s.add(3);
    CHECK(s.size() == 2);
    CHECK(s.contains(3));
    CHECK(s.primary() == 3);                 // add re-anchors primary

    s.toggle(3);                             // remove the primary
    CHECK(!s.contains(3));
    CHECK(s.size() == 1);
    CHECK(s.primary() == 7);                 // re-anchored to the remaining member

    s.toggle(9);                             // add via toggle
    CHECK(s.contains(9));
    CHECK(s.primary() == 9);

    s.set_primary(7);                        // re-anchor without changing membership
    CHECK(s.primary() == 7);
    s.set_primary(42);                       // not a member -> ignored
    CHECK(s.primary() == 7);

    s.replace(-1);                           // clear via replace(-1)
    CHECK(s.empty());
    CHECK(s.primary() == -1);
}

static void test_marquee_replace() {
    // Non-contiguous ids prove the set is id-keyed, not index-keyed.
    std::vector<SelItem> nodes = {
        { 2, { 0,   0,  10, 10 } },
        { 5, { 100, 0,  10, 10 } },
        { 9, { 50,  50, 10, 10 } },
    };
    GraphSelection s;
    s.replace(5);                            // prior selection that replace-marquee should drop

    s.resolve_marquee(nodes, Rect{ -5, -5, 70, 70 }, /*additive=*/false);
    CHECK(s.contains(2));                     // inside
    CHECK(s.contains(9));                     // inside
    CHECK(!s.contains(5));                    // outside AND prior selection cleared
    CHECK(s.size() == 2);
    CHECK(s.primary() >= 0 && s.contains(s.primary()));
}

static void test_marquee_additive_and_inverted() {
    std::vector<SelItem> nodes = {
        { 2, { 0,   0,  10, 10 } },
        { 5, { 100, 0,  10, 10 } },
        { 9, { 50,  50, 10, 10 } },
    };
    GraphSelection s;
    s.replace(5);                            // keep this across an additive marquee

    // Inverted corners (dragged up-left) around node 2 only, additive.
    s.resolve_marquee(nodes, Rect{ 12, 12, -18, -18 }, /*additive=*/true);
    CHECK(s.contains(5));                     // preserved (additive)
    CHECK(s.contains(2));                     // newly selected despite inverted rect
    CHECK(!s.contains(9));
    CHECK(s.size() == 2);
}

static void test_marquee_empty_leaves_nothing() {
    std::vector<SelItem> nodes = { { 2, { 0, 0, 10, 10 } } };
    GraphSelection s;
    s.replace(2);
    s.resolve_marquee(nodes, Rect{ 500, 500, 10, 10 }, /*additive=*/false);
    CHECK(s.empty());                         // marquee hit nothing, non-additive cleared prior
    CHECK(s.primary() == -1);
}

int main() {
    test_rects_overlap();
    test_normalize_rect();
    test_set_ops();
    test_marquee_replace();
    test_marquee_additive_and_inverted();
    test_marquee_empty_leaves_nothing();
    return vivid::test::summary("test_graph_selection");
}
