// End-to-end tests for PatternSeqCore::draw_editor().

#include "pattern_seq_core.h"
#include "pattern_seq_editor_shared.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace ek = ::vivid::editor_keys;
namespace ps = ::vivid::pattern_seq_editor;

namespace {

struct CapturedSet { std::string name; float value = 0.0f; };
struct CaptureCtx  { std::vector<CapturedSet> calls; };

void capture_set_param(void* opaque, const char* name, float v) {
    auto* c = static_cast<CaptureCtx*>(opaque);
    if (c) c->calls.push_back({std::string(name ? name : ""), v});
}
void capture_set_string_param(void*, const char*, const char*) {}

void noop_draw_rect(void*, float, float, float, float, VividColor) {}
void noop_draw_rounded_rect(void*, float, float, float, float, float, VividColor) {}
void noop_draw_text(void*, float, float, const char*, VividColor, float) {}
void noop_draw_line(void*, float, float, float, float, float, VividColor) {}
float fake_text_width(void*, const char* t, float s) {
    const std::size_t len = t ? std::strlen(t) : 0;
    return static_cast<float>(len) * 7.0f * s;
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
    PatternSeqCore core;
    CaptureCtx capture;
    std::vector<float> params;
    std::vector<float> outputs;
    std::vector<VividEditorEvent> events;
    VividEditorContext ctx{};

    EditorHarness() : params(22, 0.0f), outputs(4, 0.0f) {
        params[ps::kStepsIndex]       = 8.0f;
        params[ps::kRateIndex]        = 2.0f;
        params[ps::kGateLengthIndex]  = 0.8f;
        params[ps::kProbabilityIndex] = 1.0f;
        // val_N default 0 already from the zeroed vector.

        ctx.surface_width  = 880.0f;
        ctx.surface_height = 320.0f;
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
    void clear_input()   { events.clear(); refresh_events(); ctx.mouse = {}; }
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

bool captured_any(const CaptureCtx& c, const char* name) {
    for (const auto& call : c.calls) if (call.name == name) return true;
    return false;
}

float captured_value(const CaptureCtx& c, const char* name) {
    for (const auto& call : c.calls)
        if (call.name == name) return call.value;
    return -1e9f;
}

bool captured_within(const CaptureCtx& c, const char* name,
                     float target, float tol) {
    for (const auto& call : c.calls)
        if (call.name == name && std::abs(call.value - target) < tol)
            return true;
    return false;
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: PatternSeq draw_editor ===\n\n");

    // --- Metadata ---
    {
        auto m = PatternSeqCore::editor_metadata();
        check(m.default_width  == 880, "metadata: default_width = 880");
        check(m.default_height == 320, "metadata: default_height = 320");
        check(m.title_suffix != nullptr &&
              std::strcmp(m.title_suffix, "PatternSeq Editor") == 0,
              "metadata: title_suffix = PatternSeq Editor");
    }

    // --- wants_keyboard set every frame ---
    {
        EditorHarness h;
        h.draw();
        check(h.ctx.wants_keyboard == 1, "draw_editor sets wants_keyboard");
    }

    // --- Arrow nav ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::kRight), key_ev(ek::kRight), key_ev(ek::kRight)};
        h.draw();
        check(h.core.editor_cursor_step_ == 3,
              "right arrow three times → cursor at step 3");
    }

    // --- Up arrow nudges value by +100 (fine) ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::kUp)};
        h.draw();
        check(captured_within(h.capture, "val_0", 100.0f, 1.0f),
              "Up arrow nudges val_0 by +100");
    }

    // --- Shift+Up nudges by +1000 (coarse) ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::kUp, ek::kModShift)};
        h.draw();
        check(captured_within(h.capture, "val_0", 1000.0f, 1.0f),
              "Shift+Up nudges val_0 by +1000");
    }

    // --- Digit sets value as fraction of max ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::k9)};
        h.draw();
        const float expected = 9.0f / 9.0f * ps::kValueMax;
        check(captured_within(h.capture, "val_0", expected, 1.0f),
              "digit 9 sets val_0 to kValueMax");
    }

    // --- Space zeros the cursor cell ---
    {
        EditorHarness h;
        h.params[ps::param_index_for(0)] = 5000.0f;
        h.events = {key_ev(ek::kSpace)};
        h.draw();
        check(captured_within(h.capture, "val_0", 0.0f, 1e-3f),
              "Space zeros val_0");
    }

    // --- R fills a ramp across active steps ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::kR)};
        h.draw();
        // num_steps = 8 by default; first cell should be kValueMin, last kValueMax.
        check(captured_within(h.capture, "val_0", ps::kValueMin, 1.0f),
              "R fills val_0 with kValueMin");
        check(captured_within(h.capture, "val_7", ps::kValueMax, 1.0f),
              "R fills val_7 (last active step) with kValueMax");
    }

    // --- Shift+Right extends selection ---
    {
        EditorHarness h;
        h.events = {
            key_ev(ek::kRight, ek::kModShift),
            key_ev(ek::kRight, ek::kModShift),
        };
        h.draw();
        check(h.core.editor_selection_.col_lo == 0 &&
              h.core.editor_selection_.col_hi == 2,
              "Shift+Right×2 extends selection to steps 0..2");
    }

    // --- Clipboard round-trip via Cmd+C → Cmd+V ---
    {
        EditorHarness h;
        h.params[ps::param_index_for(0)] = 100.0f;
        h.params[ps::param_index_for(1)] = 200.0f;
        h.params[ps::param_index_for(2)] = 300.0f;

        // Extend selection to 3 cells (cursor at 0, shift+right twice).
        h.events = {
            key_ev(ek::kRight, ek::kModShift),
            key_ev(ek::kRight, ek::kModShift),
            key_ev(ek::kC, ek::kModSuper),
        };
        h.draw();
        check(h.core.editor_clipboard_.has_content,
              "Cmd+C fills clipboard");
        check(h.core.editor_clipboard_.cols == 3,
              "clipboard has 3 cells");

        // Move cursor + anchor to step 5 and paste.
        h.clear_input(); h.clear_capture();
        h.core.editor_cursor_step_ = 5;
        h.core.grid_state_.anchor_col = 5;
        h.events = {key_ev(ek::kV, ek::kModSuper)};
        h.draw();
        check(captured_within(h.capture, "val_5", 100.0f, 1e-3f),
              "Cmd+V writes val_5 = 100");
        check(captured_within(h.capture, "val_6", 200.0f, 1e-3f),
              "Cmd+V writes val_6 = 200");
        check(captured_within(h.capture, "val_7", 300.0f, 1e-3f),
              "Cmd+V writes val_7 = 300");
    }

    // --- Side-panel Ramp ↗ button ---
    {
        EditorHarness h;
        // Ramp ↗ button lives at side-panel y = btn_y0 = side_y + 120 = 44 + 120 = 164.
        h.ctx.mouse.left_clicked = 1;
        h.ctx.mouse.left_down = 1;
        h.ctx.mouse.x = 700.0f;   // inside side panel
        h.ctx.mouse.y = 170.0f;   // first button (Ramp ↗)
        h.draw();
        check(captured_within(h.capture, "val_0", ps::kValueMin, 1.0f),
              "Ramp ↗ button click fills val_0 with kValueMin");
        check(captured_within(h.capture, "val_7", ps::kValueMax, 1.0f),
              "Ramp ↗ button fills last active step with kValueMax");
    }

    // --- Zero button ---
    {
        EditorHarness h;
        // Third button (Zero): y = 164 + 2*(22+4) = 216.
        h.ctx.mouse.left_clicked = 1;
        h.ctx.mouse.left_down = 1;
        h.ctx.mouse.x = 700.0f;
        h.ctx.mouse.y = 222.0f;
        h.draw();
        for (int s = 0; s < 8; ++s) {
            char name[16];
            std::snprintf(name, sizeof(name), "val_%d", s);
            check(captured_within(h.capture, name, 0.0f, 1e-3f),
                  "Zero button zeros all active steps");
        }
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
