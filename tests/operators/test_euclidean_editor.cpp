// End-to-end tests for EuclideanCore::draw_editor(). Synthesised
// VividEditorContext drives keyboard + mouse flows; captured set_param
// writes verify the editor routes user intent to the right params.

#include "euclidean_core.h"
#include "euclidean_editor_shared.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace ek = ::vivid::editor_keys;
namespace eu = ::vivid::euclidean_editor;

namespace {

struct CapturedSet {
    std::string name;
    float value = 0.0f;
};
struct CaptureCtx {
    std::vector<CapturedSet> calls;
};

void capture_set_param(void* opaque, const char* name, float v) {
    auto* c = static_cast<CaptureCtx*>(opaque);
    if (c) c->calls.push_back({std::string(name ? name : ""), v});
}
void capture_set_string_param(void*, const char*, const char*) {}

void noop_draw_rect(void*, float, float, float, float, VividColor) {}
void noop_draw_rounded_rect(void*, float, float, float, float, float, VividColor) {}
void noop_draw_text(void*, float, float, const char*, VividColor, float) {}
void noop_draw_line(void*, float, float, float, float, float, VividColor) {}
float fake_text_width(void*, const char* text, float scale) {
    const std::size_t len = text ? std::strlen(text) : 0;
    return static_cast<float>(len) * 7.0f * scale;
}
float fake_line_height(void*) { return 14.0f; }
void noop_push_clip(void*, float, float, float, float) {}
void noop_pop_clip(void*) {}

VividDrawAPI make_draw_api() {
    VividDrawAPI d{};
    d.draw_rect         = noop_draw_rect;
    d.draw_rounded_rect = noop_draw_rounded_rect;
    d.draw_text         = noop_draw_text;
    d.draw_line         = noop_draw_line;
    d.text_width        = fake_text_width;
    d.line_height       = fake_line_height;
    d.push_clip_rect    = noop_push_clip;
    d.pop_clip_rect     = noop_pop_clip;
    return d;
}

struct EditorHarness {
    EuclideanCore core;
    CaptureCtx capture;
    std::vector<float> params;
    std::vector<float> outputs;
    std::vector<VividEditorEvent> events;
    VividEditorContext ctx{};

    EditorHarness() : params(7, 0.0f), outputs(3, 0.0f) {
        params[0] = 3.0f;   // hits
        params[1] = 8.0f;   // steps
        params[2] = 0.0f;   // rotation
        params[3] = 0.5f;   // gate_length
        params[4] = 2.0f;   // rate = 1/4
        params[5] = 0.0f;   // clock_mode = external
        params[6] = 0.0f;   // bar_sync = off

        ctx.surface_width  = 820.0f;
        ctx.surface_height = 260.0f;
        ctx.dpi_scale      = 1.0f;
        ctx.draw           = make_draw_api();
        ctx.commands.opaque           = &capture;
        ctx.commands.set_param        = capture_set_param;
        ctx.commands.set_string_param = capture_set_string_param;
        ctx.param_values  = params.data();
        ctx.param_count   = static_cast<uint32_t>(params.size());
        ctx.output_values = outputs.data();
        ctx.output_count  = static_cast<uint32_t>(outputs.size());
        ctx.mouse         = {};
        ctx.time          = 0.0;
        refresh_events();
    }
    void refresh_events() {
        ctx.events      = events.empty() ? nullptr : events.data();
        ctx.event_count = static_cast<uint32_t>(events.size());
    }
    void clear_input() {
        events.clear(); refresh_events();
        ctx.mouse = {};
    }
    void clear_capture() { capture.calls.clear(); }
    void draw() {
        refresh_events();
        ctx.wants_keyboard = 0;
        core.draw_editor(&ctx);
    }
};

VividEditorEvent key_ev(int k, int mods = 0) {
    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_KEY;
    e.key = k;
    e.action = ek::kPress;
    e.modifiers = mods;
    return e;
}
VividEditorEvent scroll_ev(float dy, int mods = 0) {
    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_MOUSE_SCROLL;
    e.scroll_dy = dy;
    e.modifiers = mods;
    return e;
}

bool captured(const CaptureCtx& c, const char* name, float v) {
    for (const auto& call : c.calls)
        if (call.name == name && std::abs(call.value - v) < 1e-4f) return true;
    return false;
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: Euclidean draw_editor ===\n\n");

    // --- Editor metadata ---
    {
        auto m = EuclideanCore::editor_metadata();
        check(m.default_width  == 820, "metadata: default_width = 820");
        check(m.default_height == 260, "metadata: default_height = 260");
        check(m.title_suffix != nullptr &&
              std::strcmp(m.title_suffix, "Euclidean Editor") == 0,
              "metadata: title_suffix = Euclidean Editor");
    }

    // --- wants_keyboard set every frame ---
    {
        EditorHarness h;
        h.draw();
        check(h.ctx.wants_keyboard == 1, "draw_editor sets wants_keyboard");
    }

    // --- Arrow keys drive rotation / hits / steps ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::kRight)};
        h.draw();
        check(captured(h.capture, "rotation", 1.0f),
              "Right arrow nudges rotation +1");

        h.clear_input(); h.clear_capture();
        h.params[2] = 3.0f;  // rotation = 3
        h.events = {key_ev(ek::kLeft)};
        h.draw();
        check(captured(h.capture, "rotation", 2.0f),
              "Left arrow nudges rotation -1");

        h.clear_input(); h.clear_capture();
        h.params[2] = 0.0f;
        h.events = {key_ev(ek::kLeft)};
        h.draw();
        check(captured(h.capture, "rotation", 0.0f) ||
              !captured(h.capture, "rotation", -1.0f),
              "Left arrow at rotation=0 clamps (no negative rotation emitted)");

        h.clear_input(); h.clear_capture();
        h.events = {key_ev(ek::kUp)};
        h.draw();
        check(captured(h.capture, "hits", 4.0f),
              "Up arrow nudges hits +1");

        h.clear_input(); h.clear_capture();
        h.events = {key_ev(ek::kDown)};
        h.draw();
        check(captured(h.capture, "hits", 2.0f),
              "Down arrow nudges hits -1");

        h.clear_input(); h.clear_capture();
        h.events = {key_ev(ek::kUp, ek::kModShift)};
        h.draw();
        check(captured(h.capture, "steps", 9.0f),
              "Shift+Up nudges steps +1");

        h.clear_input(); h.clear_capture();
        h.events = {key_ev(ek::kDown, ek::kModShift)};
        h.draw();
        check(captured(h.capture, "steps", 7.0f),
              "Shift+Down nudges steps -1");
    }

    // --- R key resets rotation ---
    {
        EditorHarness h;
        h.params[2] = 5.0f;
        h.events = {key_ev(ek::kR)};
        h.draw();
        check(captured(h.capture, "rotation", 0.0f),
              "R key resets rotation to 0");
    }

    // --- D key cycles through density presets ---
    {
        EditorHarness h;
        // First D press advances to preset index 1 (5/8 cinquillo).
        h.events = {key_ev(ek::kD)};
        h.draw();
        check(captured(h.capture, "hits",  5.0f) &&
              captured(h.capture, "steps", 8.0f),
              "first D press applies preset index 1 (5/8)");
        check(h.core.editor_preset_cursor_ == 1,
              "preset cursor advanced to 1");
    }

    // --- Scroll wheel over the strip adjusts hits ---
    {
        EditorHarness h;
        // Place mouse inside the strip region.
        h.ctx.mouse.x = 400.0f;
        h.ctx.mouse.y = 120.0f;
        h.events = {scroll_ev(+1.0f)};
        h.draw();
        check(captured(h.capture, "hits", 4.0f),
              "scroll up increments hits");

        h.clear_input(); h.clear_capture();
        h.ctx.mouse.x = 400.0f;
        h.ctx.mouse.y = 120.0f;
        h.events = {scroll_ev(-1.0f)};
        h.draw();
        check(captured(h.capture, "hits", 2.0f),
              "scroll down decrements hits");
    }

    // --- Alt+scroll adjusts steps ---
    {
        EditorHarness h;
        h.ctx.mouse.x = 400.0f;
        h.ctx.mouse.y = 120.0f;
        h.events = {scroll_ev(+1.0f, ek::kModAlt)};
        h.draw();
        check(captured(h.capture, "steps", 9.0f),
              "Alt+scroll up increments steps");
    }

    // --- Scroll outside the strip is ignored ---
    {
        EditorHarness h;
        h.ctx.mouse.x = 2000.0f;  // far right, outside strip
        h.ctx.mouse.y = 120.0f;
        h.events = {scroll_ev(+1.0f)};
        h.draw();
        check(h.capture.calls.empty(),
              "scroll outside the strip emits nothing");
    }

    // --- Horizontal drag on strip scrubs rotation ---
    {
        EditorHarness h;
        h.params[1] = 16.0f;  // steps = 16
        // Strip is at x ≈ 8..556 (approx). Click at the middle of the strip.
        h.ctx.mouse.left_clicked = 1;
        h.ctx.mouse.left_down = 1;
        h.ctx.mouse.x = 280.0f;  // mid-strip
        h.ctx.mouse.y = 120.0f;
        h.draw();
        // Expect rotation around 7-8 (middle of 0..15).
        bool sane_rot = false;
        for (const auto& c : h.capture.calls) {
            if (c.name == "rotation" && c.value >= 6.0f && c.value <= 9.0f) {
                sane_rot = true; break;
            }
        }
        check(sane_rot, "drag in mid-strip emits rotation near steps/2");
    }

    // --- Click on a density-preset row applies that preset ---
    {
        EditorHarness h;
        // Side panel starts at x ≈ 8 + (820-3*8-220) + 8 ≈ 584. Click inside.
        // Preset row 2 (index 2) at y = top_bar + inset + 44 + 2*22 = ...
        h.ctx.mouse.left_clicked = 1;
        h.ctx.mouse.left_down = 1;
        h.ctx.mouse.x = 700.0f;
        // preset_y0 = side_y (strip_y = 8+28+8 = 44) + 44 = 88.
        // row 2 (index 2) starts at 88 + 2*22 = 132.
        h.ctx.mouse.y = 134.0f;
        h.draw();
        // Preset index 2 is {2, 5, "2/5"}.
        check(captured(h.capture, "hits",  2.0f) &&
              captured(h.capture, "steps", 5.0f),
              "click on 3rd preset row applies 2/5");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
