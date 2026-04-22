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

void clamp_cursor(int num_rows,
                  int* cursor_row, int* cursor_channel,
                  Field* cursor_field, int* effect_char) {
    if (num_rows < 1) num_rows = 1;
    if (cursor_row)
        *cursor_row = std::clamp(*cursor_row, 0, num_rows - 1);
    if (cursor_channel)
        *cursor_channel = std::clamp(*cursor_channel, 0, ::tracker::MAX_CHANNELS - 1);
    if (cursor_field) {
        const int f = static_cast<int>(*cursor_field);
        *cursor_field = static_cast<Field>(std::clamp(f, 0, 2));
    }
    if (effect_char)
        *effect_char = std::clamp(*effect_char, 0, 2);
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
