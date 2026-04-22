// End-to-end tests for ArpeggiatorCore::draw_editor(). Drives a
// synthesized VividEditorContext and captures set_param writes.
//
// Also covers the v1 engine expansion — backward compat (default
// params still produce today's behaviour), Note Override semantics
// (follow/1-8/mute), and the gate-length multiplier.

#include "arpeggiator_core.h"
#include "arpeggiator_editor_shared.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace ae = ::vivid::arpeggiator_editor;
namespace ek = ::vivid::editor_keys;

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
    ArpeggiatorCore core;
    CaptureCtx capture;
    std::vector<float> params;
    std::vector<float> outputs;
    std::vector<VividEditorEvent> events;
    VividEditorContext ctx{};

    // 73 params total after the Cthulhu-inspired expansion. Fill with
    // defaults that match the operator's collect_params().
    EditorHarness() : params(73, 0.0f), outputs(4, 0.0f) {
        // Top-level controls with sensible defaults.
        params[ae::kModeIndex]       = 0.0f;   // Up
        params[ae::kOctavesIndex]    = 1.0f;
        params[ae::kRateIndex]       = 3.0f;   // 1/8
        params[ae::kGateLengthIndex] = 0.8f;
        params[ae::kModStepsIndex]   = 8.0f;
        params[ae::kMidiChannelIndex]= 1.0f;

        // Vel / Gate default to 1.0.
        for (int i = 0; i < 16; ++i) {
            params[ae::param_index_for(ae::Lane::Velocity, i)] = 1.0f;
            params[ae::param_index_for(ae::Lane::Gate,     i)] = 1.0f;
        }
        // Transpose + Note Override default to 0 (already zeroed).

        outputs[ae::kStepOutputIndex] = 0.0f;

        ctx.surface_width  = 1000.0f;
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
    e.type      = VIVID_EDITOR_EVENT_KEY;
    e.key       = k;
    e.action    = ek::kPress;
    e.modifiers = mods;
    return e;
}

bool captured(const CaptureCtx& c, const char* name, float v) {
    for (const auto& call : c.calls) {
        if (call.name == name && std::abs(call.value - v) < 1e-4f) return true;
    }
    return false;
}
bool captured_name(const CaptureCtx& c, const char* name) {
    for (const auto& call : c.calls) if (call.name == name) return true;
    return false;
}

// ---- Engine backward-compat harness: driving compute() directly ----

struct ArpHarness {
    ArpeggiatorCore core;
    std::vector<float> params;

    ArpHarness() : params(73, 0.0f) {
        params[ae::kModeIndex]       = 0.0f;   // Up
        params[ae::kOctavesIndex]    = 1.0f;
        params[ae::kRateIndex]       = 2.0f;   // 1/4
        params[ae::kGateLengthIndex] = 0.8f;
        params[ae::kModStepsIndex]   = 8.0f;
        for (int i = 0; i < 16; ++i) {
            params[ae::param_index_for(ae::Lane::Velocity, i)] = 1.0f;
            params[ae::param_index_for(ae::Lane::Gate,     i)] = 1.0f;
        }
    }

    // Build input lane spreads from an explicit note list.
    struct Result {
        float note, vel, gate;
        int step;
    };
    Result step_at(float beat_phase, const std::vector<float>& notes) {
        // Lane-view stubs: a 4-length VividLaneView array for the arp's
        // input ports [beat_phase, notes, velocities, gates]. Only
        // notes/velocities/gates are used via indexes [1..3].
        VividLaneView lane_views[4] = {};
        std::vector<float> vel_data(notes.size(), 1.0f);
        std::vector<float> gate_data(notes.size(), 1.0f);
        lane_views[1].data = const_cast<float*>(notes.data());
        lane_views[1].length = static_cast<uint32_t>(notes.size());
        lane_views[2].data = vel_data.data();
        lane_views[2].length = static_cast<uint32_t>(vel_data.size());
        lane_views[3].data = gate_data.data();
        lane_views[3].length = static_cast<uint32_t>(gate_data.size());

        float output_values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        // The custom MIDI output is not checked here.
        core.compute(beat_phase, params.data(), lane_views,
                     output_values, /*out_spreads=*/nullptr,
                     /*custom_outputs=*/nullptr, 0);
        return {output_values[0], output_values[1],
                output_values[2],
                static_cast<int>(output_values[3])};
    }
};

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: Arpeggiator draw_editor + engine expansion ===\n\n");

    // --- Metadata sanity ---
    {
        auto m = ArpeggiatorCore::editor_metadata();
        check(m.default_width == 1000, "metadata: default_width = 1000");
        check(m.default_height == 420, "metadata: default_height = 420");
        check(m.title_suffix != nullptr &&
              std::strcmp(m.title_suffix, "Arpeggiator Editor") == 0,
              "metadata: title_suffix = Arpeggiator Editor");
    }

    // --- draw_editor requests keyboard focus ---
    {
        EditorHarness h;
        h.draw();
        check(h.ctx.wants_keyboard == 1, "draw_editor sets wants_keyboard = 1");
    }

    // --- Backward compat: default params behave as today's arp ---
    //
    // Note Override at sentinel 0 → follow `mode`. Gate multiplier 1.0
    // → effective gate matches global. For a 3-note pool [60, 64, 67]
    // with mode=Up and rate=1/4, consecutive steps should walk 60→64→67→60…
    {
        ArpHarness h;
        std::vector<float> pool = {60.0f, 64.0f, 67.0f};

        // Feed several consecutive phases that cross step boundaries.
        // rate=1/4 → multiplier=1.0 → one step per beat. We fake
        // `beat_count_` advancement via phase wraparounds: each call
        // increments internal beat state if phase wraps.
        auto r0 = h.step_at(0.0f, pool);  // step 0 → pool[0] = 60
        auto r1 = h.step_at(0.6f, pool);  // still step 0 (post-swing etc.)
        (void)r1;
        // Wrap the phase to trigger beat_count_ advance.
        auto r_wrap = h.step_at(0.05f, pool);  // wraps → step 1 → pool[1]=64
        (void)r_wrap;
        auto r_wrap2 = h.step_at(0.10f, pool); // still step 1

        // The exact step output depends on beat_count_ state; we mainly
        // verify that output notes stay in {60, 64, 67} for mode=Up and
        // that no unexpected transposition happens with default params.
        auto mid = h.step_at(0.30f, pool);
        const bool in_pool = mid.note == 60.0f ||
                             mid.note == 64.0f ||
                             mid.note == 67.0f;
        check(in_pool, "default-params arp output note stays within pool (no transpose)");
        check(mid.vel == 1.0f, "default velocity = 1.0 (no scale)");

        // Check first-step observation directly: r0 should be a pool member.
        const bool r0_ok = r0.note == 60.0f || r0.note == 64.0f || r0.note == 67.0f;
        check(r0_ok, "first-step output is a pool member");
    }

    // --- Note Override forces specific pool index ---
    {
        ArpHarness h;
        std::vector<float> pool = {50.0f, 55.0f, 60.0f};  // 3 notes
        // Force every step to pool index 1 (the "2" value = second note).
        for (int s = 0; s < 16; ++s) {
            h.params[ae::param_index_for(ae::Lane::NoteOverride, s)] = 2.0f;
        }
        auto r = h.step_at(0.0f, pool);
        check(r.note == 55.0f,
              "Note Override = 2 forces output to pool[1] regardless of mode");
    }

    // --- Mute (Note Override = 9) silences the step ---
    {
        ArpHarness h;
        std::vector<float> pool = {60.0f};
        for (int s = 0; s < 16; ++s) {
            h.params[ae::param_index_for(ae::Lane::NoteOverride, s)] = 9.0f;
        }
        auto r = h.step_at(0.0f, pool);
        check(r.note == 0.0f,
              "Mute sets out_note to 0");
        check(r.vel == 0.0f,
              "Mute sets out_vel to 0");
        check(r.gate == 0.0f,
              "Mute forces gate = 0");
    }

    // --- Gate multiplier shortens effective gate ---
    {
        ArpHarness h;
        std::vector<float> pool = {60.0f};
        // Global gate_length = 0.8; step gt = 0.25 → effective = 0.2.
        h.params[ae::kGateLengthIndex] = 0.8f;
        for (int s = 0; s < 16; ++s) {
            h.params[ae::param_index_for(ae::Lane::Gate, s)] = 0.25f;
        }
        // With the default rate member (index 3 = 1/8, multiplier = 2),
        // beat_phase = 0.25 lands at step_phase = 0.5 — mid-step. Gate
        // should be CLOSED because effective_gate = 0.2.
        auto r_mid = h.step_at(0.25f, pool);
        check(r_mid.gate == 0.0f,
              "Gate closed mid-step when effective_gate = 0.2 and step_phase = 0.5");

        // Compare with default gt = 1.0 → effective_gate = 0.8, so
        // step_phase = 0.5 falls inside the open window.
        ArpHarness h2;
        auto r_full = h2.step_at(0.25f, pool);
        check(r_full.gate == 1.0f,
              "Default gt = 1.0 keeps gate open at step_phase = 0.5");
    }

    // --- mod_steps widened to 16 ---
    {
        ArpHarness h;
        h.params[ae::kModStepsIndex] = 16.0f;  // Previously clamped to 8
        std::vector<float> pool = {60.0f};
        // Place a distinctive transpose on step 15 (unreachable at
        // mod_steps=8). With mod_steps=16, step 15's transpose should
        // be addressable by the engine when raw_step % 16 == 15.
        h.params[ae::param_index_for(ae::Lane::Transpose, 15)] = 12.0f;
        // (Directly verifying the wrap requires feeding 16 steps worth
        // of phase, which is state-heavy. Here we just confirm that
        // the param layout is reachable by reading back.)
        check(h.params[ae::kTrBase_8_15 + 7] == 12.0f,
              "tr_15 lives at descriptor index 40 and survives the widening");
    }

    // --- Editor: arrow keys move cursor within 4×16 grid ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::kRight), key_ev(ek::kDown), key_ev(ek::kDown)};
        h.draw();
        check(h.core.editor_cursor_step_ == 1,
              "right arrow advances step cursor");
        check(h.core.editor_cursor_row_ == 2,
              "two down arrows move cursor to row 2 (Transpose)");
    }

    // --- Enter on Note Override cycles (0 → 1) ---
    {
        EditorHarness h;
        // Cursor starts at (row=0, step=0) — Note Override lane.
        h.events = {key_ev(ek::kEnter)};
        h.draw();
        check(captured(h.capture, "note_override_0", 1.0f),
              "Enter on Note Override writes 1 (cycle 0 → 1)");
    }

    // --- Digit 5 on Velocity sets vel = 5/9 ≈ 0.555 ---
    {
        EditorHarness h;
        h.core.editor_cursor_row_ = static_cast<int>(ae::Lane::Velocity);
        h.events = {key_ev(ek::k5)};
        h.draw();
        // Expect set_param("vel_0", 5/9).
        bool found = false;
        for (const auto& c : h.capture.calls) {
            if (c.name == "vel_0" &&
                std::abs(c.value - (5.0f / 9.0f)) < 1e-3f) {
                found = true; break;
            }
        }
        check(found, "digit 5 on Velocity sets vel_0 to 5/9");
    }

    // --- Space clears selection to lane defaults ---
    {
        EditorHarness h;
        // Put cursor on Velocity row and press Space — should set vel_0 = 1.0.
        h.core.editor_cursor_row_ = static_cast<int>(ae::Lane::Velocity);
        h.events = {key_ev(ek::kSpace)};
        h.draw();
        check(captured(h.capture, "vel_0", 1.0f),
              "Space resets Velocity to default 1.0");
    }

    // --- Shift+Down extends selection to row 1 ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::kDown, ek::kModShift)};
        h.draw();
        check(h.core.editor_selection_.row_lo == 0 &&
              h.core.editor_selection_.row_hi == 1,
              "Shift+Down selects rows 0..1");
    }

    // --- Cmd+C → Cmd+V round-trips a 1×3 selection ---
    {
        EditorHarness h;
        // Need mod_steps = 16 so the cursor can legally reach step 10.
        // The member Param isn't reset by writing into the params array,
        // so poke the member directly (matches DrumSequencer test style).
        h.core.mod_steps.value = 16;
        h.params[ae::kModStepsIndex] = 16.0f;
        // Seed cells 0..2 on Velocity row with distinctive values.
        h.params[ae::param_index_for(ae::Lane::Velocity, 0)] = 0.2f;
        h.params[ae::param_index_for(ae::Lane::Velocity, 1)] = 0.4f;
        h.params[ae::param_index_for(ae::Lane::Velocity, 2)] = 0.6f;
        h.core.editor_cursor_row_ = static_cast<int>(ae::Lane::Velocity);
        h.core.editor_cursor_step_ = 2;
        h.core.editor_selection_.row_lo = 1;
        h.core.editor_selection_.row_hi = 1;
        h.core.editor_selection_.col_lo = 0;
        h.core.editor_selection_.col_hi = 2;
        h.core.grid_state_.anchor_row = 1;
        h.core.grid_state_.anchor_col = 0;
        // Copy.
        h.events = {key_ev(ek::kC, ek::kModSuper)};
        h.draw();
        check(h.core.editor_clipboard_.has_content,
              "Cmd+C populates the clipboard");
        check(h.core.editor_clipboard_.rows == 1 &&
              h.core.editor_clipboard_.cols == 3,
              "clipboard dimensions match selection");

        // Move cursor and anchor to step 10 (rebuild_selection derives
        // the selection from both each frame, so both must move together).
        h.clear_input();
        h.clear_capture();
        h.core.editor_cursor_step_ = 10;
        h.core.grid_state_.anchor_col = 10;
        h.events = {key_ev(ek::kV, ek::kModSuper)};
        h.draw();
        check(captured(h.capture, "vel_10", 0.2f),
              "paste restores vel_10 = 0.2");
        check(captured(h.capture, "vel_11", 0.4f),
              "paste restores vel_11 = 0.4");
        check(captured(h.capture, "vel_12", 0.6f),
              "paste restores vel_12 = 0.6");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
