// End-to-end tests for ParametricEQ::draw_editor. Synthesised
// VividEditorContext drives keyboard + mouse flows; captured set_param
// writes verify the editor routes user intent to the right params.

#include "parametric_eq.h"
#include "parametric_eq_editor_shared.h"
#include "operator_api/editor_keys.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace ek = ::vivid::editor_keys;
namespace pe = ::vivid::parametric_eq_editor;

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
    ParametricEQ core;
    CaptureCtx capture;
    std::vector<float> params;
    std::vector<float> outputs;
    std::vector<VividEditorEvent> events;
    VividEditorContext ctx{};

    EditorHarness() : params(17, 0.0f), outputs(1, 0.0f) {
        // Defaults matching ParametricEQ constructor.
        params[pe::kBandCountParamIndex] = 4.0f;
        params[pe::freq_param_index(0)]  =   100.0f;
        params[pe::freq_param_index(1)]  =   500.0f;
        params[pe::freq_param_index(2)]  =  2000.0f;
        params[pe::freq_param_index(3)]  =  8000.0f;
        for (int b = 0; b < pe::kMaxBands; ++b) {
            params[pe::gain_param_index(b)] = 0.0f;
            params[pe::q_param_index(b)]    = 1.0f;
            params[pe::type_param_index(b)] = 0.0f;
        }

        ctx.surface_width  = 1000.0f;
        ctx.surface_height = 540.0f;
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

bool captured_name(const CaptureCtx& c, const char* name) {
    for (const auto& call : c.calls)
        if (call.name == name) return true;
    return false;
}
float captured_last(const CaptureCtx& c, const char* name) {
    for (auto it = c.calls.rbegin(); it != c.calls.rend(); ++it)
        if (it->name == name) return it->value;
    return std::nanf("");
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: ParametricEQ draw_editor ===\n\n");

    // --- Editor metadata ---
    {
        auto m = ParametricEQ::editor_metadata();
        check(m.default_width  == 1000, "metadata default_width = 1000");
        check(m.default_height == 540,  "metadata default_height = 540");
        check(m.min_width      == 720,  "metadata min_width = 720");
        check(m.title_suffix != nullptr &&
              std::strcmp(m.title_suffix, "ParametricEQ Editor") == 0,
              "metadata title_suffix");
    }

    // --- wants_keyboard is asserted every frame ---
    {
        EditorHarness h;
        h.draw();
        check(h.ctx.wants_keyboard == 1, "draw_editor sets wants_keyboard");
    }

    // --- 1..4 select band ---
    {
        EditorHarness h;
        h.core.editor_selected_band_ = 0;
        h.events = {key_ev(ek::k3)};
        h.draw();
        check(h.core.editor_selected_band_ == 2,
              "key '3' selects band 3 (index 2)");

        h.clear_input();
        h.events = {key_ev(ek::k1)};
        h.draw();
        check(h.core.editor_selected_band_ == 0,
              "key '1' selects band 1 (index 0)");
    }

    // --- T cycles type of the selected band ---
    {
        EditorHarness h;
        h.core.editor_selected_band_ = 0;
        // type_1 default is 0 (Peak); after T it should be 1 (Low Shelf).
        h.events = {key_ev(ek::kT)};
        h.draw();
        const float v = captured_last(h.capture, "type_1");
        check(!std::isnan(v), "T emits type_1");
        check(v == 1.0f, "T on Peak cycles to Low Shelf (1)");

        // Wrap-around at the last type.
        h.clear_input(); h.clear_capture();
        h.params[pe::type_param_index(0)] = 4.0f;  // HP
        h.events = {key_ev(ek::kT)};
        h.draw();
        check(captured_last(h.capture, "type_1") == 0.0f,
              "T wraps HP → Peak");
    }

    // --- Arrow keys nudge selected band: freq and gain ---
    {
        EditorHarness h;
        h.core.editor_selected_band_ = 1;  // select band 2
        // gain_2 default 0, Up arrow → +0.5 dB
        h.events = {key_ev(ek::kUp)};
        h.draw();
        check(std::fabs(captured_last(h.capture, "gain_2") - 0.5f) < 1e-3f,
              "Up arrow nudges gain +0.5 dB");

        h.clear_input(); h.clear_capture();
        h.events = {key_ev(ek::kDown, ek::kModShift)};
        h.draw();
        check(std::fabs(captured_last(h.capture, "gain_2") - (-3.0f)) < 1e-3f,
              "Shift+Down coarse nudge = -3 dB");

        h.clear_input(); h.clear_capture();
        // freq_2 default 500; Right arrow = semitone up (× 2^(1/12)).
        h.events = {key_ev(ek::kRight)};
        h.draw();
        const float expect_r = 500.0f * std::pow(2.0f, 1.0f / 12.0f);
        check(std::fabs(captured_last(h.capture, "freq_2") - expect_r) < 1.0f,
              "Right arrow nudges freq up one semitone");

        h.clear_input(); h.clear_capture();
        h.events = {key_ev(ek::kLeft, ek::kModShift)};
        h.draw();
        const float expect_l = 500.0f * 0.5f;  // down one octave
        check(std::fabs(captured_last(h.capture, "freq_2") - expect_l) < 1.0f,
              "Shift+Left nudges freq down one octave");
    }

    // --- Clicking on a band node selects + enters drag ---
    {
        EditorHarness h;
        // Compute band 0's node position for the 1000x540 surface.
        // Plane region: x in [8 + 48 = 56, surf_w - 8 - 260 - 8 = 724),
        //              y in [8 + 32 + 8 + 12 = 60, ...).
        const float plane_x = 8.0f + 48.0f;
        const float plane_y = 8.0f + 32.0f + 8.0f + 12.0f;
        const float plane_w = (1000.0f - 8.0f - 260.0f - 8.0f) - plane_x - 12.0f;
        const float plane_h = (540.0f - (8.0f + 32.0f + 8.0f) - 8.0f) - 24.0f - 24.0f;

        auto node0 = pe::band_node_position(plane_x, plane_y, plane_w, plane_h,
                                            100.0f, 0.0f);
        h.ctx.mouse.x = node0.x;
        h.ctx.mouse.y = node0.y;
        h.ctx.mouse.left_clicked = 1;
        h.ctx.mouse.left_down = 1;
        h.draw();
        check(h.core.editor_selected_band_ == 0,
              "click on band 0 node selects band 0");
        check(h.core.editor_drag_band_ == 0,
              "click on band 0 enters drag for band 0");

        // While dragging, mouse motion emits freq + gain for band 1 (1→freq_1).
        h.clear_capture();
        h.ctx.mouse.x = plane_x + 0.3f * plane_w;
        h.ctx.mouse.y = plane_y + 0.3f * plane_h;
        h.ctx.mouse.left_clicked = 0;
        h.ctx.mouse.left_down    = 1;
        h.draw();
        check(captured_name(h.capture, "freq_1"),
              "drag emits freq_1");
        check(captured_name(h.capture, "gain_1"),
              "drag emits gain_1");
        const float gain_v = captured_last(h.capture, "gain_1");
        check(gain_v > 0.0f && gain_v < pe::kMaxGainDb,
              "drag y-axis near top yields positive gain");

        // Mouse release ends the drag.
        h.ctx.mouse.left_down = 0;
        h.draw();
        check(h.core.editor_drag_band_ == -1,
              "mouse release ends drag");
    }

    // --- Scroll over a band node adjusts Q ---
    {
        EditorHarness h;
        const float plane_x = 8.0f + 48.0f;
        const float plane_y = 8.0f + 32.0f + 8.0f + 12.0f;
        const float plane_w = (1000.0f - 8.0f - 260.0f - 8.0f) - plane_x - 12.0f;
        const float plane_h = (540.0f - (8.0f + 32.0f + 8.0f) - 8.0f) - 24.0f - 24.0f;

        auto node1 = pe::band_node_position(plane_x, plane_y, plane_w, plane_h,
                                            500.0f, 0.0f);
        h.ctx.mouse.x = node1.x;
        h.ctx.mouse.y = node1.y;
        h.events = {scroll_ev(+1.0f)};
        h.draw();
        const float q = captured_last(h.capture, "q_2");
        check(q > 1.0f && q < 2.0f,
              "scroll up over band 2 increases Q (~*1.15)");

        h.clear_input(); h.clear_capture();
        h.ctx.mouse.x = node1.x;
        h.ctx.mouse.y = node1.y;
        h.events = {scroll_ev(-1.0f)};
        h.draw();
        const float q_down = captured_last(h.capture, "q_2");
        check(q_down < 1.0f && q_down > 0.5f,
              "scroll down over band 2 decreases Q");
    }

    // --- Null ctx is safe ---
    {
        ParametricEQ core;
        core.draw_editor(nullptr);
        check(true, "draw_editor(nullptr) does not crash");
    }

    // --- band_count clamps keyboard '4' when only 2 bands are active ---
    {
        EditorHarness h;
        h.params[pe::kBandCountParamIndex] = 2.0f;
        h.core.editor_selected_band_ = 0;
        h.events = {key_ev(ek::k4)};
        h.draw();
        check(h.core.editor_selected_band_ == 0,
              "key '4' with band_count=2 stays on band 1");
    }

    return failures == 0 ? 0 : 1;
}
