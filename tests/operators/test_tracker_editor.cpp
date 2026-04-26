// End-to-end tests for TrackerCore::draw_editor(). Drives a synthesized
// VividEditorContext through keyboard/mouse flows and captures
// set_string_param calls on "pattern_data" — each capture is
// deserialized so we can assert on the resulting TrackerSong state.

#include "tracker_core.h"
#include "tracker_editor_shared.h"
#include "tracker_data.h"
#include "operator_api/editor_keys.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace te = ::vivid::tracker_editor;
namespace ek = ::vivid::editor_keys;

namespace {

struct CapturedSet {
    std::string name;
    float value = 0.0f;
};
struct CapturedStringSet {
    std::string name;
    std::string value;
};
struct CaptureCtx {
    std::vector<CapturedSet>       calls;
    std::vector<CapturedStringSet> string_calls;
};

void capture_set_param(void* opaque, const char* name, float v) {
    auto* c = static_cast<CaptureCtx*>(opaque);
    if (c) c->calls.push_back({std::string(name ? name : ""), v});
}
void capture_set_string_param(void* opaque, const char* name, const char* v) {
    auto* c = static_cast<CaptureCtx*>(opaque);
    if (c) c->string_calls.push_back(
        {std::string(name ? name : ""), std::string(v ? v : "")});
}

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
    TrackerCore core;
    CaptureCtx capture;
    std::vector<VividEditorEvent> events;
    VividEditorContext ctx{};

    EditorHarness() {
        // Seed with a minimal 16-row empty song so the editor has bounds.
        ::tracker::TrackerSong song;
        core.pattern_data.str_value = ::tracker::serialize_song(song);

        ctx.surface_width  = 1200.0f;
        ctx.surface_height = 720.0f;
        ctx.dpi_scale      = 1.0f;
        ctx.draw           = make_draw_api();
        ctx.commands.opaque           = &capture;
        ctx.commands.set_param        = capture_set_param;
        ctx.commands.set_string_param = capture_set_string_param;
        ctx.param_values  = nullptr;
        ctx.param_count   = 0;
        ctx.output_values = nullptr;
        ctx.output_count  = 0;
        ctx.mouse         = {};
        ctx.time          = 0.0;
        refresh_events();
    }
    void refresh_events() {
        ctx.events      = events.empty() ? nullptr : events.data();
        ctx.event_count = static_cast<uint32_t>(events.size());
    }
    void clear() {
        events.clear();
        refresh_events();
        ctx.mouse = {};
        capture.calls.clear();
        capture.string_calls.clear();
    }
    void draw() {
        refresh_events();
        ctx.wants_keyboard = 0;
        core.draw_editor(&ctx);
        // Mirror the runtime's command-queue routing: apply the latest
        // pattern_data write back to the core so the next frame sees
        // it. In the real app this round-trip happens automatically via
        // set_string_param → runtime → operator-param update.
        apply_last_pattern_data();
    }
    void apply_last_pattern_data() {
        for (auto it = capture.string_calls.rbegin();
             it != capture.string_calls.rend(); ++it) {
            if (it->name == "pattern_data") {
                core.pattern_data.str_value = it->value;
                return;
            }
        }
    }
    // Convenience: deserialize the last serialized song so tests can
    // assert on specific cells.
    ::tracker::TrackerSong last_song() {
        ::tracker::TrackerSong s;
        for (auto it = capture.string_calls.rbegin();
             it != capture.string_calls.rend(); ++it) {
            if (it->name == "pattern_data") {
                ::tracker::deserialize_song(it->value, s);
                return s;
            }
        }
        return s;
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

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: Tracker draw_editor ===\n\n");

    // --- Metadata sanity ---
    {
        auto m = TrackerCore::editor_metadata();
        check(m.default_width  == 1200, "metadata: default_width = 1200");
        check(m.default_height == 720,  "metadata: default_height = 720");
        check(m.title_suffix != nullptr &&
              std::strcmp(m.title_suffix, "Tracker Editor") == 0,
              "metadata: title_suffix = Tracker Editor");
    }

    // --- wants_keyboard set every frame ---
    {
        EditorHarness h;
        h.draw();
        check(h.ctx.wants_keyboard == 1,
              "draw_editor sets wants_keyboard = 1");
    }

    // --- Piano-row note entry writes pattern_data ---
    {
        EditorHarness h;
        h.core.editor_octave_ = 4;
        // Cursor defaults to (row 0, ch 0, Note). 'Z' → C-4 = MIDI 60.
        h.events = {key_ev(ek::kZ)};
        h.draw();
        check(!h.capture.string_calls.empty(),
              "note entry fires set_string_param");
        auto song = h.last_song();
        check(song.patterns[0].cells[0][0].note == 60,
              "Z at octave 4 writes MIDI 60 (C-4)");
        check(song.patterns[0].cells[0][0].velocity == 0x7F,
              "first note gets default velocity 0x7F");
        check(h.core.editor_cursor_row_ == 1,
              "cursor auto-advances after note entry");
    }

    // --- Upper-octave row (Q) bumps octave by one ---
    {
        EditorHarness h;
        h.core.editor_octave_ = 4;
        h.events = {key_ev(ek::kQ)};
        h.draw();
        auto song = h.last_song();
        check(song.patterns[0].cells[0][0].note == 72,
              "Q at octave 4 writes MIDI 72 (C-5)");
    }

    // --- `A` key writes note-off ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::kA)};
        h.draw();
        auto song = h.last_song();
        check(song.patterns[0].cells[0][0].note == ::tracker::NOTE_OFF,
              "A writes note-off sentinel");
    }

    // --- Arrow keys move cursor without committing ---
    {
        EditorHarness h;
        h.events = {
            key_ev(ek::kDown), key_ev(ek::kDown), key_ev(ek::kRight),
        };
        h.draw();
        check(h.core.editor_cursor_row_ == 2,
              "two Down-arrows advance cursor to row 2");
        check(h.core.editor_cursor_field_ == te::Field::Velocity,
              "Right-arrow from Note moves to Velocity field");
        check(h.capture.string_calls.empty(),
              "arrow nav alone does not write pattern_data");
    }

    // --- Tab jumps to next channel, Note field ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::kTab)};
        h.draw();
        check(h.core.editor_cursor_channel_ == 1,
              "Tab moves to channel 1");
        check(h.core.editor_cursor_field_ == te::Field::Note,
              "Tab resets field to Note");
    }

    // --- Velocity hex entry: first char → high nibble ---
    {
        EditorHarness h;
        // Enter a note first to park us, then move to velocity field.
        h.events = {key_ev(ek::kZ)};
        h.draw();
        h.apply_last_pattern_data();
        // Back up to row 0, velocity field.
        h.clear();
        h.core.editor_cursor_row_ = 0;
        h.core.editor_cursor_field_ = te::Field::Velocity;

        h.events = {key_ev(ek::k8)};  // first hex char: 0x80
        h.draw();
        auto song = h.last_song();
        check(song.patterns[0].cells[0][0].velocity == 0x80,
              "first velocity char sets high nibble → 0x80");
        check(h.core.editor_vel_chars_ == 1,
              "velocity accumulator at 1 char");
        check(h.core.editor_cursor_row_ == 0,
              "cursor does not advance mid-hex-entry");

        h.clear();
        h.events = {key_ev(ek::k5)};  // second hex char: 0x85
        h.draw();
        song = h.last_song();
        check(song.patterns[0].cells[0][0].velocity == 0x85,
              "second velocity char fills low nibble → 0x85");
        check(h.core.editor_vel_chars_ == 0,
              "velocity accumulator resets after commit");
        check(h.core.editor_cursor_row_ == 1,
              "cursor advances after velocity entry completes");
    }

    // --- Effect hex entry: 3 chars across type + param ---
    {
        EditorHarness h;
        h.core.editor_cursor_field_ = te::Field::Effect;
        h.events = {key_ev(ek::kF)};  // 'F' is 0xF
        h.draw();
        h.apply_last_pattern_data();
        auto song = h.last_song();
        check(song.patterns[0].cells[0][0].effect_type == 0x0F,
              "first fx char sets effect_type");

        h.clear();
        h.events = {key_ev(ek::k2)};
        h.draw();
        h.apply_last_pattern_data();
        song = h.last_song();
        check(song.patterns[0].cells[0][0].effect_param == 0x20,
              "second fx char sets param high nibble");

        h.clear();
        h.events = {key_ev(ek::k6)};
        h.draw();
        song = h.last_song();
        check(song.patterns[0].cells[0][0].effect_param == 0x26,
              "third fx char fills param low nibble → 0x26");
        check(h.core.editor_cursor_row_ == 1,
              "cursor advances after fx entry completes");
    }

    // --- Delete clears the cell ---
    {
        EditorHarness h;
        ::tracker::TrackerSong pre;
        pre.patterns[0].cells[0][0] = {60, 0x7F, 0x0F, 0x10};
        h.core.pattern_data.str_value = ::tracker::serialize_song(pre);
        h.events = {key_ev(ek::kDelete)};
        h.draw();
        auto song = h.last_song();
        check(song.patterns[0].cells[0][0].note == 0,
              "Delete clears note");
        check(song.patterns[0].cells[0][0].velocity == 0,
              "Delete clears velocity");
        check(song.patterns[0].cells[0][0].effect_type == 0,
              "Delete clears effect type");
    }

    // --- Backspace scrubs the current field only ---
    {
        EditorHarness h;
        ::tracker::TrackerSong pre;
        pre.patterns[0].cells[0][0] = {60, 0x7F, 0x0F, 0x10};
        h.core.pattern_data.str_value = ::tracker::serialize_song(pre);
        h.core.editor_cursor_field_ = te::Field::Velocity;
        h.events = {key_ev(ek::kBackspace)};
        h.draw();
        auto song = h.last_song();
        check(song.patterns[0].cells[0][0].note == 60,
              "Backspace on velocity leaves note alone");
        check(song.patterns[0].cells[0][0].velocity == 0,
              "Backspace on velocity zeroes velocity");
    }

    // --- Pattern navigation: `=` → next pattern via set_param ---
    {
        EditorHarness h;
        // Song needs >=2 patterns to advance.
        ::tracker::TrackerSong pre;
        pre.num_patterns = 2;
        h.core.pattern_data.str_value = ::tracker::serialize_song(pre);
        h.events = {key_ev(ek::kEqual)};
        h.draw();
        bool found = false;
        for (auto& c : h.capture.calls) {
            if (c.name == "edit_pattern" && c.value == 1.0f) { found = true; break; }
        }
        check(found, "`=` writes edit_pattern = 1");
    }

    // --- Octave shift with `]` / `[` ---
    {
        EditorHarness h;
        h.core.editor_octave_ = 4;
        h.events = {key_ev(ek::kRightBracket)};
        h.draw();
        check(h.core.editor_octave_ == 5, "] bumps octave to 5");
        h.clear();
        h.events = {key_ev(ek::kLeftBracket), key_ev(ek::kLeftBracket)};
        h.draw();
        check(h.core.editor_octave_ == 3, "[ [ drops octave to 3");
    }

    // --- Row clipboard: Cmd+C → Cmd+V round-trips a row ---
    {
        EditorHarness h;
        ::tracker::TrackerSong pre;
        pre.patterns[0].cells[0][0] = {60, 0x7F, 0x00, 0x00};
        pre.patterns[0].cells[1][0] = {64, 0x40, 0x00, 0x00};
        h.core.pattern_data.str_value = ::tracker::serialize_song(pre);

        // Cursor on row 0: Cmd+C captures it.
        h.events = {key_ev(ek::kC, ek::kModSuper)};
        h.draw();
        check(h.core.editor_row_clipboard_.has_content,
              "Cmd+C fills the row clipboard");
        check(h.core.editor_row_clipboard_.rows == 1,
              "single-row selection defaults to cursor row only");

        // Move cursor to row 5 and paste.
        h.clear();
        h.core.editor_cursor_row_ = 5;
        h.events = {key_ev(ek::kV, ek::kModSuper)};
        h.draw();
        auto song = h.last_song();
        check(song.patterns[0].cells[0][5].note == 60,
              "Cmd+V paste restores channel 0 note at row 5");
        check(song.patterns[0].cells[1][5].note == 64,
              "Cmd+V paste restores channel 1 note at row 5");
    }

    // --- Shift+Down extends the row selection ---
    {
        EditorHarness h;
        h.events = {
            key_ev(ek::kDown, ek::kModShift),
            key_ev(ek::kDown, ek::kModShift),
        };
        h.draw();
        check(h.core.editor_selection_row_lo_ == 0 &&
              h.core.editor_selection_row_hi_ == 2,
              "Shift+Down×2 selects rows 0..2");
    }

    // --- Toggle follow-playhead with F ---
    {
        EditorHarness h;
        const bool before = h.core.editor_follow_playhead_;
        h.events = {key_ev(ek::kF)};
        h.draw();
        check(h.core.editor_follow_playhead_ != before,
              "F toggles follow-playhead");
    }

    // --- Phase 4: Cmd+Shift+P/R/T toggle expression lane visibility ---
    {
        EditorHarness h;
        // Initial: mask = 0
        h.events = {key_ev(ek::kP, ek::kModSuper | ek::kModShift)};
        h.draw();
        auto song = h.last_song();
        check((song.patterns[0].expression_lane_mask & ::tracker::kLanePb) != 0,
              "Cmd+Shift+P sets pb visible");

        h.clear();
        h.events = {key_ev(ek::kR, ek::kModSuper | ek::kModShift)};
        h.draw();
        song = h.last_song();
        check((song.patterns[0].expression_lane_mask & ::tracker::kLanePr) != 0,
              "Cmd+Shift+R sets pr visible");
        check((song.patterns[0].expression_lane_mask & ::tracker::kLanePb) != 0,
              "previous pb bit preserved");

        h.clear();
        h.events = {key_ev(ek::kT, ek::kModSuper | ek::kModShift)};
        h.draw();
        song = h.last_song();
        check(song.patterns[0].expression_lane_mask ==
              (::tracker::kLanePb | ::tracker::kLanePr | ::tracker::kLaneTb),
              "Cmd+Shift+T flips remaining tb bit; mask = 7");

        // Toggle pb back off — data persists, only mask changes.
        h.clear();
        h.events = {key_ev(ek::kP, ek::kModSuper | ek::kModShift)};
        h.draw();
        song = h.last_song();
        check((song.patterns[0].expression_lane_mask & ::tracker::kLanePb) == 0,
              "Cmd+Shift+P toggled pb back off");
        check((song.patterns[0].expression_lane_mask & ::tracker::kLanePr) != 0,
              "pr stays on after pb toggle");
    }

    // --- Phase 4: hex/sign entry into the PitchBend cell ---
    {
        EditorHarness h;
        // Enable pb lane.
        h.events = {key_ev(ek::kP, ek::kModSuper | ek::kModShift)};
        h.draw();
        h.clear();
        // Move cursor onto pb (Note → Vel → Effect → PitchBend = 3 rights).
        h.events = {
            key_ev(ek::kRight), key_ev(ek::kRight), key_ev(ek::kRight),
        };
        h.draw();
        check(h.core.editor_cursor_field_ == te::Field::PitchBend,
              "right arrow lands on PitchBend when pb visible");

        // Type `+`, `4`, `0` → pitch_bend = +0x40 raw ≈ +24 semis.
        h.clear();
        h.events = {
            key_ev(ek::kEqual),  // '+' (sign)
            key_ev(ek::k4),
            key_ev(ek::k0),
        };
        h.draw();
        auto song = h.last_song();
        const auto& cell = song.patterns[0].cells[0][0];
        check(cell.pitch_bend != ::tracker::kExprEmpty,
              "pb cell got an anchor");
        check(cell.pitch_bend > 16000 && cell.pitch_bend < 17500,
              "+0x40 ≈ raw half-max (~16384)");
        // Cursor advanced to next row after completing 3-char entry.
        check(h.core.editor_cursor_row_ == 1, "advance_cursor_row after pb entry");
    }

    // --- Phase 4: hex entry into Pressure (2 chars, no sign) ---
    {
        EditorHarness h;
        h.events = {key_ev(ek::kR, ek::kModSuper | ek::kModShift)};  // pr on
        h.draw();
        h.clear();
        // Move cursor: Note → Vel → Effect → Pressure (3 rights when only pr is on).
        h.events = {
            key_ev(ek::kRight), key_ev(ek::kRight), key_ev(ek::kRight),
        };
        h.draw();
        check(h.core.editor_cursor_field_ == te::Field::Pressure,
              "right arrow lands on Pressure when only pr visible");

        h.clear();
        // Type "7" then "F" → 0x7F. For pressure (2 hex maps to 0..255 →
        // 0..32767 raw), 0x7F = 127 lands at ~16319 raw (half-max).
        h.events = { key_ev(ek::k7), key_ev(ek::kF) };
        h.draw();
        auto song = h.last_song();
        const auto& cell = song.patterns[0].cells[0][0];
        check(cell.pressure != ::tracker::kExprEmpty, "pr anchor set");
        check(cell.pressure > 16000 && cell.pressure < 17000,
              "0x7F ≈ raw half-max (~16319)");

        // Type "F", "F" → 0xFF → raw max ~32767.
        h.clear();
        h.events = { key_ev(ek::kF), key_ev(ek::kF) };
        h.draw();
        song = h.last_song();
        check(song.patterns[0].cells[0][1].pressure > 32500,
              "0xFF ≈ raw max (~32767) on the row that auto-advanced");
    }

    // --- Phase 4: Backspace clears the expression cell to kExprEmpty ---
    {
        EditorHarness h;
        // Pre-populate via serialized song (avoids accessing protected song_).
        ::tracker::TrackerSong seed;
        seed.patterns[0].expression_lane_mask = ::tracker::kLanePb;
        seed.patterns[0].cells[0][0].pitch_bend = 8192;
        h.core.pattern_data.str_value = ::tracker::serialize_song(seed);
        // Cursor onto pb.
        h.events = {
            key_ev(ek::kRight), key_ev(ek::kRight), key_ev(ek::kRight),
            key_ev(ek::kBackspace),
        };
        h.draw();
        auto song = h.last_song();
        check(song.patterns[0].cells[0][0].pitch_bend == ::tracker::kExprEmpty,
              "Backspace clears pb anchor to kExprEmpty");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
