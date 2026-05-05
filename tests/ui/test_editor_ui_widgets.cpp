// Unit tests for the first-wave widgets in src/operator_api/editor_ui.h:
// ui_button, ui_toggle, ui_radio, ui_slider_h, ui_slider_v, plus the
// layout cursor helpers. Uses a stub VividEditorContext (no GPU / no
// window); record-only draw API so we can assert render calls as well
// as result-struct contents.

#include "operator_api/editor_ui.h"
#include "operator_api/types.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

// --- Recording draw API -----------------------------------------------------
struct DrawCall {
    std::string name;
    std::vector<float> floats;
    std::string text;
    VividColor color{};
};

struct Recorder { std::vector<DrawCall> calls; };

void rec_rect(void* o, float x, float y, float w, float h, VividColor c) {
    auto* r = static_cast<Recorder*>(o);
    r->calls.push_back({"rect", {x, y, w, h}, "", c});
}
void rec_rounded_rect(void* o, float x, float y, float w, float h, float rad, VividColor c) {
    auto* r = static_cast<Recorder*>(o);
    r->calls.push_back({"rounded_rect", {x, y, w, h, rad}, "", c});
}
void rec_text(void* o, float x, float y, const char* text, VividColor c, float scale) {
    auto* r = static_cast<Recorder*>(o);
    r->calls.push_back({"text", {x, y, scale}, text ? text : "", c});
}
void rec_line(void*, float, float, float, float, float, VividColor) {}
float fake_text_width(void*, const char* text, float scale) {
    return (text ? std::strlen(text) : 0u) * 6.0f * scale;
}
float fake_line_height(void*) { return 14.0f; }
void noop_push_clip(void*, float, float, float, float) {}
void noop_pop_clip(void*) {}

VividDrawAPI make_draw_api(Recorder* rec) {
    VividDrawAPI d{};
    d.opaque            = rec;
    d.draw_rect         = rec_rect;
    d.draw_rounded_rect = rec_rounded_rect;
    d.draw_text         = rec_text;
    d.draw_line         = rec_line;
    d.text_width        = fake_text_width;
    d.line_height       = fake_line_height;
    d.push_clip_rect    = noop_push_clip;
    d.pop_clip_rect     = noop_pop_clip;
    return d;
}

// --- Fake context -----------------------------------------------------------
struct Harness {
    Recorder rec;
    VividEditorContext ctx{};

    Harness() {
        ctx.surface_width  = 800.0f;
        ctx.surface_height = 600.0f;
        ctx.dpi_scale      = 1.0f;
        ctx.draw           = make_draw_api(&rec);
        ctx.theme.accent      = {0.3f, 0.6f, 1.0f, 1.0f};
        ctx.theme.bright_text = {1.0f, 1.0f, 1.0f, 1.0f};
        ctx.theme.dim_text    = {0.7f, 0.7f, 0.7f, 1.0f};
        ctx.theme.dark_bg     = {0.1f, 0.1f, 0.12f, 1.0f};
        ctx.theme.slider_fill = {0.9f, 0.7f, 0.2f, 1.0f};
        ctx.theme.slider_track= {0.15f,0.15f,0.17f, 1.0f};
        ctx.param_values   = nullptr;
        ctx.param_count    = 0;
        ctx.output_values  = nullptr;
        ctx.output_count   = 0;
        ctx.events         = nullptr;
        ctx.event_count    = 0;
    }

    void click(float x, float y) {
        ctx.mouse = {};
        ctx.mouse.x = x; ctx.mouse.y = y;
        ctx.mouse.left_clicked = 1;
        ctx.mouse.left_down    = 1;
    }
    void drag_to(float x, float y) {
        ctx.mouse = {};
        ctx.mouse.x = x; ctx.mouse.y = y;
        ctx.mouse.left_down = 1;
    }
    void release_at(float x, float y) {
        ctx.mouse = {};
        ctx.mouse.x = x; ctx.mouse.y = y;
        ctx.mouse.left_released = 1;
    }
    void idle(float x = 0.0f, float y = 0.0f) {
        ctx.mouse = {};
        ctx.mouse.x = x; ctx.mouse.y = y;
    }
};

} // namespace

int main() {
    namespace ui = vivid::ui;
    std::fprintf(stderr, "=== Test: editor_ui widgets ===\n\n");

    // --- Rect containment -------------------------------------------------
    {
        ui::Rect r{10.0f, 20.0f, 30.0f, 40.0f};
        check(r.contains(10.0f, 20.0f), "top-left inclusive");
        check(r.contains(39.99f, 59.99f), "interior contained");
        check(!r.contains(40.0f, 20.0f), "right edge exclusive");
        check(!r.contains(10.0f, 60.0f), "bottom edge exclusive");
        check(!r.contains(5.0f, 30.0f), "left-of-rect rejected");
    }

    // --- Layout cursor: rows stack top-to-bottom --------------------------
    {
        auto c = ui::ui_layout(ui::Rect{0, 0, 100, 100}, /*pad*/ 4.0f, /*gap*/ 2.0f);
        ui::Rect r1 = ui::ui_row(c, 20.0f);
        ui::Rect r2 = ui::ui_row(c, 30.0f);
        check(r1.y == 4.0f, "first row aligns to padded top");
        check(r1.x == 4.0f, "first row x aligns to padded left");
        check(r1.w == 92.0f, "rows span padded width");
        check(r1.h == 20.0f, "row height honoured");
        check(r2.y == 4.0f + 20.0f + 2.0f, "second row gap-separated");
        check(r2.h == 30.0f, "second row height preserved");
    }

    // --- Layout cursor: columns stack left-to-right -----------------------
    {
        auto c = ui::ui_layout(ui::Rect{0, 0, 200, 50}, /*pad*/ 0.0f, /*gap*/ 3.0f);
        ui::Rect a = ui::ui_column(c, 60.0f);
        ui::Rect b = ui::ui_column(c, 40.0f);
        check(a.x == 0.0f && a.w == 60.0f, "first column at origin");
        check(b.x == 63.0f && b.w == 40.0f, "second column gap-separated");
        check(a.h == 50.0f && b.h == 50.0f, "columns span full height");
    }

    // --- ui_split_h / ui_split_v ------------------------------------------
    {
        auto [left, right] = ui::ui_split_h(ui::Rect{0, 0, 100, 20}, 0.25f);
        check(left.w == 25.0f && right.w == 75.0f, "split_h honours fraction");
        check(left.x == 0.0f && right.x == 25.0f, "split_h halves share a seam");

        auto [top, bottom] = ui::ui_split_v(ui::Rect{0, 0, 20, 100}, 0.75f, 4.0f);
        check(std::fabs(top.h - 72.0f) < 1e-4f, "split_v with gap reserves gap space");
        check(std::fabs(bottom.h - 24.0f) < 1e-4f, "split_v remainder after gap");
        check(top.y == 0.0f && bottom.y == 76.0f, "split_v seam = top.y + top.h + gap");
    }

    // --- ui_pad -----------------------------------------------------------
    {
        ui::Rect r = ui::ui_pad(ui::Rect{10, 10, 50, 50}, 6.0f);
        check(r.x == 16.0f && r.y == 16.0f, "pad shifts origin");
        check(r.w == 38.0f && r.h == 38.0f, "pad shrinks dimensions by 2*inset");
    }

    // --- ui_button hover / press / click ----------------------------------
    {
        Harness h;
        ui::Rect r{10, 10, 40, 20};
        // No mouse interaction.
        h.idle(0, 0);
        auto res = ui::ui_button(h.ctx, r, "hi");
        check(!res.hovered && !res.pressed && !res.clicked,
              "button idle → no hover/press/click");

        // Hover only.
        h.idle(20, 15);
        res = ui::ui_button(h.ctx, r, "hi");
        check(res.hovered && !res.pressed && !res.clicked,
              "cursor in rect with no button → hover only");

        // Press (left_down).
        h.drag_to(20, 15);
        res = ui::ui_button(h.ctx, r, "hi");
        check(res.hovered && res.pressed && !res.clicked,
              "cursor + left_down → pressed but not clicked");

        // Click (left_clicked frame).
        h.click(20, 15);
        res = ui::ui_button(h.ctx, r, "hi");
        check(res.clicked, "cursor + left_clicked frame → clicked");

        // Click outside the rect should NOT register.
        h.click(200, 200);
        res = ui::ui_button(h.ctx, r, "hi");
        check(!res.clicked, "click outside rect → no click");
    }

    // --- ui_toggle flips state on click -----------------------------------
    {
        Harness h;
        ui::Rect r{0, 0, 50, 20};
        bool state = false;

        h.click(25, 10);
        auto t = ui::ui_toggle(h.ctx, r, "T", state);
        check(t.clicked, "toggle reports clicked");
        check(t.value == true, "toggle flips false → true");
        state = t.value;

        h.click(25, 10);
        t = ui::ui_toggle(h.ctx, r, "T", state);
        check(t.value == false, "second click flips back true → false");
    }

    // --- ui_radio selects the clicked segment -----------------------------
    {
        Harness h;
        ui::Rect r{0, 0, 120, 20};
        const char* labels[] = {"1x", "2x", "3x", "4x"};
        // Click in the 3rd cell: x in [60, 90).
        h.click(75, 10);
        auto res = ui::ui_radio(h.ctx, r, labels, 4, 0);
        check(res.clicked, "radio reports clicked");
        check(res.value == 2, "radio value = 2 for the 3rd cell");

        // Clicking outside the strip returns current.
        h.click(200, 10);
        res = ui::ui_radio(h.ctx, r, labels, 4, 2);
        check(!res.clicked, "click outside strip → no change");
        check(res.value == 2, "radio echoes current when unclicked");
    }

    // --- ui_slider_h drag: click, drag, release, no state after release ---
    {
        Harness h;
        ui::Rect r{0, 0, 200, 16};
        ui::SliderState st;
        float value = 0.0f;

        // Click near the middle-ish of the meter. The labeled slider
        // geometry reserves 35% for label, 15% for value readout, so
        // the meter occupies (~70..~165).  Clicking at x=100 should
        // land the fraction somewhere in the mid range.
        h.click(100.0f, 8.0f);
        auto r1 = ui::ui_slider_h(h.ctx, r, "Vel", value, 0.0f, 1.0f, &st);
        check(st.dragging, "click in slider → state.dragging = true");
        check(r1.dragging, "result reports dragging");
        check(r1.changed, "new value differs from input 0");
        check(r1.value > 0.0f && r1.value < 1.0f,
              "mid-rect click produces a mid-range value");
        value = r1.value;

        // Drag to near the right: value should increase.
        h.drag_to(180.0f, 8.0f);
        auto r2 = ui::ui_slider_h(h.ctx, r, "Vel", value, 0.0f, 1.0f, &st);
        check(r2.dragging, "still dragging after drag frame");
        check(r2.value > value, "drag right increases value");
        value = r2.value;

        // Release anywhere → dragging clears.
        h.release_at(999.0f, 999.0f);
        auto r3 = ui::ui_slider_h(h.ctx, r, "Vel", value, 0.0f, 1.0f, &st);
        check(!st.dragging, "release → state.dragging = false");
        check(!r3.dragging, "result reflects end of drag");
        check(!r3.changed, "release frame emits no value change");

        // Subsequent idle frame: no change, no drag.
        h.idle();
        auto r4 = ui::ui_slider_h(h.ctx, r, "Vel", value, 0.0f, 1.0f, &st);
        check(!r4.changed && !r4.dragging,
              "post-release idle frame is quiet");
    }

    // --- ui_slider_h clamps at range edges --------------------------------
    {
        Harness h;
        ui::Rect r{0, 0, 200, 16};
        ui::SliderState st;

        // Click far left of meter: fraction clamps to 0 → value = lo.
        h.click(0.0f, 8.0f);
        auto res = ui::ui_slider_h(h.ctx, r, "X", 0.5f, 0.0f, 1.0f, &st);
        check(res.value == 0.0f, "click at left edge → value = lo");

        h.release_at(0.0f, 8.0f);
        ui::ui_slider_h(h.ctx, r, "X", 0.0f, 0.0f, 1.0f, &st);  // reset state

        // Click past the meter but still inside the rect → frac clamps to
        // 1 → value = hi. Rect is (0,0,200,16); meter spans roughly
        // x=74..166. Clicking at x=195 is past the meter but inside the
        // widget, so the drag starts and the value pins to hi.
        h.click(195.0f, 8.0f);
        res = ui::ui_slider_h(h.ctx, r, "X", 0.0f, 0.0f, 1.0f, &st);
        check(res.value == 1.0f, "click past meter right edge → value = hi");
    }

    // --- ui_slider_v: top = hi, bottom = lo -------------------------------
    {
        Harness h;
        ui::Rect r{0, 0, 16, 100};
        ui::SliderState st;

        // Click at the top → frac=0 → value = hi.
        h.click(8.0f, 0.0f);
        auto top = ui::ui_slider_v(h.ctx, r, 0.5f, 0.0f, 1.0f, &st);
        check(top.value == 1.0f, "vertical click at top → value = hi");

        h.release_at(8.0f, 0.0f);
        ui::ui_slider_v(h.ctx, r, 1.0f, 0.0f, 1.0f, &st);

        // Click near the bottom (rect is half-open so y=100 is outside) —
        // y=99.9 is just inside the rect, frac≈1.0 → value = lo.
        h.click(8.0f, 99.9f);
        auto bot = ui::ui_slider_v(h.ctx, r, 1.0f, 0.0f, 1.0f, &st);
        check(std::fabs(bot.value) < 1e-3f, "vertical click near bottom → value ≈ lo");
    }

    // --- Null SliderState is safe on ui_slider_h --------------------------
    {
        Harness h;
        ui::Rect r{0, 0, 200, 16};
        h.click(100.0f, 8.0f);
        auto res = ui::ui_slider_h(h.ctx, r, "X", 0.3f, 0.0f, 1.0f, nullptr);
        check(!res.dragging && !res.changed,
              "slider without state struct never drags");
        check(res.value == 0.3f, "slider echoes input when stateless");
    }

    // --- ui_tab_strip ----------------------------------------------------
    {
        Harness h;
        constexpr const char* kLabels[] = {"A", "B", "C"};
        ui::Rect r{0, 0, 90, 20};  // 3 tabs × 30px each

        // Idle cursor outside strip → no hover, no click
        h.idle(200.0f, 10.0f);
        auto res = ui::ui_tab_strip(h.ctx, r, kLabels, 3, 0);
        check(res.hovered_idx == -1 && !res.clicked,
              "tab_strip idle outside → no hover");

        // Cursor over tab 1 (x=35, tab_w=30 → idx=1)
        h.idle(35.0f, 10.0f);
        res = ui::ui_tab_strip(h.ctx, r, kLabels, 3, 0);
        check(res.hovered_idx == 1, "tab_strip hover → correct idx");

        // Click on tab 2 (x=65 → idx=2)
        h.click(65.0f, 10.0f);
        res = ui::ui_tab_strip(h.ctx, r, kLabels, 3, 0);
        check(res.clicked && res.clicked_idx == 2,
              "tab_strip click → clicked=true, correct idx");

        // Click on already-active tab (0)
        h.click(5.0f, 10.0f);
        res = ui::ui_tab_strip(h.ctx, r, kLabels, 3, 0);
        check(res.clicked && res.clicked_idx == 0,
              "tab_strip click on active tab still fires");

        // Zero-count strip is safe
        h.click(5.0f, 10.0f);
        res = ui::ui_tab_strip(h.ctx, r, kLabels, 0, 0);
        check(!res.clicked && res.clicked_idx == -1,
              "tab_strip count=0 → no click");
    }

    // --- ui_selectable_row -----------------------------------------------
    {
        Harness h;
        ui::Rect r{10, 10, 100, 18};

        // Idle → nothing
        h.idle(0.0f, 0.0f);
        auto res = ui::ui_selectable_row(h.ctx, r, "item", false);
        check(!res.hovered && !res.clicked, "selectable_row idle → nothing");

        // Hover
        h.idle(50.0f, 18.0f);
        res = ui::ui_selectable_row(h.ctx, r, "item", false);
        check(res.hovered && !res.clicked, "selectable_row hover → hovered");

        // Click
        h.click(50.0f, 18.0f);
        res = ui::ui_selectable_row(h.ctx, r, "item", false);
        check(res.clicked, "selectable_row click → clicked");

        // Selected draw: expect a draw_rect call
        h.idle(0.0f, 0.0f);
        h.rec.calls.clear();
        ui::ui_selectable_row(h.ctx, r, "item", /*selected=*/true);
        bool has_rect = false;
        for (auto& c : h.rec.calls) if (c.name == "rect") has_rect = true;
        check(has_rect, "selectable_row selected=true → draws fill rect");

        // Non-selected, non-hovered → no rect
        h.idle(0.0f, 0.0f);
        h.rec.calls.clear();
        ui::ui_selectable_row(h.ctx, r, "item", /*selected=*/false);
        bool has_rect2 = false;
        for (auto& c : h.rec.calls) if (c.name == "rect") has_rect2 = true;
        check(!has_rect2, "selectable_row idle+unselected → no fill rect");
    }

    // --- ui_icon_button --------------------------------------------------
    {
        Harness h;
        ui::Rect r{10, 10, 40, 20};

        // Idle outside → nothing
        h.idle(0.0f, 0.0f);
        auto res = ui::ui_icon_button(h.ctx, r, "▶");
        check(!res.hovered && !res.pressed && !res.clicked,
              "icon_button idle outside → no hover");

        // Hover
        h.idle(20.0f, 18.0f);
        res = ui::ui_icon_button(h.ctx, r, "▶");
        check(res.hovered && !res.clicked, "icon_button hover");

        // Click
        h.click(20.0f, 18.0f);
        res = ui::ui_icon_button(h.ctx, r, "▶");
        check(res.clicked, "icon_button click → clicked");

        // Active state
        h.idle(0.0f, 0.0f);
        res = ui::ui_icon_button(h.ctx, r, "▶", /*active=*/true);
        check(!res.clicked, "icon_button active=true idle → not clicked");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
