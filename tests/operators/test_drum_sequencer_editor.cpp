// Focused unit tests for DrumSequencerCore::draw_editor(...) using a fake
// VividEditorContext. Covers editor metadata, keyboard capture, command
// routing, and mouse interaction across the redesigned unified grid +
// side-panel layout (follow-up: sequencer editing workflows).
//
// The editor surface is laid out like so:
//   inset | grid | inset | side panel | inset
// Grid cells carry trigger+velocity+probability+roll glyphs; the right
// side panel drives selection-wide edits. Enter toggles triggers across
// the current selection; Space clears; 1..4 set roll count; P <digit>
// sets probability; Cmd+C/Cmd+V copy the rectangular selection.

#include "drum_sequencer_core.h"
#include "drum_sequencer_editor_shared.h"
#include "drum_sequencer_layout.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace de     = ::vivid_sequencers::drum_editor;
namespace layout = ::vivid_sequencers::drum_layout;

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
    draw.draw_rect        = noop_draw_rect;
    draw.draw_rounded_rect = noop_draw_rounded_rect;
    draw.draw_text        = noop_draw_text;
    draw.draw_line        = noop_draw_line;
    draw.text_width       = fake_text_width;
    draw.line_height      = fake_line_height;
    draw.push_clip_rect   = noop_push_clip;
    draw.pop_clip_rect    = noop_pop_clip;
    return draw;
}

// Must match drum_sequencer_editor.cpp constants. Kept in sync by hand
// because the editor doesn't expose them; keeping them here as inline
// constants means a layout change that breaks the editor will be caught
// by these tests failing.
constexpr float kInset       = 8.0f;
constexpr float kTopBarH     = 26.0f;
constexpr float kSidePanelW  = 280.0f;
constexpr float kLabelW      = 28.0f;

de::GridMetrics compute_gm(float surface_width, float surface_height) {
    const float grid_x = kInset;
    const float grid_y = kInset + kTopBarH + kInset;
    const float grid_w = std::max(0.0f,
        surface_width - 3.0f * kInset - kSidePanelW);
    const float grid_h = std::max(0.0f, surface_height - grid_y - kInset);
    return de::compute_grid_metrics(grid_x, grid_y, grid_w, grid_h, kLabelW);
}

struct EditorHarness {
    DrumSequencerCore core;
    std::vector<float> params;
    std::vector<float> outputs;
    std::vector<VividEditorEvent> events;
    CaptureCtx capture;
    VividEditorContext ctx{};

    // 588 params: 4 visible + 6 notes + 96×3 pattern/mod_a/mod_b + bar_sync
    // + active_pattern + 96×3 trig_b/prob/roll. See drum_sequencer_layout.h.
    EditorHarness() : params(588, 0.0f), outputs(8, 0.0f) {
        params[0] = 16.0f;  // num_steps

        // probability default = 1.0 (every cell always fires)
        for (std::size_t d = 0; d < layout::kDrumCount; ++d)
            for (int s = 0; s < static_cast<int>(layout::kStepCount); ++s)
                params[layout::prob_param_index(d, s)] = 1.0f;
        // mod_a (velocity) default = 0.5, mod_b default = 0.5, roll default = 1
        for (std::size_t d = 0; d < layout::kDrumCount; ++d) {
            for (int s = 0; s < static_cast<int>(layout::kStepCount); ++s) {
                params[layout::mod_a_param_index(d, s)] = 0.5f;
                params[layout::mod_b_param_index(d, s)] = 0.5f;
                params[layout::roll_param_index(d, s)]  = 1.0f;
            }
        }

        ctx.surface_width  = 1100.0f;
        ctx.surface_height = 600.0f;
        ctx.dpi_scale      = 1.0f;
        ctx.draw           = make_draw_api();
        ctx.commands.opaque            = &capture;
        ctx.commands.set_param         = capture_set_param;
        ctx.commands.set_string_param  = capture_set_string_param;
        ctx.param_values   = params.data();
        ctx.param_count    = static_cast<uint32_t>(params.size());
        ctx.output_values  = outputs.data();
        ctx.output_count   = static_cast<uint32_t>(outputs.size());
        ctx.mouse          = {};
        ctx.time           = 0.0;
        refresh_events();
    }

    void refresh_events() {
        ctx.events     = events.empty() ? nullptr : events.data();
        ctx.event_count = static_cast<uint32_t>(events.size());
    }
    void clear_capture() { capture.calls.clear(); }
    void clear_input() {
        events.clear(); refresh_events();
        ctx.mouse = {};
    }
    void draw() {
        refresh_events();
        ctx.wants_keyboard = 0;
        ctx.request_close  = 0;
        core.draw_editor(&ctx);
    }
};

constexpr int kGlfwPress        = 1;
constexpr int kGlfwKeySpace     = 32;
constexpr int kGlfwKeyDigit3    = 51;
constexpr int kGlfwKeyDigit7    = 55;
constexpr int kGlfwKeyA         = 65;
constexpr int kGlfwKeyB         = 66;
constexpr int kGlfwKeyC         = 67;
constexpr int kGlfwKeyP         = 80;
constexpr int kGlfwKeyV         = 86;
constexpr int kGlfwKeyEnter     = 257;
constexpr int kGlfwKeyRight     = 262;
constexpr int kGlfwKeyDown      = 264;
constexpr int kGlfwModShift     = 0x0001;
constexpr int kGlfwModSuper     = 0x0008;

VividEditorEvent make_key_event(int key, int modifiers = 0) {
    VividEditorEvent e{};
    e.type       = VIVID_EDITOR_EVENT_KEY;
    e.key        = key;
    e.action     = kGlfwPress;
    e.modifiers  = modifiers;
    return e;
}

void set_mouse_click(VividEditorMouse& mouse, float x, float y,
                     bool shift = false) {
    mouse.x = x; mouse.y = y;
    mouse.left_clicked = 1;
    mouse.shift_down = shift ? 1 : 0;
}

bool capture_has(const CaptureCtx& c, const char* name, float v) {
    for (const auto& call : c.calls) {
        if (call.name == name && call.value == v) return true;
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
    std::fprintf(stderr, "=== Test: DrumSequencer draw_editor (redesigned) ===\n\n");

    // --- editor metadata ---
    {
        VividEditorMetadata meta = DrumSequencerCore::editor_metadata();
        check(meta.default_width == 1100, "editor metadata: default_width = 1100");
        check(meta.default_height == 600, "editor metadata: default_height = 600");
        check(meta.title_suffix != nullptr &&
              std::strcmp(meta.title_suffix, "DrumSequencer Editor") == 0,
              "editor metadata: title_suffix = DrumSequencer Editor");
    }

    // --- draw_editor requests keyboard focus every frame ---
    {
        EditorHarness h;
        h.draw();
        check(h.ctx.wants_keyboard == 1, "draw_editor sets wants_keyboard = 1");
    }

    // --- Enter toggles the pattern-A trigger at the cursor ---
    {
        EditorHarness h;
        h.events = {make_key_event(kGlfwKeyEnter)};
        h.draw();
        check(h.capture.calls.size() == 1, "Enter emits one set_param");
        if (h.capture.calls.size() == 1) {
            check(h.capture.calls[0].name == "kick_0",
                  "Enter targets the cursor Pattern-A cell");
            check(h.capture.calls[0].value == 1.0f,
                  "Enter toggles inactive trigger to 1");
        }
    }

    // --- arrow keys move the cursor (collapse selection, no shift) ---
    {
        EditorHarness h;
        h.events = {
            make_key_event(kGlfwKeyRight),
            make_key_event(kGlfwKeyDown),
            make_key_event(kGlfwKeyEnter),
        };
        h.draw();
        check(h.capture.calls.size() == 1, "arrows + Enter emit one toggle");
        if (h.capture.calls.size() == 1) {
            check(h.capture.calls[0].name == "snare_1",
                  "cursor move retargets Enter to snare step 1");
        }
    }

    // --- Space clears the full set of six per-cell values ---
    {
        EditorHarness h;
        h.events = {
            make_key_event(kGlfwKeyRight),
            make_key_event(kGlfwKeyRight),
            make_key_event(kGlfwKeyDown),
            make_key_event(kGlfwKeySpace),
        };
        h.draw();
        check(h.capture.calls.size() == 6,
              "Space emits six set_param calls (trigA/trigB/modA/modB/prob/roll)");
        check(capture_has(h.capture, "snare_2",       0.0f), "Space clears pattern-A");
        check(capture_has(h.capture, "snare_b_2",     0.0f), "Space clears pattern-B");
        check(capture_has(h.capture, "snare_ma_2",    0.5f), "Space resets velocity");
        check(capture_has(h.capture, "snare_mb_2",    0.5f), "Space resets mod_b");
        check(capture_has(h.capture, "snare_prob_2",  1.0f), "Space resets probability");
        check(capture_has(h.capture, "snare_roll_2",  1.0f), "Space resets roll");
    }

    // --- roll shortcut: digit 1-4 sets roll count across selection ---
    {
        EditorHarness h;
        h.events = { make_key_event(kGlfwKeyDigit3) };
        h.draw();
        check(h.capture.calls.size() == 1, "digit key emits one set_param");
        if (h.capture.calls.size() == 1) {
            check(h.capture.calls[0].name == "kick_roll_0",
                  "digit key targets roll param");
            check(h.capture.calls[0].value == 3.0f,
                  "digit '3' sets roll count to 3");
        }
    }

    // --- probability shortcut: P then 7 → prob = 0.7 ---
    {
        EditorHarness h;
        h.events = { make_key_event(kGlfwKeyP) };
        h.draw();  // frame 1: enter prefix mode
        check(h.capture.calls.empty(),
              "pressing P alone does not emit a probability write");

        h.clear_input();
        h.events = { make_key_event(kGlfwKeyDigit7) };
        h.draw();  // frame 2: commit digit
        check(h.capture.calls.size() == 1, "P<digit> emits one prob write");
        if (h.capture.calls.size() == 1) {
            check(h.capture.calls[0].name == "kick_prob_0",
                  "P<digit> targets probability param at cursor");
            check(std::abs(h.capture.calls[0].value - 0.7f) < 1e-6f,
                  "P 7 writes probability = 0.7");
        }
    }

    // --- pattern A/B keys switch the active_pattern param ---
    {
        EditorHarness h;
        h.events = { make_key_event(kGlfwKeyB) };
        h.draw();
        check(h.capture.calls.size() == 1, "B key emits one set_param");
        if (h.capture.calls.size() == 1) {
            check(h.capture.calls[0].name == "active_pattern",
                  "B key targets active_pattern");
            check(h.capture.calls[0].value == 1.0f,
                  "B key selects pattern B (1)");
        }
        h.clear_input(); h.clear_capture();
        h.events = { make_key_event(kGlfwKeyA) };
        h.draw();
        check(h.capture.calls.size() == 1, "A key emits one set_param");
        check(capture_has(h.capture, "active_pattern", 0.0f),
              "A key selects pattern A (0)");
    }

    // --- Shift+Arrow extends the selection; Enter then toggles all cells ---
    {
        EditorHarness h;
        h.events = {
            make_key_event(kGlfwKeyRight, kGlfwModShift),  // extend to (0, 1)
            make_key_event(kGlfwKeyRight, kGlfwModShift),  // extend to (0, 2)
            make_key_event(kGlfwKeyDown,  kGlfwModShift),  // extend to (1, 2)
            make_key_event(kGlfwKeyEnter),                 // toggle 2×3 = 6 cells
        };
        h.draw();
        check(h.capture.calls.size() == 6,
              "Enter over a 2×3 selection emits 6 toggles");
        // Spot check: corner and opposite corner.
        check(capture_has_name(h.capture, "kick_0"),  "toggle includes kick step 0");
        check(capture_has_name(h.capture, "snare_2"), "toggle includes snare step 2");
        // All values should be the same (derived from anchor-cell state).
        for (const auto& c : h.capture.calls) {
            check(c.value == 1.0f, "selection toggle writes the same new value");
        }
    }

    // --- Grid click moves cursor + paints velocity; does NOT toggle trigger ---
    {
        EditorHarness h;
        const auto gm = compute_gm(h.ctx.surface_width, h.ctx.surface_height);
        // Click the middle of hat (drum 2), step 3.
        const float x = gm.cells_x + gm.cell_w * 3.5f;
        const float y = gm.cells_y + gm.cell_h * 2.5f;
        set_mouse_click(h.ctx.mouse, x, y);
        h.draw();
        check(h.capture.calls.size() == 1,
              "Grid click emits one set_param (velocity paint)");
        if (h.capture.calls.size() == 1) {
            check(h.capture.calls[0].name == "hat_ma_3",
                  "Grid click writes velocity (mod_a), not trigger");
        }
        // Follow up with Enter: it should now target hat step 3.
        h.clear_capture(); h.clear_input();
        h.events = { make_key_event(kGlfwKeyEnter) };
        h.draw();
        check(h.capture.calls.size() == 1, "Enter after click emits one toggle");
        if (h.capture.calls.size() == 1) {
            check(h.capture.calls[0].name == "hat_3",
                  "cursor now sits on hat step 3");
        }
    }

    // --- Shift+click extends selection from anchor ---
    {
        EditorHarness h;
        const auto gm = compute_gm(h.ctx.surface_width, h.ctx.surface_height);
        // Anchor click at kick step 0 (drum 0).
        set_mouse_click(h.ctx.mouse,
                        gm.cells_x + gm.cell_w * 0.5f,
                        gm.cells_y + gm.cell_h * 0.5f);
        h.draw();
        h.clear_capture(); h.clear_input();

        // Shift+click at snare step 2 (drum 1). No velocity paint should fire.
        set_mouse_click(h.ctx.mouse,
                        gm.cells_x + gm.cell_w * 2.5f,
                        gm.cells_y + gm.cell_h * 1.5f,
                        /*shift=*/true);
        h.draw();
        check(h.capture.calls.empty(),
              "Shift+click extends selection without writing params");

        // Enter now toggles a 2×3 rectangle (drum 0-1, step 0-2) = 6 cells.
        h.clear_capture(); h.clear_input();
        h.events = { make_key_event(kGlfwKeyEnter) };
        h.draw();
        check(h.capture.calls.size() == 6,
              "Enter after shift+click emits 6 toggles (2×3 rect)");
    }

    // --- Click on pattern A/B top-bar buttons sets active_pattern ---
    {
        EditorHarness h;
        // Pattern A button: grid_x + 140, top_y + 3, 28×20. Click centre.
        set_mouse_click(h.ctx.mouse, kInset + 140.0f + 14.0f, kInset + 3.0f + 10.0f);
        h.draw();
        check(capture_has(h.capture, "active_pattern", 0.0f),
              "Pattern A button writes active_pattern = 0");
        h.clear_capture(); h.clear_input();
        set_mouse_click(h.ctx.mouse, kInset + 140.0f + 32.0f + 14.0f,
                        kInset + 3.0f + 10.0f);
        h.draw();
        check(capture_has(h.capture, "active_pattern", 1.0f),
              "Pattern B button writes active_pattern = 1");
    }

    // --- Cmd+C / Cmd+V on a multi-cell selection round-trips all six params ---
    {
        EditorHarness h;
        // Populate step 0-1 × drums 0-1 with a distinctive pattern.
        h.params[layout::trigger_param_index(0, 0)] = 1.0f;
        h.params[layout::mod_a_param_index(0, 0)]   = 0.75f;
        h.params[layout::trig_b_param_index(1, 1)]  = 1.0f;
        h.params[layout::prob_param_index(1, 0)]    = 0.4f;
        h.params[layout::roll_param_index(0, 1)]    = 3.0f;

        // Shift+Right, Shift+Down to build a 2×2 selection.
        h.events = {
            make_key_event(kGlfwKeyRight, kGlfwModShift),
            make_key_event(kGlfwKeyDown,  kGlfwModShift),
            make_key_event(kGlfwKeyC, kGlfwModSuper),
        };
        h.draw();
        check(h.capture.calls.empty(), "Cmd+C does not emit set_param");

        // After Shift+Right + Shift+Down, cursor is at (drum=1, step=1) and
        // the selection is {0,1} × {0,1}. Plain arrows then collapse the
        // selection and move the cursor — two plain Rights land us at
        // (drum=1, step=3) with a point selection at that cell.
        h.clear_input(); h.clear_capture();
        h.events = {
            make_key_event(kGlfwKeyRight),
            make_key_event(kGlfwKeyRight),
        };
        h.draw();
        h.clear_input(); h.clear_capture();

        // Paste emits 4 cells × 6 params = 24 writes, origin = (1, 3).
        h.events = { make_key_event(kGlfwKeyV, kGlfwModSuper) };
        h.draw();
        check(h.capture.calls.size() == 24,
              "Cmd+V over a 2×2 copy emits 24 set_param calls (4 cells × 6 params)");

        // Origin cell (clip 0,0) came from source (drum=0, step=0):
        //   trigger_a=1.0, velocity=0.75, others default.
        check(capture_has(h.capture, "snare_3", 1.0f),
              "paste preserves trigger A at origin (drum=1, step=3)");
        check(capture_has(h.capture, "snare_ma_3", 0.75f),
              "paste preserves velocity at origin");
        // Relative (1,1) came from source (drum=1, step=1): trigger_b=1.
        check(capture_has(h.capture, "hat_b_4", 1.0f),
              "paste preserves trigger B at relative (1,1)");
        // Relative (0,1) came from (drum=0, step=1): roll=3.
        check(capture_has(h.capture, "snare_roll_4", 3.0f),
              "paste preserves roll at relative (0,1)");
        // Relative (1,0) came from (drum=1, step=0): prob=0.4.
        check(capture_has(h.capture, "hat_prob_3", 0.4f),
              "paste preserves probability at relative (1,0)");
    }

    // --- null command callbacks stay safe ---
    {
        EditorHarness h;
        h.ctx.commands.set_param        = nullptr;
        h.ctx.commands.set_string_param = nullptr;
        h.events = { make_key_event(kGlfwKeyEnter), make_key_event(kGlfwKeySpace) };
        const auto gm = compute_gm(h.ctx.surface_width, h.ctx.surface_height);
        set_mouse_click(h.ctx.mouse,
                        gm.cells_x + gm.cell_w * 0.5f,
                        gm.cells_y + gm.cell_h * 0.5f);
        h.draw();
        check(h.capture.calls.empty(), "null command callbacks emit no captured writes");
        check(true, "draw_editor with null command callbacks does not crash");
    }

    // --- grid clicks beyond the active step count are ignored ---
    {
        EditorHarness h;
        h.params[0] = 8.0f;  // num_steps
        const auto gm = compute_gm(h.ctx.surface_width, h.ctx.surface_height);
        set_mouse_click(h.ctx.mouse,
                        gm.cells_x + gm.cell_w * 10.5f,
                        gm.cells_y + gm.cell_h * 0.5f);
        h.draw();
        check(h.capture.calls.empty(), "click beyond num_steps emits no commands");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
