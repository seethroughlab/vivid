// Pure-logic tests for Tracker editor helpers: piano-row → semitone
// mapping, hex-entry accumulator math, row-range clipboard, cursor
// clamping. No VividEditorContext plumbing.

#include "tracker_editor_shared.h"
#include "tracker_data.h"
#include "operator_api/editor_keys.h"

#include <cstdio>
#include <cstring>

#include "test_helpers.h"

namespace te = ::vivid::tracker_editor;
namespace ek = ::vivid::editor_keys;

namespace {

::tracker::TrackerPattern make_empty_pattern(int num_rows) {
    ::tracker::TrackerPattern p{};
    p.num_rows = static_cast<uint8_t>(num_rows);
    return p;
}

void set_cell(::tracker::TrackerPattern& p, int ch, int row,
              uint8_t note, uint8_t vel, uint8_t fx_t, uint8_t fx_p) {
    p.cells[ch][row] = {note, vel, fx_t, fx_p};
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: tracker editor helpers ===\n\n");

    // --- Piano-row → semitone mapping ---
    {
        int bump = 99;
        // Lower row — stays in the current octave (bump = 0).
        check(te::piano_key_to_semitone(ek::kZ, &bump) == 0  && bump == 0, "Z → C");
        check(te::piano_key_to_semitone(ek::kS, &bump) == 1  && bump == 0, "S → C#");
        check(te::piano_key_to_semitone(ek::kX, &bump) == 2  && bump == 0, "X → D");
        check(te::piano_key_to_semitone(ek::kM, &bump) == 11 && bump == 0, "M → B");
        // Comma is C one octave up (bump = 1).
        check(te::piano_key_to_semitone(ek::kComma, &bump) == 0 && bump == 1,
              ", → C (next octave)");
        // Upper row bumps octave.
        check(te::piano_key_to_semitone(ek::kQ, &bump) == 0 && bump == 1,
              "Q → C (upper row, +1 octave)");
        check(te::piano_key_to_semitone(ek::kU, &bump) == 11 && bump == 1,
              "U → B (upper row, +1 octave)");
        // A → note-off.
        check(te::piano_key_to_semitone(ek::kA, &bump) == te::kNoteOff,
              "A → note-off");
        // Arbitrary non-note key.
        check(te::piano_key_to_semitone(ek::kTab, &bump) == te::kNoNote,
              "Tab is not a note key");
    }

    // --- hex_char_value handles 0-9, A-F, a-f, garbage ---
    {
        check(te::hex_char_value('0') == 0,  "'0' → 0");
        check(te::hex_char_value('9') == 9,  "'9' → 9");
        check(te::hex_char_value('A') == 10, "'A' → 10");
        check(te::hex_char_value('F') == 15, "'F' → 15");
        check(te::hex_char_value('a') == 10, "'a' → 10");
        check(te::hex_char_value('f') == 15, "'f' → 15");
        check(te::hex_char_value('G') == -1, "'G' → -1");
        check(te::hex_char_value(' ') == -1, "space → -1");
    }

    // --- Row-range clipboard ---
    {
        // Build a 16-row pattern with distinct cells in rows 2..5.
        auto pat = make_empty_pattern(16);
        for (int r = 2; r <= 5; ++r)
            set_cell(pat, 0, r, static_cast<uint8_t>(60 + r),
                     static_cast<uint8_t>(0x40 + r), 0x0F, 0x10);

        te::RowClipboard clip;
        te::copy_rows(pat, 2, 5, &clip);
        check(clip.has_content,         "copy_rows populates clipboard");
        check(clip.rows == 4,           "4 rows captured");
        check(clip.cells[0][0].note == 62, "row 0, ch 0 captured note=62");
        check(clip.cells[3][0].velocity == 0x45,
              "row 3, ch 0 captured velocity=0x45");

        // Paste into a fresh pattern at row 10.
        auto dst = make_empty_pattern(16);
        const int written = te::paste_rows(dst, 10, clip);
        check(written == 4,            "4 rows written at origin 10");
        check(dst.cells[0][10].note == 62, "paste lands at origin row");
        check(dst.cells[0][13].velocity == 0x45,
              "paste preserves cell values across rows");
        check(dst.cells[0][9].note == 0,
              "row before origin untouched");

        // Paste near the end clips to available space.
        auto clip_dst = make_empty_pattern(12);
        const int writ2 = te::paste_rows(clip_dst, 10, clip);
        check(writ2 == 2,              "paste at row 10 in 12-row pat → writes 2 rows");
        check(clip_dst.cells[0][11].note == 63,
              "clipped paste still writes the rows that fit");

        // Empty clipboard → no writes.
        te::RowClipboard empty;
        auto empty_dst = make_empty_pattern(8);
        check(te::paste_rows(empty_dst, 0, empty) == 0,
              "paste of empty clipboard writes 0 rows");
    }

    // --- copy_rows bounds handling ---
    {
        auto pat = make_empty_pattern(8);
        te::RowClipboard clip;
        te::copy_rows(pat, 5, 3, &clip);
        check(!clip.has_content,
              "copy_rows with inverted bounds yields empty clipboard");
        te::copy_rows(pat, -5, 100, &clip);
        check(clip.has_content && clip.rows == 8,
              "copy_rows clamps to pattern bounds");
    }

    // --- clamp_cursor on bounds shrink ---
    {
        int row = 40, ch = 9, ec = 5;
        te::Field f = static_cast<te::Field>(3);
        te::clamp_cursor(16, &row, &ch, &f, &ec);
        check(row == 15,          "cursor row clamped to num_rows-1");
        check(ch == 7,            "cursor channel clamped to MAX_CHANNELS-1");
        check(static_cast<int>(f) == 2, "cursor field clamped to 2 (Effect)");
        check(ec == 2,            "effect char clamped to 2");
    }

    // --- advance_cursor_row auto-scrolls ---
    {
        int row = 4, scroll = 0;
        te::advance_cursor_row(16, /*wrap=*/false, &row, &scroll, /*visible=*/8);
        check(row == 5 && scroll == 0,
              "advance inside visible window leaves scroll alone");

        row = 7; scroll = 0;  // next advance pushes past bottom-slack
        te::advance_cursor_row(16, /*wrap=*/false, &row, &scroll, 8);
        check(row == 8,      "advance past visible area moves cursor");
        check(scroll > 0,    "scroll moves down to keep cursor visible");

        row = 15; scroll = 8;
        te::advance_cursor_row(16, /*wrap=*/false, &row, &scroll, 8);
        check(row == 15,     "advance at last row stays put (no wrap)");

        row = 15; scroll = 8;
        te::advance_cursor_row(16, /*wrap=*/true, &row, &scroll, 8);
        check(row == 0,      "advance at last row with wrap → row 0");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
