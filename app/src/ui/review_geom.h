#pragma once
// ADR-0064 — the Review workspace's GEOMETRY, renderer-free so it is headlessly testable
// (tests/test_review_geom.cpp) and shared by draw (ui/review_view.cpp) and input
// (app/review_workspace.cpp): both call review_geom() with the same counts, so every hit-rect is
// exactly the rect that was drawn (the layout.h discipline).
#include "ui/layout.h"

#include <vector>

namespace vivid::ui {

// All hit-rects for one frame. Computed from the window size + counts; both draw and input call it.
struct ReviewGeom {
    Rect left, main;
    Rect current, since, brief, queue;        // left-column panels
    Rect status_strip, header;
    std::vector<Rect> tiles;                  // one per source (video area)
    std::vector<Rect> tile_labels;            // label bar under each tile
    Rect play, scrub, loop_btn, level_btn, position;
    std::vector<Rect> act_use, act_keep, act_revise, act_dismiss;   // per source (empty rect for baseline)
    Rect neither, open_create;
    Rect comment_box, comment_send;
    Rect feedback;
    float queue_row_h = 40.f;
    int   queue_rows_visible = 0;
    Rect queue_row(int i) const;
    // Which source tile (or -1) / which queue row (or -1) is at a point.
    int tile_at(double mx, double my) const;
    int queue_row_at(double mx, double my) const;
};
ReviewGeom review_geom(int win_w, int win_h, int n_sources, int n_queue, int n_since, int n_brief);

// The transport-bar workspace switch [ Create | Review ] (shared by both workspaces).
inline Rect workspace_switch_rect() { return { 126.f, 10.f, 152.f, 20.f }; }
}  // namespace vivid::ui
