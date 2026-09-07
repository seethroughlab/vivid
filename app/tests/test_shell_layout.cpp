// Headless unit test for the shell's window-relative geometry (app/src/ui/layout.h) — the two
// invariants that were violated in the running app and are invisible to a compiler:
//
//   1. The top-right status cluster: the ADR-0019 health dot and the always-on perf read-out chip
//      used to be independent right-edge anchors, and the dot ended up drawn INSIDE the chip.
//      They now share kTopRightPad, so pin "these never intersect" for a range of window widths
//      and (text-driven) chip widths.
//   2. The audio-graph pane must clear the browser sidebar. It used to hard-code x = kPaneMargin,
//      so opening the CLIPS browser put the audio node graph on top of it (and made scroll over
//      the browser zoom the graph).
//
// layout.h is pure + GPU-free, so this runs headlessly.
#include "ui/layout.h"
#include "test_helpers.h"

using vivid::ui::Rect;
using namespace vivid::ui;

// Do two axis-aligned rects share any area? (Touching edges do not count as overlap.)
static bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w &&
           a.y < b.y + b.h && b.y < a.y + a.h;
}

static void test_top_right_cluster_never_overlaps() {
    // A spread of window widths x plausible read-out widths ("-- fps" through "120 fps · 8.3 ms").
    const int   widths[]   = { 900, 1280, 1440, 1920, 2560, 3840 };
    const float text_ws[]  = { 30.f, 60.f, 90.f, 120.f, 160.f };
    for (int w : widths) {
        const Rect dot = health_dot_rect(w);
        for (float tw : text_ws) {
            const Rect chip = perf_hud_rect(w, tw);
            CHECK(!overlaps(chip, dot));
            CHECK(chip.x + chip.w <= dot.x);                 // chip sits strictly LEFT of the dot
            CHECK(dot.x + dot.w <= static_cast<float>(w));   // both stay on-screen
            CHECK(chip.x > 0.f);
            // Both vertically centred on the transport bar, so the cluster reads as one row.
            CHECK_NEAR(chip.y + chip.h * 0.5f, dot.y + dot.h * 0.5f, 1e-4);
            CHECK(dot.y + dot.h <= kTopBarH);
            CHECK(chip.y + chip.h <= kTopBarH);
        }
    }
}

static void test_perf_chip_grows_leftward() {
    // The chip is right-anchored: a longer string must extend to the LEFT, never toward the dot.
    const Rect narrow = perf_hud_rect(1440, 40.f);
    const Rect wide   = perf_hud_rect(1440, 140.f);
    CHECK_NEAR(narrow.x + narrow.w, wide.x + wide.w, 1e-4);   // same right edge
    CHECK(wide.x < narrow.x);
    CHECK_NEAR(wide.w - narrow.w, 100.0, 1e-4);               // width tracks the text exactly
}

static void test_audio_pane_unchanged_when_browser_closed() {
    // Closed browser (sidebar_w == 0) must reproduce the pre-fix geometry exactly.
    const Rect p = audio_graph_pane(700.f, 0.f, 900, 200.f, 4);
    CHECK_NEAR(p.x, kPaneMargin, 1e-4);
    CHECK_NEAR(p.w, 700.0 - 2.0 * kPaneMargin, 1e-4);
    CHECK_NEAR(p.y, mixer_y(4) + 48.0 + 16.0 + 12.0, 1e-4);
    CHECK_NEAR(p.y + p.h, dock_top(900, 200.f) - kPaneMargin, 1e-4);
}

static void test_audio_pane_clears_the_open_browser() {
    const float SW = kSidebarW;
    const Rect clips = sidebar_clips_panel(SW, 900, 200.f);
    const Rect pane  = audio_graph_pane(700.f, SW, 900, 200.f, 4);
    CHECK(!overlaps(pane, clips));
    CHECK(pane.x >= clips.x + clips.w);            // starts past the browser column
    CHECK(pane.x + pane.w <= 700.f);               // and still stops short of the splitter
    // The vertical span is untouched by the sidebar — only the horizontal axis was blind.
    const Rect closed = audio_graph_pane(700.f, 0.f, 900, 200.f, 4);
    CHECK_NEAR(pane.y, closed.y, 1e-4);
    CHECK_NEAR(pane.h, closed.h, 1e-4);
    // Its derived sub-rects follow the pane rather than the window edge.
    CHECK_NEAR(audio_pane_hdr_rect(pane).x, pane.x, 1e-4);
    CHECK(audio_pane_canvas_rect(pane).x >= clips.x + clips.w);
    CHECK(audio_pane_editor_rect(pane).x + audio_pane_editor_rect(pane).w <= pane.x + pane.w);
}

static void test_audio_pane_degenerate_window() {
    // A window narrow enough that the splitter is left of the sidebar must still yield a usable
    // (non-negative, min-clamped) rect — a negative width would blow up the scissor.
    const Rect p = audio_graph_pane(100.f, kSidebarW, 400, 120.f, 8);
    CHECK(p.w >= 48.f);
    CHECK(p.h >= 48.f);
}

int main() {
    test_top_right_cluster_never_overlaps();
    test_perf_chip_grows_leftward();
    test_audio_pane_unchanged_when_browser_closed();
    test_audio_pane_clears_the_open_browser();
    test_audio_pane_degenerate_window();
    return vivid::test::summary("test_shell_layout");
}
