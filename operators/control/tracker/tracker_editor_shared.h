#pragma once
// Pure-logic helpers for the Tracker editor. Piano-row → semitone map,
// hex-entry buffer accumulator, and the row-range clipboard. No
// VividEditorContext access — everything here is testable headless.

#include "tracker_data.h"

#include <cstdint>

namespace vivid::tracker_editor {

// --- Piano-row key → semitone mapping --------------------------------------

// Lower-row (Z/X/C/V/B/N/M) covers C..B within the current octave;
// upper-row (Q/W/E/R/T/Y/U) covers C..B one octave up. Black keys live
// on the row above (S/D/G/H/J for the lower octave; 2/3/5/6/7 for the
// upper). `A` is the note-off key.
//
// Returns -1 when the key isn't a note-entry key. When non-negative,
// the return is a semitone offset from the current octave's C; `C-4`
// keyboard entry with editor_octave_ == 4 yields 0 (semitone 0) which
// the caller combines with (editor_octave_ + 1) * 12 to form the MIDI
// note, matching tracker_data's `(oct + 1) * 12 + semi` convention.
//
// The special return value `kNoteOff` indicates `A` was pressed (note-off
// event) and `kNoNote` indicates no mapping.
inline constexpr int kNoNote = -1;
inline constexpr int kNoteOff = -2;

// Additional return: caller should bump the octave by this many before
// placing the note. Currently the "upper-octave row" keys (Q..U, 2..7)
// set the bump to +1; lower-octave keys set it to 0. Returned via the
// out-parameter for compactness.
int piano_key_to_semitone(int glfw_key, int* octave_bump_out);

// --- Hex-entry accumulator --------------------------------------------------
//
// Velocity (2 hex chars) and effect fields (1 type nibble + 2 hex param
// chars = 3 hex chars) share the same pattern: type one hex char at a
// time, each char shifts into the low nibble of the working value.
// The caller tracks how many chars have been entered and commits when
// the field is full.

// Parse a single hex char (0-9, A-F, a-f). Returns -1 on non-hex.
inline int hex_char_value(int codepoint) {
    if (codepoint >= '0' && codepoint <= '9') return codepoint - '0';
    if (codepoint >= 'A' && codepoint <= 'F') return codepoint - 'A' + 10;
    if (codepoint >= 'a' && codepoint <= 'f') return codepoint - 'a' + 10;
    return -1;
}

// Shift a 4-bit nibble into the low end of a running value, dropping
// the top nibble. Used to build a velocity or effect-param byte one
// hex char at a time.
inline uint8_t shift_in_nibble(uint8_t current, int nibble) {
    return static_cast<uint8_t>(((current << 4) | (nibble & 0xF)) & 0xFF);
}

// --- Row-range clipboard ----------------------------------------------------
//
// Captures a contiguous range of rows from one pattern (all 8 channels
// per row). Paste origin is "cursor row" in the target pattern; the
// clipboard tries to paste up to `rows` rows and silently clips when
// it runs past the pattern's end.

struct RowClipboard {
    bool  has_content = false;
    int   rows        = 0;   // number of captured rows
    // Row-major [row][channel]. Sized to the max extent so copy/paste
    // never heap-allocates.
    ::tracker::TrackerCell cells[::tracker::MAX_ROWS]
                                [::tracker::MAX_CHANNELS] = {};
};

// Snapshot pattern rows [row_lo, row_hi] into *out. Bounds are clamped
// to the pattern's actual row count; an inverted range yields an empty
// clipboard with has_content=false.
void copy_rows(const ::tracker::TrackerPattern& pat,
               int row_lo, int row_hi, RowClipboard* out);

// Write the clipboard into *pat starting at `origin_row`. Rows that
// would land past pat.num_rows are silently clipped. Returns the
// number of rows actually written; 0 indicates nothing was written
// (empty clipboard or origin past end).
int paste_rows(::tracker::TrackerPattern& pat, int origin_row,
               const RowClipboard& clip);

// --- Cursor bookkeeping ----------------------------------------------------

enum class Field : std::uint8_t {
    Note     = 0,
    Velocity = 1,
    Effect   = 2,
};

// Clamp cursor fields to the given pattern bounds. num_rows >= 1;
// channels is fixed at MAX_CHANNELS. Called at the top of every frame
// so a pattern shrinking between frames doesn't leave a stale cursor.
void clamp_cursor(int num_rows,
                  int* cursor_row, int* cursor_channel,
                  Field* cursor_field, int* effect_char);

// Advance the cursor down one row (or wrap to top if wrap=true). Used
// after a note/velocity/effect entry completes so the user can keep
// typing a descending pattern without arrow-keying.
void advance_cursor_row(int num_rows, bool wrap,
                        int* cursor_row, int* scroll_row,
                        int visible_rows);

} // namespace vivid::tracker_editor
