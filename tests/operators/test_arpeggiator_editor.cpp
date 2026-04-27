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

// After Phase 5 PR3 the Arpeggiator consumes a native VividNoteBuffer on
// notes_in. The harness manages held-set state across step_at() calls so
// existing tests can keep passing a "current notes" pool each tick — we
// diff vs. the previously-held set and emit ON/OFF events as needed.
struct ArpHarness {
    ArpeggiatorCore core;
    std::vector<float> params;

    // Per-pitch tracking so we can emit NOTE_OFF for departed notes.
    struct HeldEntry {
        float    note;
        uint64_t note_id;
    };
    std::vector<HeldEntry> currently_held;

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

    struct Result {
        float note, vel, gate;
        int step;
    };

    // Build a VividNoteBuffer carrying ON/OFF deltas to bring the held
    // set in line with `notes`, then run one compute() tick.
    Result step_at(float beat_phase, const std::vector<float>& notes) {
        VividNoteBuffer buf{};

        // NOTE_OFF for any previously-held note that's not in the new set.
        for (auto it = currently_held.begin(); it != currently_held.end(); ) {
            bool still_present = false;
            for (float n : notes) if (n == it->note) { still_present = true; break; }
            if (!still_present) {
                vivid_sequencers::note_off(buf, it->note_id);
                it = currently_held.erase(it);
            } else {
                ++it;
            }
        }

        // NOTE_ON for any new note that isn't already held.
        for (float n : notes) {
            bool already_held = false;
            for (auto& e : currently_held) if (e.note == n) { already_held = true; break; }
            if (!already_held) {
                uint64_t id = vivid_sequencers::next_note_id();
                vivid_sequencers::note_on(buf,
                    static_cast<uint8_t>(std::clamp(static_cast<int>(n), 0, 127)),
                    /*velocity=*/1.0f, id);
                currently_held.push_back({n, id});
            }
        }

        float output_values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        // custom_outputs not inspected here.
        core.compute(beat_phase, params.data(), &buf,
                     output_values,
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

    // --- PR3: Held-set semantics on notes_in (NOTE_OFF removes from rotation) ---
    {
        ArpeggiatorCore core;
        std::vector<float> params(73, 0.0f);
        params[ae::kModeIndex]       = 0.0f;   // Up
        params[ae::kOctavesIndex]    = 1.0f;
        params[ae::kRateIndex]       = 2.0f;   // 1/4
        params[ae::kGateLengthIndex] = 0.8f;
        params[ae::kModStepsIndex]   = 8.0f;
        for (int i = 0; i < 16; ++i) {
            params[ae::param_index_for(ae::Lane::Velocity, i)] = 1.0f;
            params[ae::param_index_for(ae::Lane::Gate,     i)] = 1.0f;
        }
        // Send three NOTE_ON events: 60, 64, 67. Then NOTE_OFF the 64.
        VividNoteBuffer buf{};
        uint64_t id_60 = vivid_sequencers::next_note_id();
        uint64_t id_64 = vivid_sequencers::next_note_id();
        uint64_t id_67 = vivid_sequencers::next_note_id();
        vivid_sequencers::note_on(buf, 60, 1.0f, id_60);
        vivid_sequencers::note_on(buf, 64, 1.0f, id_64);
        vivid_sequencers::note_on(buf, 67, 1.0f, id_67);
        vivid_sequencers::note_off(buf, id_64);
        float out_values[4] = {};
        core.compute(0.0f, params.data(), &buf, out_values, nullptr, 0);
        // Pool should contain only 60 and 67. Step 0 lands on the lowest
        // pitch under "Up" mode, which is 60.
        check(out_values[0] == 60.0f,
              "PR3: NOTE_OFF on the middle held note removed it from the pool");
    }

    // --- PR3: re-triggering same MIDI pitch with distinct note_ids → distinct held entries ---
    {
        ArpeggiatorCore core;
        std::vector<float> params(73, 0.0f);
        params[ae::kModeIndex]       = 0.0f;
        params[ae::kOctavesIndex]    = 1.0f;
        params[ae::kRateIndex]       = 2.0f;
        params[ae::kGateLengthIndex] = 0.8f;
        params[ae::kModStepsIndex]   = 8.0f;
        for (int i = 0; i < 16; ++i) {
            params[ae::param_index_for(ae::Lane::Velocity, i)] = 1.0f;
            params[ae::param_index_for(ae::Lane::Gate,     i)] = 1.0f;
        }
        // Two NOTE_ONs for the same pitch with distinct note_ids.
        // Force pool index 1 via note_override → if there are 2 entries,
        // the engine resolves index 1 and emits the same pitch (60); if
        // there's only 1 entry, the clamp pulls index back to 0 anyway.
        // Better assertion: NOTE_OFF only the FIRST note_id and verify
        // the second note still produces output.
        VividNoteBuffer buf_a{};
        uint64_t id_a = vivid_sequencers::next_note_id();
        uint64_t id_b = vivid_sequencers::next_note_id();
        vivid_sequencers::note_on(buf_a, 60, 1.0f, id_a);
        vivid_sequencers::note_on(buf_a, 60, 0.5f, id_b);
        float out_a[4] = {};
        core.compute(0.0f, params.data(), &buf_a, out_a, nullptr, 0);
        check(out_a[0] == 60.0f && out_a[2] == 1.0f,
              "PR3: same-pitch overlap still produces the held pitch on step 0");

        // Frame 2: NOTE_OFF only id_a. id_b should still drive output.
        VividNoteBuffer buf_b{};
        vivid_sequencers::note_off(buf_b, id_a);
        float out_b[4] = {};
        core.compute(0.0f, params.data(), &buf_b, out_b, nullptr, 0);
        check(out_b[0] == 60.0f && out_b[2] == 1.0f,
              "PR3: NOTE_OFF on one of two same-pitch entries leaves the other held (note_id semantics)");
    }

    // --- PR3: held-note pressure → arp emits PRESSURE on each step's note_id (snapshot-and-bake) ---
    {
        ArpeggiatorCore core;
        std::vector<float> params(73, 0.0f);
        params[ae::kModeIndex]       = 0.0f;
        params[ae::kOctavesIndex]    = 1.0f;
        params[ae::kRateIndex]       = 2.0f;
        params[ae::kGateLengthIndex] = 0.8f;
        params[ae::kModStepsIndex]   = 8.0f;
        for (int i = 0; i < 16; ++i) {
            params[ae::param_index_for(ae::Lane::Velocity, i)] = 1.0f;
            params[ae::param_index_for(ae::Lane::Gate,     i)] = 1.0f;
        }

        // NOTE_ON + PRESSURE on the held note before the first step fires.
        VividNoteBuffer buf{};
        uint64_t held_id = vivid_sequencers::next_note_id();
        vivid_sequencers::note_on(buf, 60, 1.0f, held_id);
        vivid_sequencers::note_pressure(buf, held_id, 0.75f);

        VividNoteBuffer arp_out{};
        void* custom_outputs[1] = {&arp_out};
        float out_values[4] = {};
        core.compute(0.0f, params.data(), &buf, out_values, custom_outputs, 1);

        // arp_out should contain a NOTE_ON followed by a PRESSURE event
        // keyed on the new step's note_id (NOT the held source's note_id).
        auto* result_buf = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        check(result_buf != nullptr, "PR3: arp wrote to custom_outputs[0]");
        bool saw_on = false;
        bool saw_pressure_baked = false;
        uint64_t step_id = 0;
        for (uint32_t i = 0; result_buf && i < result_buf->count; ++i) {
            const auto& e = result_buf->events[i];
            if (e.type == VIVID_NOTE_ON) {
                saw_on = true;
                step_id = e.note_id;
            } else if (e.type == VIVID_NOTE_PRESSURE) {
                check(step_id != 0, "PR3: PRESSURE follows NOTE_ON in the same emit");
                check(e.note_id == step_id,
                      "PR3: baked PRESSURE keyed on the new step's note_id, not the held source's id");
                check_float(e.value, 0.75f, 1e-4f,
                            "PR3: baked PRESSURE value matches the held source's last pressure");
                check(e.note_id != held_id,
                      "PR3: arp's emitted note_id is fresh, not reused from the held source");
                saw_pressure_baked = true;
            }
        }
        check(saw_on, "PR3: arp emitted NOTE_ON for step 0");
        check(saw_pressure_baked,
              "PR3: arp baked the held source's PRESSURE onto the emitted note");
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
