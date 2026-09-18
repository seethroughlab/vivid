// Headless tests for the ADR-0064 Review workspace geometry (ui/review_geom.h): the one function draw
// AND input call, so a rect that is drawn is exactly the rect that is hit. Checks: everything lands
// inside the window below the transport bar; the two columns do not overlap; source tiles are
// side-by-side, equal, non-overlapping, 16:9-or-shorter; each candidate's four action buttons sit
// under its own tile and partition its width; the queue rows fit their panel; hit-testing round-trips
// (a point inside tile i reports i, a point in the gutter reports -1); and the layout degrades sanely
// on a small window (tiles shrink instead of pushing the comment box off-screen).
#include "ui/review_geom.h"
#include "test_helpers.h"

#include <cmath>

using namespace vivid::ui;

static bool inside(const Rect& a, const Rect& b) {   // a inside b (with 0.5px slack)
    return a.x >= b.x - 0.5f && a.y >= b.y - 0.5f && a.x + a.w <= b.x + b.w + 0.5f && a.y + a.h <= b.y + b.h + 0.5f;
}
static bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void check_layout(int W, int H, int n_sources, int n_queue) {
    const ReviewGeom g = review_geom(W, H, n_sources, n_queue, 3, 2);
    const Rect win{ 0.f, kTopBarH, static_cast<float>(W), static_cast<float>(H) - kTopBarH };
    CHECK(inside(g.left, win) && inside(g.main, win));
    CHECK(!overlap(g.left, g.main));
    CHECK(g.main.w > 200.f);
    // Left column panels stack without overlap, inside the column.
    CHECK(inside(g.current, g.left) && inside(g.since, g.left) && inside(g.brief, g.left));
    CHECK(!overlap(g.current, g.since) && !overlap(g.since, g.brief) && !overlap(g.brief, g.queue));
    CHECK(inside(g.queue, g.left));
    // Main column pieces inside the column, top to bottom.
    CHECK(inside(g.status_strip, g.main) && inside(g.header, g.main));
    CHECK(g.header.y >= g.status_strip.y + g.status_strip.h);
    CHECK(static_cast<int>(g.tiles.size()) == n_sources && g.tile_labels.size() == g.tiles.size());
    for (std::size_t i = 0; i < g.tiles.size(); ++i) {
        const Rect& t = g.tiles[i];
        CHECK(inside(t, g.main));
        CHECK(t.y >= g.header.y + g.header.h);
        CHECK(t.h <= t.w * 9.f / 16.f + 1.f);                     // 16:9 or letterboxed shorter
        CHECK(std::fabs(t.w - g.tiles[0].w) < 0.5f);              // equal widths
        CHECK(g.tile_labels[i].y == t.y + t.h && g.tile_labels[i].x == t.x && g.tile_labels[i].w == t.w);
        if (i > 0) { CHECK(!overlap(t, g.tiles[i - 1])); CHECK(t.x > g.tiles[i - 1].x + g.tiles[i - 1].w); }
        // Actions partition the tile's width, directly below the label bar + transport row.
        CHECK(g.act_use[i].x == t.x);
        CHECK(std::fabs((g.act_dismiss[i].x + g.act_dismiss[i].w) - (t.x + t.w)) < 0.5f);
        CHECK(g.act_use[i].x + g.act_use[i].w <= g.act_keep[i].x && g.act_keep[i].x + g.act_keep[i].w <= g.act_revise[i].x
              && g.act_revise[i].x + g.act_revise[i].w <= g.act_dismiss[i].x);
        CHECK(g.act_use[i].y > g.play.y);
        CHECK(inside(g.act_dismiss[i], g.main));
    }
    // Transport row: play, scrub, position, loop, level — one row, left to right, inside the column.
    CHECK(inside(g.play, g.main) && inside(g.scrub, g.main) && inside(g.loop_btn, g.main) && inside(g.level_btn, g.main));
    CHECK(g.scrub.x >= g.play.x + g.play.w && g.position.x >= g.scrub.x + g.scrub.w - 0.5f);
    CHECK(g.loop_btn.x >= g.position.x + g.position.w - 0.5f && g.level_btn.x >= g.loop_btn.x + g.loop_btn.w);
    CHECK(g.scrub.w > 40.f);
    // Decisions + comment + feedback below the actions, inside the column.
    CHECK(inside(g.neither, g.main) && inside(g.open_create, g.main) && inside(g.comment_box, g.main) && inside(g.comment_send, g.main));
    CHECK(!overlap(g.neither, g.open_create) && !overlap(g.comment_box, g.comment_send));
    CHECK(g.comment_box.y > g.neither.y && g.feedback.y >= g.comment_box.y + g.comment_box.h);
    CHECK(inside(g.feedback, g.main) || g.feedback.h == 0.f);
    // Queue rows fit the panel content rect.
    CHECK(g.queue_rows_visible <= n_queue);
    for (int i = 0; i < g.queue_rows_visible; ++i) CHECK(inside(g.queue_row(i), g.queue));
    // Hit-testing round-trips.
    for (std::size_t i = 0; i < g.tiles.size(); ++i) {
        const Rect& t = g.tiles[i];
        CHECK(g.tile_at(t.x + t.w * 0.5, t.y + t.h * 0.5) == static_cast<int>(i));
        CHECK(g.tile_at(g.tile_labels[i].x + 3.0, g.tile_labels[i].y + 3.0) == static_cast<int>(i));   // the label bar counts
    }
    if (g.tiles.size() > 1) CHECK(g.tile_at(g.tiles[0].x + g.tiles[0].w + 2.0, g.tiles[0].y + 5.0) == -1);   // the gutter
    CHECK(g.tile_at(g.left.x + 5.0, g.left.y + 5.0) == -1);
    if (g.queue_rows_visible > 0) {
        const Rect r0 = g.queue_row(0);
        CHECK(g.queue_row_at(r0.x + 5.0, r0.y + 5.0) == 0);
    }
    CHECK(g.queue_row_at(g.main.x + 5.0, g.main.y + 5.0) == -1);
}

static void test_typical_and_edge_sizes() {
    check_layout(1440, 900, 3, 4);    // baseline + A + B on a laptop
    check_layout(1920, 1080, 3, 1);
    check_layout(2200, 1350, 4, 12);  // four sources, a long queue
    check_layout(1280, 800, 2, 0);    // baseline + one candidate, empty queue
    check_layout(1280, 800, 1, 2);    // a review with no candidates still lays out
    check_layout(640, 480, 3, 3);     // the minimum window: tiles shrink, nothing off-screen
}

static void test_small_window_keeps_comment_on_screen() {
    const ReviewGeom g = review_geom(640, 480, 3, 3, 6, 9);
    CHECK(g.comment_box.y + g.comment_box.h <= 480.f + 0.5f);
    CHECK(g.tiles[0].h >= 60.f);   // width-limited at 640 wide (3 tiles of ~122px): small but never collapsed
}

static void test_workspace_switch_rect() {
    const Rect b = workspace_switch_rect();
    CHECK(b.y >= 0.f && b.y + b.h <= kTopBarH);
    CHECK(b.x > sidebar_toggle_rect().x + sidebar_toggle_rect().w);   // right of the sidebar toggle
    CHECK(b.x + b.w < transport_play_rect().x);                       // left of the play button
    CHECK(segmented_hit(b, 2, b.x + 5.0, b.y + 5.0) == 0);
    CHECK(segmented_hit(b, 2, b.x + b.w - 5.0, b.y + 5.0) == 1);
    CHECK(segmented_hit(b, 2, b.x - 5.0, b.y + 5.0) == -1);
}

int main() {
    test_typical_and_edge_sizes();
    test_small_window_keeps_comment_on_screen();
    test_workspace_switch_rect();
    return vivid::test::summary("test_review_geom");
}
