// Focused unit tests for editor_ui viewport, scrollbar, timeline, range,
// box-selection, and shared drawing helpers.

#include "operator_api/editor_ui.h"
#include "operator_api/types.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "test_helpers.h"

namespace {

struct Recorder {
    int rects = 0;
    int rounded = 0;
};

void rec_rect(void* o, float, float, float, float, VividColor) {
    static_cast<Recorder*>(o)->rects++;
}
void rec_rounded(void* o, float, float, float, float, float, VividColor) {
    static_cast<Recorder*>(o)->rounded++;
}
void noop_text(void*, float, float, const char*, VividColor, float) {}
void noop_line(void*, float, float, float, float, float, VividColor) {}
float text_width(void*, const char*, float) { return 0.0f; }
float line_height(void*) { return 12.0f; }

VividDrawAPI make_draw_api(Recorder* rec) {
    VividDrawAPI d{};
    d.opaque = rec;
    d.draw_rect = rec_rect;
    d.draw_rounded_rect = rec_rounded;
    d.draw_text = noop_text;
    d.draw_line = noop_line;
    d.text_width = text_width;
    d.line_height = line_height;
    return d;
}

struct Harness {
    Recorder rec;
    VividEditorContext ctx{};
    Harness() {
        ctx.draw = make_draw_api(&rec);
        ctx.theme.dim_text = {0.7f, 0.7f, 0.7f, 1.0f};
        ctx.theme.bright_text = {1.0f, 1.0f, 1.0f, 1.0f};
        ctx.theme.accent = {0.3f, 0.6f, 1.0f, 1.0f};
    }
    void click(float x, float y) {
        ctx.mouse = {};
        ctx.mouse.x = x;
        ctx.mouse.y = y;
        ctx.mouse.left_clicked = 1;
        ctx.mouse.left_down = 1;
    }
    void drag(float x, float y) {
        ctx.mouse = {};
        ctx.mouse.x = x;
        ctx.mouse.y = y;
        ctx.mouse.left_down = 1;
    }
    void release(float x, float y) {
        ctx.mouse = {};
        ctx.mouse.x = x;
        ctx.mouse.y = y;
        ctx.mouse.left_released = 1;
    }
};

} // namespace

int main() {
    namespace ui = vivid::ui;
    std::fprintf(stderr, "=== Test: editor_ui timeline / viewport helpers ===\n\n");

    {
        ui::Viewport1D v{0.0, 16.0, 100.0f, 320.0f, 4.0, 8.0};
        check(std::fabs(v.pixels_per_unit() - 40.0) < 1e-6, "pixels_per_unit");
        check(std::fabs(v.world_to_screen(6.0) - 180.0f) < 1e-6, "world_to_screen");
        check(std::fabs(v.screen_to_world(180.0f) - 6.0) < 1e-6, "screen_to_world");
        auto vr = v.visible_range();
        check(vr.start == 4.0 && vr.end == 12.0, "visible_range");
    }

    {
        ui::Viewport1D v{0.0, 16.0, 0.0f, 160.0f, 4.0, 8.0};
        ui::zoom_viewport_at(&v, 80.0f, 2.0, 2.0);
        check(std::fabs(v.view_size - 4.0) < 1e-6, "zoom_at halves view size");
        check(std::fabs(v.screen_to_world(80.0f) - 8.0) < 1e-6,
              "zoom_at keeps anchor world value stable");
        ui::pan_viewport(&v, 100.0);
        check(std::fabs(v.view_start - 12.0) < 1e-6, "pan clamps to max");
    }

    {
        Harness h;
        ui::Viewport1D v{0.0, 16.0, 0.0f, 160.0f, 0.0, 8.0};
        ui::ScrollbarState st;
        ui::Rect track{0.0f, 0.0f, 160.0f, 10.0f};
        auto thumb = ui::scrollbar_thumb_rect(track, ui::Orientation::Horizontal,
                                              v.content_size(), v.view_size,
                                              v.view_start);
        check(std::fabs(thumb.w - 80.0f) < 1e-6, "horizontal thumb fraction");
        h.click(40.0f, 5.0f);
        auto r0 = ui::ui_scrollbar(h.ctx, track, ui::Orientation::Horizontal, &v, &st);
        check(r0.hovered && st.dragging_thumb, "scrollbar click starts drag");
        h.drag(80.0f, 5.0f);
        auto r1 = ui::ui_scrollbar(h.ctx, track, ui::Orientation::Horizontal, &v, &st);
        check(r1.changed && std::fabs(v.view_start - 4.0) < 1e-6,
              "scrollbar drag maps travel to view_start");
        h.release(80.0f, 5.0f);
        ui::ui_scrollbar(h.ctx, track, ui::Orientation::Horizontal, &v, &st);
        check(!st.dragging_thumb, "scrollbar release clears drag");
    }

    {
        std::vector<ui::TimelineTick> ticks;
        ui::for_each_timeline_tick({1.0, 5.0}, 1.0, 4.0,
            [&](ui::TimelineTick t) { ticks.push_back(t); });
        bool saw_major_four = false;
        for (const auto& t : ticks) {
            if (std::fabs(t.value - 4.0) < 1e-6 && t.major) saw_major_four = true;
        }
        check(saw_major_four, "timeline tick marks bar boundary major");
    }

    {
        ui::Viewport1D v{0.0, 4096.0, 0.0f, 160.0f, 0.0, 4096.0};
        const double step = ui::timeline_grid_step_for_pixels(v, 0.25, 4.0, 3.0f);
        check(step > 0.25, "timeline grid coarsens dense subpixel ticks");

        Harness h;
        ui::draw_timeline_grid(h.ctx.draw, h.ctx.draw.opaque,
                               {0.0f, 0.0f, 160.0f, 80.0f},
                               v, 0.25, 4.0, {1, 1, 1, 1});
        check(h.rec.rects < 120, "timeline grid caps dense visible draw count");
    }

    {
        ui::Viewport1D v{0.0, 16.0, 0.0f, 160.0f, 0.0, 16.0};
        ui::RangeDragState st;
        double start = 2.0;
        double end = 6.0;
        auto hit = ui::hit_test_range({0, 0, 160, 12}, v, start, end, 8.0f,
                                      v.world_to_screen(6.0), 6.0f);
        check(hit.zone == ui::RangeDragMode::Right, "range right handle hit");
        ui::begin_range_drag(&st, ui::RangeDragMode::Body,
                             v.world_to_screen(3.0), start, end);
        bool changed = ui::update_range_drag(&st, v, v.world_to_screen(5.0),
                                             0.0, 16.0, 1.0, &start, &end);
        check(changed && std::fabs(start - 4.0) < 1e-6 &&
              std::fabs(end - 8.0) < 1e-6, "range body drag preserves length");
    }

    {
        ui::BoxSelectState st;
        ui::begin_box_select(&st, 4.0, 10.0, true);
        auto box = ui::update_box_select(st, 1.0, 12.0);
        check(box.active && box.additive, "box selection reports additive active");
        check(box.x0 == 1.0 && box.x1 == 4.0 && box.y0 == 10.0 && box.y1 == 12.0,
              "box selection normalizes extents");
        ui::end_box_select(&st);
        check(!st.active, "box selection ends");
    }

    {
        struct Item { double start, end; int row; };
        std::vector<Item> items{{1.0, 2.0, 0}, {2.0, 4.0, 1}};
        std::vector<int> indices{0, 1};
        ui::Viewport1D v{0.0, 8.0, 0.0f, 80.0f, 0.0, 8.0};
        auto hit = ui::hit_test_spans(
            indices,
            [&](int i) { return items[static_cast<size_t>(i)].start; },
            [&](int i) { return items[static_cast<size_t>(i)].end; },
            [&](int i) { return items[static_cast<size_t>(i)].row == 1; },
            v, 3.8, v.world_to_screen(3.95));
        check(hit.index == 1 && hit.zone == ui::SpanHitZone::ResizeRight,
              "span hit-test filters rows and detects resize zone");
    }

    {
        Harness h;
        ui::draw_selection_rect(h.ctx.draw, h.ctx.draw.opaque,
                                {0, 0, 10, 10}, {1, 1, 1, 1});
        check(h.rec.rects == 5, "selection rect draws fill + four edges");
    }

    std::fprintf(stderr, "\nAll editor_ui timeline tests passed.\n");
    return 0;
}
