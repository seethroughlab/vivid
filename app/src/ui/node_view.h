#pragma once
#include <algorithm>   // std::clamp

// ADR-0023 — the shared graph CAMERA: a pure world<->screen pan/zoom transform plus the gesture math
// both node editors drive it with. Deliberately dependency-free (no renderer / GPU / marks) so it is a
// standalone, headlessly-testable unit — the interaction math lives in exactly one place and can be
// pinned by a unit test (see tests/test_graph_canvas_camera.cpp). `GraphCanvas` owns one instance and
// forwards pan/zoom/reset to it; the editors reach it via `canvas_.view()`.
namespace vivid::ui {

struct NodeView {
    float ox = 0.f, oy = 0.f, scale = 1.f;   // screen = world*scale + (ox,oy)

    // One shared zoom range for both editors (ADR-0023 #3d): the same max keeps the two surfaces
    // feeling identical.
    static constexpr float kMinZoom = 0.35f, kMaxZoom = 4.0f;

    void to_world(double sx, double sy, double& wx, double& wy) const {
        wx = (sx - ox) / scale; wy = (sy - oy) / scale;
    }
    void to_screen(double wx, double wy, double& sx, double& sy) const {
        sx = ox + wx * scale; sy = oy + wy * scale;
    }
    void pan(float dx, float dy) { ox += dx; oy += dy; }

    // Zoom by `factor` about the screen point (sx,sy), keeping that world point fixed under the cursor;
    // the scale is clamped to [kMinZoom, kMaxZoom].
    void zoom_at(double sx, double sy, float factor) {
        double wx, wy; to_world(sx, sy, wx, wy);
        scale = std::clamp(scale * factor, kMinZoom, kMaxZoom);
        ox = static_cast<float>(sx) - static_cast<float>(wx) * scale;
        oy = static_cast<float>(sy) - static_cast<float>(wy) * scale;
    }

    // Fit the origin to (x,y) at identity scale — the "reset to the fitted view" gesture.
    void reset_to(float x, float y) { ox = x; oy = y; scale = 1.f; }
};

}  // namespace vivid::ui
