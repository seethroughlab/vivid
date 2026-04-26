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
    Note      = 0,
    Velocity  = 1,
    Effect    = 2,
    // Phase 4: per-cell expression lane fields. Only valid when the
    // matching bit is set in the pattern's expression_lane_mask; cursor
    // navigation skips hidden lanes.
    PitchBend = 3,
    Pressure  = 4,
    Timbre    = 5,
};

// True if `field` is visible in a pattern with the given lane mask.
// Note/Velocity/Effect are always visible. Expression lanes require
// their respective mask bit.
inline bool is_field_visible(Field field, std::uint8_t mask) {
    switch (field) {
        case Field::Note:
        case Field::Velocity:
        case Field::Effect:    return true;
        case Field::PitchBend: return (mask & ::tracker::kLanePb) != 0;
        case Field::Pressure:  return (mask & ::tracker::kLanePr) != 0;
        case Field::Timbre:    return (mask & ::tracker::kLaneTb) != 0;
    }
    return false;
}

// Step to the next/prev visible field within a single channel. Returns
// `current` unchanged when no visible field exists in the requested
// direction (caller then crosses the channel boundary). Field order
// (left → right): Note, Velocity, Effect, PitchBend, Pressure, Timbre.
Field next_visible_field(Field current, std::uint8_t mask);
Field prev_visible_field(Field current, std::uint8_t mask);
// True if there's any visible field strictly to the right/left of `current`
// in the same channel. Used by the editor to decide whether to advance
// across the channel boundary.
bool has_visible_field_after(Field current, std::uint8_t mask);
bool has_visible_field_before(Field current, std::uint8_t mask);
// First / last visible field in a channel (mask aware).
Field first_visible_field(std::uint8_t mask);
Field last_visible_field(std::uint8_t mask);

// Clamp cursor fields to the given pattern bounds. num_rows >= 1;
// channels is fixed at MAX_CHANNELS. If the cursor is parked on a hidden
// expression lane, snap it back to Note. Called at the top of every frame
// so a pattern shrinking between frames doesn't leave a stale cursor.
void clamp_cursor(int num_rows, std::uint8_t lane_mask,
                  int* cursor_row, int* cursor_channel,
                  Field* cursor_field, int* effect_char);

// --- Phase 4 expression-value entry ----------------------------------------
//
// Converters between the cell's raw int16 storage and the editor's hex/sign
// authoring surface.
//
//   pitch_bend  3-char authoring: `+`/`-` sign + 2 hex digits 00..7F.
//                Sign + 0x7F (127) maps to ±32767 raw (= ±48 semis, MPE).
//                Display formula: `raw * 127 / 32767`.
//   pressure    2-char authoring: 2 hex digits 00..FF mapped to 0..32767.
//                Display formula: `raw * 255 / 32767` (round to nearest).
//   timbre      same shape as pressure.
//
// kExprEmpty cells render as `---` (pb) or `--` (pr/tb) and are skipped
// from emission entirely.

// Convert a raw int16 to the unsigned hex magnitude (0..127 for pb,
// 0..255 for pr/tb). Caller handles the sign character separately.
std::uint8_t pitch_bend_raw_to_hex(std::int16_t raw);
std::uint8_t unit_raw_to_hex(std::int16_t raw);

// Convert hex magnitude (and sign for pb) back to raw int16.
std::int16_t pitch_bend_hex_to_raw(std::uint8_t hex_magnitude, int sign);
std::int16_t unit_hex_to_raw(std::uint8_t hex_value);

// Format helpers for the editor display. Buf must be ≥4 bytes for pb
// (`+04` / `---` / `\0`) and ≥3 bytes for pr/tb (`7F` / `--` / `\0`).
void format_pitch_bend(std::int16_t raw, char out[4]);
void format_unit(std::int16_t raw, char out[3]);

// Advance the cursor down one row (or wrap to top if wrap=true). Used
// after a note/velocity/effect entry completes so the user can keep
// typing a descending pattern without arrow-keying.
void advance_cursor_row(int num_rows, bool wrap,
                        int* cursor_row, int* scroll_row,
                        int visible_rows);

} // namespace vivid::tracker_editor
