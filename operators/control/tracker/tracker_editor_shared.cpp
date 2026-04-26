#include "tracker_editor_shared.h"

#include "operator_api/editor_keys.h"

#include <algorithm>
#include <cstring>

namespace vivid::tracker_editor {

namespace ek = ::vivid::editor_keys;

int piano_key_to_semitone(int glfw_key, int* octave_bump_out) {
    if (octave_bump_out) *octave_bump_out = 0;

    // Lower row (current octave)
    switch (glfw_key) {
        case ek::kZ: return 0;   // C
        case ek::kS: return 1;   // C#
        case ek::kX: return 2;   // D
        case ek::kD: return 3;   // D#
        case ek::kC: return 4;   // E
        case ek::kV: return 5;   // F
        case ek::kG: return 6;   // F#
        case ek::kB: return 7;   // G
        case ek::kH: return 8;   // G#
        case ek::kN: return 9;   // A
        case ek::kJ: return 10;  // A#
        case ek::kM: return 11;  // B
        case ek::kComma:         // classic: `,` is C one octave up
            if (octave_bump_out) *octave_bump_out = 1;
            return 0;
        case ek::kA: return kNoteOff;  // note-off
        default: break;
    }

    // Upper row (current octave + 1)
    if (octave_bump_out) *octave_bump_out = 1;
    switch (glfw_key) {
        case ek::kQ: return 0;   // C
        case ek::k2: return 1;   // C#
        case ek::kW: return 2;   // D
        case ek::k3: return 3;   // D#
        case ek::kE: return 4;   // E
        case ek::kR: return 5;   // F
        case ek::k5: return 6;   // F#
        case ek::kT: return 7;   // G
        case ek::k6: return 8;   // G#
        case ek::kY: return 9;   // A
        case ek::k7: return 10;  // A#
        case ek::kU: return 11;  // B
        default: break;
    }

    if (octave_bump_out) *octave_bump_out = 0;
    return kNoNote;
}

void copy_rows(const ::tracker::TrackerPattern& pat,
               int row_lo, int row_hi, RowClipboard* out) {
    if (!out) return;
    *out = RowClipboard{};
    const int pn = pat.num_rows;
    if (pn <= 0) return;
    row_lo = std::max(0, std::min(row_lo, pn - 1));
    row_hi = std::max(0, std::min(row_hi, pn - 1));
    if (row_hi < row_lo) return;

    const int rows = row_hi - row_lo + 1;
    out->rows = rows;
    for (int r = 0; r < rows; ++r) {
        for (int ch = 0; ch < ::tracker::MAX_CHANNELS; ++ch) {
            out->cells[r][ch] = pat.cells[ch][row_lo + r];
        }
    }
    out->has_content = true;
}

int paste_rows(::tracker::TrackerPattern& pat, int origin_row,
               const RowClipboard& clip) {
    if (!clip.has_content) return 0;
    if (origin_row < 0 || origin_row >= pat.num_rows) return 0;
    const int writable = std::min(clip.rows, pat.num_rows - origin_row);
    if (writable <= 0) return 0;
    for (int r = 0; r < writable; ++r) {
        for (int ch = 0; ch < ::tracker::MAX_CHANNELS; ++ch) {
            pat.cells[ch][origin_row + r] = clip.cells[r][ch];
        }
    }
    return writable;
}

void clamp_cursor(int num_rows, std::uint8_t lane_mask,
                  int* cursor_row, int* cursor_channel,
                  Field* cursor_field, int* effect_char) {
    if (num_rows < 1) num_rows = 1;
    if (cursor_row)
        *cursor_row = std::clamp(*cursor_row, 0, num_rows - 1);
    if (cursor_channel)
        *cursor_channel = std::clamp(*cursor_channel, 0, ::tracker::MAX_CHANNELS - 1);
    if (cursor_field) {
        const int f = static_cast<int>(*cursor_field);
        Field clamped = static_cast<Field>(std::clamp(f, 0, 5));
        // If the cursor is parked on a hidden expression lane, snap back
        // to Note (the always-visible default). Avoids stranding the
        // cursor when a lane gets toggled off mid-frame.
        if (!is_field_visible(clamped, lane_mask)) clamped = Field::Note;
        *cursor_field = clamped;
    }
    if (effect_char)
        *effect_char = std::clamp(*effect_char, 0, 2);
}

namespace {
// Field iteration order: Note(0) → Velocity(1) → Effect(2) →
// PitchBend(3) → Pressure(4) → Timbre(5). next/prev visible-field
// helpers walk this order, skipping fields whose mask bit is off.
constexpr int kFieldCount = 6;
}

Field next_visible_field(Field current, std::uint8_t mask) {
    int i = static_cast<int>(current);
    for (int n = i + 1; n < kFieldCount; ++n) {
        if (is_field_visible(static_cast<Field>(n), mask))
            return static_cast<Field>(n);
    }
    return current;
}

Field prev_visible_field(Field current, std::uint8_t mask) {
    int i = static_cast<int>(current);
    for (int n = i - 1; n >= 0; --n) {
        if (is_field_visible(static_cast<Field>(n), mask))
            return static_cast<Field>(n);
    }
    return current;
}

bool has_visible_field_after(Field current, std::uint8_t mask) {
    return next_visible_field(current, mask) != current;
}

bool has_visible_field_before(Field current, std::uint8_t mask) {
    return prev_visible_field(current, mask) != current;
}

Field first_visible_field(std::uint8_t /*mask*/) {
    // Note is always visible.
    return Field::Note;
}

Field last_visible_field(std::uint8_t mask) {
    for (int n = kFieldCount - 1; n >= 0; --n) {
        if (is_field_visible(static_cast<Field>(n), mask))
            return static_cast<Field>(n);
    }
    return Field::Note;
}

// --- Phase 4 expression-value converters -----------------------------------

std::uint8_t pitch_bend_raw_to_hex(std::int16_t raw) {
    if (raw == ::tracker::kExprEmpty) return 0;
    int abs_raw = raw < 0 ? -raw : raw;
    // raw 32767 → hex 0x7F (127). Round to nearest.
    int hex = (abs_raw * 127 + 16383) / 32767;
    if (hex < 0)   hex = 0;
    if (hex > 127) hex = 127;
    return static_cast<std::uint8_t>(hex);
}

std::uint8_t unit_raw_to_hex(std::int16_t raw) {
    if (raw == ::tracker::kExprEmpty) return 0;
    if (raw < 0) raw = 0;
    int hex = (static_cast<int>(raw) * 255 + 16383) / 32767;
    if (hex < 0)   hex = 0;
    if (hex > 255) hex = 255;
    return static_cast<std::uint8_t>(hex);
}

std::int16_t pitch_bend_hex_to_raw(std::uint8_t hex_magnitude, int sign) {
    if (sign == 0) return 0;
    int mag = hex_magnitude > 127 ? 127 : hex_magnitude;
    int raw = (mag * 32767 + 63) / 127;
    if (sign < 0) raw = -raw;
    if (raw < -32767) raw = -32767;
    if (raw >  32767) raw =  32767;
    return static_cast<std::int16_t>(raw);
}

std::int16_t unit_hex_to_raw(std::uint8_t hex_value) {
    int raw = (static_cast<int>(hex_value) * 32767 + 127) / 255;
    if (raw <     0) raw =     0;
    if (raw > 32767) raw = 32767;
    return static_cast<std::int16_t>(raw);
}

void format_pitch_bend(std::int16_t raw, char out[4]) {
    if (raw == ::tracker::kExprEmpty) { out[0]='-'; out[1]='-'; out[2]='-'; out[3]=0; return; }
    char sign = (raw < 0) ? '-' : '+';
    std::uint8_t hex = pitch_bend_raw_to_hex(raw);
    static constexpr char kHex[] = "0123456789ABCDEF";
    out[0] = sign;
    out[1] = kHex[(hex >> 4) & 0xF];
    out[2] = kHex[hex & 0xF];
    out[3] = 0;
}

void format_unit(std::int16_t raw, char out[3]) {
    if (raw == ::tracker::kExprEmpty) { out[0]='-'; out[1]='-'; out[2]=0; return; }
    std::uint8_t hex = unit_raw_to_hex(raw);
    static constexpr char kHex[] = "0123456789ABCDEF";
    out[0] = kHex[(hex >> 4) & 0xF];
    out[1] = kHex[hex & 0xF];
    out[2] = 0;
}

void advance_cursor_row(int num_rows, bool wrap,
                        int* cursor_row, int* scroll_row,
                        int visible_rows) {
    if (!cursor_row) return;
    if (num_rows < 1) num_rows = 1;
    if (*cursor_row + 1 < num_rows) {
        ++(*cursor_row);
    } else if (wrap) {
        *cursor_row = 0;
    }
    // Keep cursor visible inside the scroll window (leave 2-row slack
    // at the bottom so the user can see what's coming).
    if (scroll_row && visible_rows > 0) {
        const int bottom_slack = 2;
        if (*cursor_row > *scroll_row + visible_rows - bottom_slack - 1) {
            *scroll_row = std::max(0,
                std::min(*cursor_row - (visible_rows - bottom_slack - 1),
                         num_rows - visible_rows));
        } else if (*cursor_row < *scroll_row) {
            *scroll_row = *cursor_row;
        }
    }
}

} // namespace vivid::tracker_editor
