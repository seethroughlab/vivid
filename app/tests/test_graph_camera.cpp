// Headless unit test for the shared graph CAMERA (app/src/ui/node_view.h): the pure world<->screen
// transform plus the pan / zoom-around-cursor / reset gesture math that GraphCanvas forwards to and both
// node editors drive (ADR-0023 #1/#3d). Pins the transform round-trip, the "world point stays under the
// cursor" zoom invariant, the shared zoom clamp, and reset-to-fitted — the interaction math the two
// editors now share, so a subtle change to it can't silently drift one surface from the other.
#include "ui/node_view.h"
#include "test_helpers.h"

using vivid::ui::NodeView;

static void test_transform_roundtrip() {
    NodeView v{ 12.f, -30.f, 1.5f };
    // to_screen(to_world(p)) == p for any screen point.
    double wx, wy; v.to_world(200.0, 140.0, wx, wy);
    double sx, sy; v.to_screen(wx, wy, sx, sy);
    CHECK_NEAR(sx, 200.0, 1e-6);
    CHECK_NEAR(sy, 140.0, 1e-6);
    // screen = world*scale + offset.
    v.to_screen(10.0, 4.0, sx, sy);
    CHECK_NEAR(sx, 12.0 + 10.0 * 1.5, 1e-6);
    CHECK_NEAR(sy, -30.0 + 4.0 * 1.5, 1e-6);
}

static void test_pan() {
    NodeView v{ 10.f, 20.f, 2.f };
    v.pan(5.f, -8.f);
    CHECK_NEAR(v.ox, 15.0, 1e-6);
    CHECK_NEAR(v.oy, 12.0, 1e-6);
    CHECK_NEAR(v.scale, 2.0, 1e-6);   // pan never touches scale
}

static void test_zoom_keeps_world_point_under_cursor() {
    NodeView v{ 40.f, -15.f, 1.2f };
    const double cx = 300.0, cy = 220.0;
    double wx0, wy0; v.to_world(cx, cy, wx0, wy0);
    v.zoom_at(cx, cy, 1.7f);
    CHECK_NEAR(v.scale, 1.2 * 1.7, 1e-5);   // scale multiplied by the factor
    double wx1, wy1; v.to_world(cx, cy, wx1, wy1);
    CHECK_NEAR(wx1, wx0, 1e-3);             // the same world point is still under the cursor
    CHECK_NEAR(wy1, wy0, 1e-3);
    // Zooming back out keeps it fixed too.
    v.zoom_at(cx, cy, 0.5f);
    double wx2, wy2; v.to_world(cx, cy, wx2, wy2);
    CHECK_NEAR(wx2, wx0, 1e-3);
    CHECK_NEAR(wy2, wy0, 1e-3);
}

static void test_zoom_clamp() {
    // The shared range is [0.35, 4.0] for BOTH editors (ADR-0023 #3d).
    CHECK_NEAR(NodeView::kMinZoom, 0.35, 1e-6);
    CHECK_NEAR(NodeView::kMaxZoom, 4.0, 1e-6);
    NodeView v{ 0.f, 0.f, 1.f };
    v.zoom_at(0, 0, 100.f);                          // way past max
    CHECK_NEAR(v.scale, NodeView::kMaxZoom, 1e-6);   // clamped to 4.0
    v.zoom_at(0, 0, 0.0001f);                        // way past min
    CHECK_NEAR(v.scale, NodeView::kMinZoom, 1e-6);   // clamped to 0.35
}

static void test_reset() {
    NodeView v{ 123.f, 456.f, 2.75f };
    v.reset_to(8.f, 674.f);
    CHECK_NEAR(v.ox, 8.0, 1e-6);
    CHECK_NEAR(v.oy, 674.0, 1e-6);
    CHECK_NEAR(v.scale, 1.0, 1e-6);   // the fitted view is identity scale
}

int main() {
    test_transform_roundtrip();
    test_pan();
    test_zoom_keeps_world_point_under_cursor();
    test_zoom_clamp();
    test_reset();
    return vivid::test::summary("test_graph_camera");
}
