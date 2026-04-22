// End-to-end tests for SequencerCore::draw_editor(...) using a fake
// VividEditorContext. Covers metadata, keyboard routing (cursor nav,
// Tab row switch, Enter toggle, digits, Space clear, Cmd+C/V),
// mouse click → set_param, and `steps`-shrink cursor clamping.

#include "sequencer_core.h"
#include "sequencer_editor_shared.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace se = ::vivid::sequencer_editor;

namespace {

struct CapturedSet {
    std::string name;
    float value = 0.0f;
};

struct CaptureCtx {
    std::vector<CapturedSet> calls;
};

void capture_set_param(void* opaque, const char* name, float value) {
    auto* ctx = static_cast<CaptureCtx*>(opaque);
    if (!ctx) return;
    ctx->calls.push_back({std::string(name ? name : ""), value});
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
    VividDrawAPI draw{};
    draw.draw_rect         = noop_draw_rect;
    draw.draw_rounded_rect = noop_draw_rounded_rect;
    draw.draw_text         = noop_draw_text;
    draw.draw_line         = noop_draw_line;
    draw.text_width        = fake_text_width;
    draw.line_height       = fake_line_height;
    draw.push_clip_rect    = noop_push_clip;
    draw.pop_clip_rect     = noop_pop_clip;
    return draw;
}

// Editor-layout constants duplicated from sequencer_editor.cpp. If these
// drift from the real editor, the mouse-hit tests below will miss their
// target — that's the failure we want.
constexpr float kInset      = 8.0f;
constexpr float kTopBarH    = 26.0f;
constexpr float kSidePanelW = 220.0f;

struct GridGeom {
    float grid_x;
    float grid_y;
    float grid_w;
    float grid_h;
    float cell_w;
    float cell_h;
};

GridGeom compute_grid_geom(float surf_w, float surf_h) {
    GridGeom g{};
    g.grid_x = kInset;
    g.grid_y = kInset + kTopBarH + kInset;
    g.grid_w = std::max(0.0f, surf_w - 3.0f * kInset - kSidePanelW);
    g.grid_h = std::max(0.0f, surf_h - g.grid_y - kInset);
    g.cell_w = g.grid_w / static_cast<float>(se::kMaxSteps);
    g.cell_h = g.grid_h / static_cast<float>(se::kRowCount);
    return g;
}

struct EditorHarness {
    SequencerCore core;
    std::vector<float> params;
    std::vector<float> outputs;
    std::vector<VividEditorEvent> events;
    CaptureCtx capture;
    VividEditorContext ctx{};

    // Params are laid out by SequencerCore::collect_params: source, steps,
    // 32 step_values, 32 step_gates, rate_mode, frequency, sync_division,
    // glide, amplitude, offset, polarity, midi_channel = 74 total.
    EditorHarness() : params(80, 0.0f), outputs(4, 0.0f) {
        params[se::kStepsParamIndex]    = 8.0f;
        params[se::kPolarityParamIndex] = 0.0f;  // bipolar
        // Non-trivial defaults: values at 0.5, gates on.
        for (int s = 0; s < se::kMaxSteps; ++s) {
            params[se::param_index_for(se::RowKind::Value, s)] = 0.5f;
            params[se::param_index_for(se::RowKind::Gate,  s)] = 1.0f;
        }
        outputs[se::kStepOutputIndex] = 0.0f;  // playhead at step 0

        ctx.surface_width  = 900.0f;
        ctx.surface_height = 420.0f;
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
    void clear_capture() { capture.calls.clear(); }
    void clear_input() {
        events.clear();
        refresh_events();
        ctx.mouse = {};
    }
    void draw() {
        refresh_events();
        ctx.wants_keyboard = 0;
        ctx.request_close  = 0;
        core.draw_editor(&ctx);
    }
};

constexpr int kGlfwPress      = 1;
constexpr int kGlfwKeySpace   = 32;
constexpr int kGlfwKey0       = 48;
constexpr int kGlfwKey5       = 53;
constexpr int kGlfwKeyC       = 67;
constexpr int kGlfwKeyV       = 86;
constexpr int kGlfwKeyEnter   = 257;
constexpr int kGlfwKeyTab     = 258;
constexpr int kGlfwKeyRight   = 262;
constexpr int kGlfwKeyDown    = 264;
constexpr int kGlfwModShift   = 0x0001;
constexpr int kGlfwModSuper   = 0x0008;

VividEditorEvent key(int k, int mods = 0) {
    VividEditorEvent e{};
    e.type       = VIVID_EDITOR_EVENT_KEY;
    e.key        = k;
    e.action     = kGlfwPress;
    e.modifiers  = mods;
    return e;
}

bool capture_has(const CaptureCtx& c, const char* name, float v) {
    for (const auto& call : c.calls) {
        if (call.name == name && std::abs(call.value - v) < 1e-4f) return true;
    }
    return false;
}
bool capture_has_name(const CaptureCtx& c, const char* name) {
    for (const auto& call : c.calls) {
        if (call.name == name) return true;
    }
    return false;
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: Sequencer draw_editor ===\n\n");

    // --- editor metadata ---
    {
        VividEditorMetadata m = SequencerCore::editor_metadata();
        check(m.default_width == 900,  "metadata: default_width = 900");
        check(m.default_height == 420, "metadata: default_height = 420");
        check(m.min_width  == 600,     "metadata: min_width = 600");
        check(m.min_height == 300,     "metadata: min_height = 300");
        check(m.title_suffix != nullptr &&
              std::strcmp(m.title_suffix, "Sequencer Editor") == 0,
              "metadata: title_suffix = Sequencer Editor");
    }

    // --- wants_keyboard set every frame ---
    {
        EditorHarness h;
        h.draw();
        check(h.ctx.wants_keyboard == 1, "draw_editor sets wants_keyboard = 1");
    }

    // --- Enter at cursor (row 0, step 0) on value row → toggle 0↔1 ---
    {
        EditorHarness h;
        // Initial value is 0.5; toggling should flip to 1.0 (cur<=0.5 path).
        // Wait — my impl is `cur > 0.5 ? 0 : 1`, so 0.5 → 1.
        h.events = {key(kGlfwKeyEnter)};
        h.draw();
        check(h.capture.calls.size() == 1u, "Enter emits one set_param");
        if (!h.capture.calls.empty()) {
            check(h.capture.calls[0].name == "step_value_0",
                  "Enter targets value row, step 0");
            check(h.capture.calls[0].value == 1.0f,
                  "Enter toggles value 0.5 → 1.0");
        }
    }

    // --- Tab swaps row; Enter then targets gate row ---
    {
        EditorHarness h;
        h.events = {key(kGlfwKeyTab), key(kGlfwKeyEnter)};
        h.draw();
        check(h.capture.calls.size() == 1u, "Tab+Enter emits one set_param");
        if (!h.capture.calls.empty()) {
            check(h.capture.calls[0].name == "step_gate_0",
                  "after Tab, Enter targets gate row");
            check(h.capture.calls[0].value == 0.0f,
                  "Enter toggles gate 1.0 → 0.0");
        }
    }

    // --- Right-arrow moves cursor; Enter now targets step 1 ---
    {
        EditorHarness h;
        h.events = {key(kGlfwKeyRight), key(kGlfwKeyEnter)};
        h.draw();
        check(capture_has(h.capture, "step_value_1", 1.0f),
              "right-arrow then Enter targets step_value_1");
    }

    // --- Digit 5 on value row sets value = 5/9 ≈ 0.555 ---
    {
        EditorHarness h;
        h.events = {key(kGlfwKey5)};
        h.draw();
        check(h.capture.calls.size() == 1u, "digit 5 emits one set_param");
        if (!h.capture.calls.empty()) {
            check(h.capture.calls[0].name == "step_value_0",
                  "digit 5 targets value row");
            const float expected = 5.0f / 9.0f;
            check(std::abs(h.capture.calls[0].value - expected) < 1e-3f,
                  "digit 5 sets value to 5/9");
        }
    }

    // --- Digit 0 on gate row sets gate off ---
    {
        EditorHarness h;
        h.events = {key(kGlfwKeyTab), key(kGlfwKey0)};
        h.draw();
        check(capture_has(h.capture, "step_gate_0", 0.0f),
              "digit 0 on gate row sets gate off");
    }

    // --- Shift+right extends selection; Enter fires on all cells ---
    {
        EditorHarness h;
        // Shift+right twice → selection covers steps 0..2 in value row.
        h.events = {
            key(kGlfwKeyRight, kGlfwModShift),
            key(kGlfwKeyRight, kGlfwModShift),
            key(kGlfwKeyEnter),
        };
        h.draw();
        check(h.capture.calls.size() == 3u,
              "Enter across 3-cell selection emits 3 set_params");
        check(capture_has_name(h.capture, "step_value_0"), "step_value_0 touched");
        check(capture_has_name(h.capture, "step_value_1"), "step_value_1 touched");
        check(capture_has_name(h.capture, "step_value_2"), "step_value_2 touched");
    }

    // --- Shift+down extends selection across rows ---
    {
        EditorHarness h;
        h.events = {
            key(kGlfwKeyDown, kGlfwModShift),
            key(kGlfwKeyEnter),
        };
        h.draw();
        // Selection spans (row 0, step 0) and (row 1, step 0). Enter toggles
        // both cells. Toggle logic probes the cursor cell (row 1 after move)
        // and applies target to the whole selection.
        check(h.capture.calls.size() == 2u,
              "Enter across row-spanning selection emits 2 set_params");
        check(capture_has_name(h.capture, "step_value_0"),
              "row-spanning selection touches step_value_0");
        check(capture_has_name(h.capture, "step_gate_0"),
              "row-spanning selection touches step_gate_0");
    }

    // --- Space clears selection (value → 0, gate → 0) ---
    {
        EditorHarness h;
        h.events = {
            key(kGlfwKeyDown, kGlfwModShift),
            key(kGlfwKeySpace),
        };
        h.draw();
        check(h.capture.calls.size() == 2u,
              "Space across row-spanning selection emits 2 set_params");
        check(capture_has(h.capture, "step_value_0", 0.0f),
              "Space clears step_value_0 to 0");
        check(capture_has(h.capture, "step_gate_0", 0.0f),
              "Space clears step_gate_0 to 0");
    }

    // --- Cmd+C then Cmd+V round-trips the rectangular selection ---
    {
        EditorHarness h;
        // Seed: put distinctive values at steps 0..2 (value row).
        h.params[se::param_index_for(se::RowKind::Value, 0)] = 0.1f;
        h.params[se::param_index_for(se::RowKind::Value, 1)] = 0.2f;
        h.params[se::param_index_for(se::RowKind::Value, 2)] = 0.3f;
        // Extend selection to cover steps 0..2, then Cmd+C.
        h.events = {
            key(kGlfwKeyRight, kGlfwModShift),
            key(kGlfwKeyRight, kGlfwModShift),
            key(kGlfwKeyC, kGlfwModSuper),
        };
        h.draw();
        check(h.capture.calls.empty(),
              "Cmd+C emits no set_param calls (pure read)");

        // Paste into same origin (selection unchanged).
        h.clear_input();
        h.events = {key(kGlfwKeyV, kGlfwModSuper)};
        h.draw();
        check(h.capture.calls.size() == 3u,
              "Cmd+V emits 3 set_param calls for 3-cell clipboard");
        check(capture_has(h.capture, "step_value_0", 0.1f),
              "Cmd+V restores step_value_0 = 0.1");
        check(capture_has(h.capture, "step_value_2", 0.3f),
              "Cmd+V restores step_value_2 = 0.3");
    }

    // --- Cursor clamps when `steps` shrinks between frames ---
    {
        EditorHarness h;
        // Move cursor to step 7 (at num_steps = 8).
        for (int i = 0; i < 7; ++i) h.events.push_back(key(kGlfwKeyRight));
        h.draw();
        check(h.core.editor_cursor_step_ == 7,
              "cursor advanced to step 7");

        // Shrink steps to 4 between frames.
        h.clear_input();
        h.params[se::kStepsParamIndex] = 4.0f;
        h.draw();
        check(h.core.editor_cursor_step_ == 3,
              "cursor clamped to num_steps-1 = 3 after shrink");
    }

    // --- Mouse click on a value cell emits set_param with click-Y value ---
    {
        EditorHarness h;
        const auto g = compute_grid_geom(h.ctx.surface_width, h.ctx.surface_height);
        // Click at the top of value-row cell for step 3 → value ≈ 1.0.
        const float click_x = g.grid_x + g.cell_w * 3.0f + g.cell_w * 0.5f;
        const float click_y = g.grid_y + 1.0f;  // 1 px below top of row 0
        h.ctx.mouse.x = click_x;
        h.ctx.mouse.y = click_y;
        h.ctx.mouse.left_clicked = 1;
        h.ctx.mouse.left_down    = 1;
        h.draw();
        check(capture_has_name(h.capture, "step_value_3"),
              "mouse click on value-row step 3 emits step_value_3");
        // The emitted value should be very close to 1.0 (top of cell).
        bool high_value_emitted = false;
        for (const auto& c : h.capture.calls) {
            if (c.name == "step_value_3" && c.value > 0.95f)
                high_value_emitted = true;
        }
        check(high_value_emitted,
              "top-of-cell click emits value near 1.0");
    }

    // --- Mouse click on gate cell toggles gate ---
    {
        EditorHarness h;
        const auto g = compute_grid_geom(h.ctx.surface_width, h.ctx.surface_height);
        // Click in the middle of gate-row cell for step 2.
        const float click_x = g.grid_x + g.cell_w * 2.0f + g.cell_w * 0.5f;
        const float click_y = g.grid_y + g.cell_h + g.cell_h * 0.5f;  // row 1
        h.ctx.mouse.x = click_x;
        h.ctx.mouse.y = click_y;
        h.ctx.mouse.left_clicked = 1;
        h.ctx.mouse.left_down    = 1;
        h.draw();
        check(capture_has(h.capture, "step_gate_2", 0.0f),
              "click on gate cell toggles step_gate_2 from 1→0");
    }

    // --- Click beyond num_steps is a no-op ---
    {
        EditorHarness h;
        const auto g = compute_grid_geom(h.ctx.surface_width, h.ctx.surface_height);
        // num_steps = 8, so step 15 is inactive.
        const float click_x = g.grid_x + g.cell_w * 15.0f + g.cell_w * 0.5f;
        const float click_y = g.grid_y + 1.0f;
        h.ctx.mouse.x = click_x;
        h.ctx.mouse.y = click_y;
        h.ctx.mouse.left_clicked = 1;
        h.ctx.mouse.left_down    = 1;
        h.draw();
        check(h.capture.calls.empty(),
              "click on inactive cell (beyond num_steps) emits nothing");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
