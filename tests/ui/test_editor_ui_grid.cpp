// Unit tests for the second-wave widgets in src/operator_api/editor_ui.h:
// ui_step_grid (click / shift-click / drag-paint / shift-drag-extend),
// ui_drag_handle (radius hit-test + dx/dy reporting), and
// ui_scroll_region_begin/end (wheel + thumb drag).

#include "operator_api/editor_ui.h"
#include "operator_api/types.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

void noop_rect(void*, float, float, float, float, VividColor) {}
void noop_rounded_rect(void*, float, float, float, float, float, VividColor) {}
void noop_text(void*, float, float, const char*, VividColor, float) {}
void noop_line(void*, float, float, float, float, float, VividColor) {}
float fake_text_width(void*, const char* t, float s) {
    return (t ? std::strlen(t) : 0u) * 6.0f * s;
}
float fake_line_height(void*) { return 14.0f; }

struct ClipCall { float x, y, w, h; };
struct Recorder { int pushes = 0, pops = 0; std::vector<ClipCall> clips; };
void rec_push_clip(void* o, float x, float y, float w, float h) {
    auto* r = static_cast<Recorder*>(o);
    r->pushes++;
    r->clips.push_back({x, y, w, h});
}
void rec_pop_clip(void* o) { static_cast<Recorder*>(o)->pops++; }

VividDrawAPI make_draw_api(Recorder* rec) {
    VividDrawAPI d{};
    d.opaque            = rec;
    d.draw_rect         = noop_rect;
    d.draw_rounded_rect = noop_rounded_rect;
    d.draw_text         = noop_text;
    d.draw_line         = noop_line;
    d.text_width        = fake_text_width;
    d.line_height       = fake_line_height;
    d.push_clip_rect    = rec_push_clip;
    d.pop_clip_rect     = rec_pop_clip;
    return d;
}

struct Harness {
    Recorder rec;
    VividEditorContext ctx{};
    std::vector<VividEditorEvent> events;

    Harness() {
        ctx.surface_width  = 800.0f;
        ctx.surface_height = 600.0f;
        ctx.dpi_scale      = 1.0f;
        ctx.draw           = make_draw_api(&rec);
        ctx.theme.accent      = {0.3f, 0.6f, 1.0f, 1.0f};
        ctx.theme.bright_text = {1.0f, 1.0f, 1.0f, 1.0f};
        ctx.theme.dim_text    = {0.7f, 0.7f, 0.7f, 1.0f};
    }

    void click(float x, float y, bool shift = false) {
        ctx.mouse = {};
        ctx.mouse.x = x; ctx.mouse.y = y;
        ctx.mouse.left_clicked = 1;
        ctx.mouse.left_down = 1;
        ctx.mouse.shift_down = shift ? 1 : 0;
    }
    void drag(float x, float y, bool shift = false) {
        ctx.mouse = {};
        ctx.mouse.x = x; ctx.mouse.y = y;
        ctx.mouse.left_down = 1;
        ctx.mouse.shift_down = shift ? 1 : 0;
    }
    void release(float x, float y) {
        ctx.mouse = {};
        ctx.mouse.x = x; ctx.mouse.y = y;
        ctx.mouse.left_released = 1;
    }
    void idle(float x = 0.0f, float y = 0.0f) {
        ctx.mouse = {};
        ctx.mouse.x = x; ctx.mouse.y = y;
        ctx.event_count = 0;
        ctx.events = nullptr;
    }
    void wheel_at(float x, float y, float dy_ticks) {
        ctx.mouse = {};
        ctx.mouse.x = x; ctx.mouse.y = y;
        events.clear();
        VividEditorEvent e{};
        e.type = VIVID_EDITOR_EVENT_MOUSE_SCROLL;
        e.x = x; e.y = y;
        e.scroll_dy = dy_ticks;
        events.push_back(e);
        ctx.events = events.data();
        ctx.event_count = static_cast<uint32_t>(events.size());
    }
};

} // namespace

int main() {
    namespace ui = vivid::ui;
    std::fprintf(stderr, "=== Test: editor_ui grid / drag-handle / scroll ===\n\n");

    // --- grid_cell_rect splits bounds uniformly ----------------------------
    {
        ui::Rect b{10, 20, 160, 60};  // 160 wide / 60 tall
        auto cell = ui::grid_cell_rect(b, /*rows=*/6, /*cols=*/16, /*row=*/2, /*col=*/3);
        check(std::fabs(cell.w - 10.0f) < 1e-4f, "cell width = bounds.w / cols");
        check(std::fabs(cell.h - 10.0f) < 1e-4f, "cell height = bounds.h / rows");
        check(std::fabs(cell.x - (10.0f + 3 * 10.0f)) < 1e-4f, "cell.x = bounds.x + col*cw");
        check(std::fabs(cell.y - (20.0f + 2 * 10.0f)) < 1e-4f, "cell.y = bounds.y + row*ch");
    }

    // --- ui_step_grid: click reports cell + sets anchor + starts drag ------
    {
        Harness h;
        ui::GridState st;
        ui::Rect b{0, 0, 160, 60};

        // Click at (35, 25) — col = 3, row = 2 (cell 10x10 sized).
        h.click(35.0f, 25.0f);
        auto r = ui::ui_step_grid(h.ctx, b, 6, 16, 16, &st);
        check(r.cell_clicked, "click reports cell_clicked");
        check(r.clicked_row == 2 && r.clicked_col == 3, "click row=2 col=3");
        check(!r.clicked_with_shift, "no shift on plain click");
        check(r.drag_painting, "click starts drag_painting");
        check(r.drag_row == 2 && r.drag_col == 3, "drag latched to clicked cell");
        check(st.anchor_row == 2 && st.anchor_col == 3, "state anchor = clicked cell");
        check(st.drag_painting, "state.drag_painting = true after click");
    }

    // --- ui_step_grid: active_cols rejects clicks past the active range ----
    {
        Harness h;
        ui::GridState st;
        ui::Rect b{0, 0, 160, 60};
        h.click(105.0f, 25.0f);  // col = 10
        auto r = ui::ui_step_grid(h.ctx, b, 6, 16, /*active_cols=*/8, &st);
        check(!r.cell_clicked, "click past active_cols → no cell_clicked");
        check(!r.drag_painting, "click past active_cols → no drag");
    }

    // --- ui_step_grid: drag latches cell; Y-fraction updates per frame ----
    {
        Harness h;
        ui::GridState st;
        ui::Rect b{0, 0, 160, 60};

        // Click near the top of cell (2,3) — cell rect = (30,20,10,10).
        h.click(35.0f, 21.0f);  // y=21 → (21-20)/10 = 0.1
        auto r1 = ui::ui_step_grid(h.ctx, b, 6, 16, 16, &st);
        check(r1.drag_painting && std::fabs(r1.drag_mouse_y_in_cell - 0.1f) < 1e-4f,
              "click near top of cell → y_in_cell ≈ 0.1");

        // Drag down to y=29 (still in cell). Mouse moves, no new click.
        h.drag(35.0f, 29.0f);
        auto r2 = ui::ui_step_grid(h.ctx, b, 6, 16, 16, &st);
        check(r2.drag_painting, "drag frame reports drag_painting");
        check(r2.drag_row == 2 && r2.drag_col == 3, "drag cell stays latched");
        check(std::fabs(r2.drag_mouse_y_in_cell - 0.9f) < 1e-4f,
              "drag down → y_in_cell ≈ 0.9");

        // Drag cursor OUTSIDE the original cell (into neighbouring cell).
        // drag stays pinned to cell (2,3); Y fraction clamps to [0, 1].
        h.drag(90.0f, 500.0f);  // far below
        auto r3 = ui::ui_step_grid(h.ctx, b, 6, 16, 16, &st);
        check(r3.drag_painting && r3.drag_row == 2 && r3.drag_col == 3,
              "drag pinned even when cursor leaves the cell");
        check(r3.drag_mouse_y_in_cell == 1.0f, "y_in_cell clamps to 1.0");

        // Release: drag state clears; next frame idle → no drag events.
        h.release(100.0f, 100.0f);
        ui::ui_step_grid(h.ctx, b, 6, 16, 16, &st);
        check(!st.drag_painting, "release clears state.drag_painting");

        h.idle();
        auto r4 = ui::ui_step_grid(h.ctx, b, 6, 16, 16, &st);
        check(!r4.drag_painting, "idle frame reports no drag");
    }

    // --- ui_step_grid: shift+click extends without moving anchor ---------
    {
        Harness h;
        ui::GridState st;
        ui::Rect b{0, 0, 160, 60};

        // Plain click sets anchor at (row=2, col=3).
        h.click(35.0f, 25.0f);
        ui::ui_step_grid(h.ctx, b, 6, 16, 16, &st);
        h.release(35.0f, 25.0f);
        ui::ui_step_grid(h.ctx, b, 6, 16, 16, &st);

        // Shift+click at (row=4, col=7).  Cell = (70, 40, 10, 10).
        h.click(75.0f, 45.0f, /*shift=*/true);
        auto r = ui::ui_step_grid(h.ctx, b, 6, 16, 16, &st);
        check(r.cell_clicked && r.clicked_with_shift,
              "shift+click reports cell_clicked + clicked_with_shift");
        check(!r.drag_painting, "shift+click does not start paint");
        check(r.shift_extending, "shift+click reports shift_extending");
        check(r.tip_row == 4 && r.tip_col == 7, "tip = clicked cell");
        check(st.anchor_row == 2 && st.anchor_col == 3,
              "anchor unchanged by shift+click");
        check(st.drag_extending, "state enters drag_extending");

        // Shift+drag to (row=5, col=10); tip updates, anchor stays.
        h.drag(105.0f, 55.0f, /*shift=*/true);
        auto r2 = ui::ui_step_grid(h.ctx, b, 6, 16, 16, &st);
        check(r2.shift_extending && r2.tip_row == 5 && r2.tip_col == 10,
              "shift-drag moves tip");
        check(st.anchor_row == 2 && st.anchor_col == 3, "anchor still unchanged");
    }

    // --- ui_drag_handle hits inside radius, reports dx/dy since origin ----
    {
        Harness h;
        ui::DragHandleState st;

        // Idle outside radius.
        h.idle(50.0f, 50.0f);
        auto r0 = ui::ui_drag_handle(h.ctx, 100.0f, 100.0f, 8.0f, &st);
        check(!r0.hovered, "cursor far from handle → not hovered");

        // Idle inside radius.
        h.idle(102.0f, 99.0f);
        auto r1 = ui::ui_drag_handle(h.ctx, 100.0f, 100.0f, 8.0f, &st);
        check(r1.hovered && !r1.pressed, "within radius → hovered only");

        // Press inside radius — pressed true, dragging true, dx/dy = 0.
        h.click(102.0f, 99.0f);
        auto r2 = ui::ui_drag_handle(h.ctx, 100.0f, 100.0f, 8.0f, &st);
        check(r2.pressed && r2.dragging,
              "click inside handle → pressed + dragging");
        check(r2.dx == 0.0f && r2.dy == 0.0f, "dx/dy zero at drag start");

        // Drag outward; dx/dy reflect delta since the origin.
        h.drag(150.0f, 80.0f);
        auto r3 = ui::ui_drag_handle(h.ctx, 100.0f, 100.0f, 8.0f, &st);
        check(r3.dragging, "still dragging when cursor leaves the handle");
        check(std::fabs(r3.dx - (150.0f - 102.0f)) < 1e-4f, "dx = mx - origin_mx");
        check(std::fabs(r3.dy - (80.0f - 99.0f))   < 1e-4f, "dy = my - origin_my");

        // Release.
        h.release(150.0f, 80.0f);
        auto r4 = ui::ui_drag_handle(h.ctx, 100.0f, 100.0f, 8.0f, &st);
        check(r4.released, "release frame reports released");
        check(!st.dragging, "state clears on release");
    }

    // --- ui_scroll_region: wheel adjusts scroll, push/pop clip fires -----
    {
        Harness h;
        ui::ScrollState st;
        ui::Rect bounds{0, 0, 100, 200};
        const float content_h = 500.0f;

        // Wheel down inside region (dy_ticks negative by convention → scroll
        // increases).
        h.wheel_at(50.0f, 100.0f, -2.0f);
        auto origin = ui::ui_scroll_region_begin(h.ctx, bounds, content_h, &st);
        check(origin.y < bounds.y, "scrolled content origin sits above bounds");
        check(st.scroll_y > 0.0f, "wheel advanced scroll_y");
        ui::ui_scroll_region_end(h.ctx, bounds, content_h, &st);
        check(h.rec.pushes == 1 && h.rec.pops == 1,
              "scroll region pushes + pops one clip rect");
        check(h.rec.clips.back().x == 0.0f &&
              h.rec.clips.back().y == 0.0f &&
              h.rec.clips.back().w == 100.0f &&
              h.rec.clips.back().h == 200.0f,
              "clip rect covers full bounds");
    }

    // --- ui_scroll_region clamps scroll at content edges -----------------
    {
        Harness h;
        ui::ScrollState st;
        ui::Rect bounds{0, 0, 100, 200};
        const float content_h = 300.0f;  // max scroll = 100

        // Wheel a lot; scroll should clamp to 100.
        h.wheel_at(50.0f, 100.0f, -1000.0f);
        ui::ui_scroll_region_begin(h.ctx, bounds, content_h, &st);
        ui::ui_scroll_region_end(h.ctx, bounds, content_h, &st);
        check(std::fabs(st.scroll_y - 100.0f) < 1e-4f,
              "scroll clamps to content_h - bounds.h");

        // Scroll clamps to 0 on excess upward wheel.
        h.wheel_at(50.0f, 100.0f, 1000.0f);
        ui::ui_scroll_region_begin(h.ctx, bounds, content_h, &st);
        ui::ui_scroll_region_end(h.ctx, bounds, content_h, &st);
        check(st.scroll_y == 0.0f, "scroll clamps to 0 at top");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
