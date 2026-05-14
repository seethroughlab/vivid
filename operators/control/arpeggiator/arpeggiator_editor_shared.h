#pragma once
// Pure-logic helpers for the Arpeggiator editor. Lane kind enum,
// param-name / param-index encoders (respecting the legacy index
// layout), and the Note Override label resolver. Test-friendly; no
// VividEditorContext access.

#include <cstdint>
#include <string>

namespace vivid::arpeggiator_editor {

// Grid lanes. Order matches the editor's row ordering top-to-bottom.
enum class Lane : std::uint8_t {
    NoteOverride = 0,
    Velocity     = 1,
    Transpose    = 2,
    Gate         = 3,
};

inline constexpr int kRowCount = 4;
inline constexpr int kMaxSteps = 16;

// --- Descriptor-order param indices ---
//
// Must stay in sync with ArpeggiatorCore::collect_params(). The first
// 25 indices (0..24) are pre-Cthulhu-expansion layout; new indices
// start at 25.
inline constexpr int kModeIndex         = 0;
inline constexpr int kOctavesIndex      = 1;
inline constexpr int kRateIndex         = 2;
inline constexpr int kGateLengthIndex   = 3;
inline constexpr int kSwingIndex        = 4;
inline constexpr int kLatchIndex        = 5;
inline constexpr int kModStepsIndex     = 6;
inline constexpr int kClockSourceIndex  = 7;

inline constexpr int kVelBase_0_7   = 8;   // vel_0..vel_7 at 8..15
inline constexpr int kTrBase_0_7    = 16;  // tr_0..tr_7  at 16..23
inline constexpr int kMidiChannelIndex = 24;
inline constexpr int kVelBase_8_15  = 25;  // vel_8..vel_15 at 25..32
inline constexpr int kTrBase_8_15   = 33;  // tr_8..tr_15  at 33..40
inline constexpr int kNoteOverrideBase = 41;  // note_override_0..15 at 41..56
inline constexpr int kGtBase            = 57;  // gt_0..15 at 57..72

// Step output ordinal — output_values[3] is `step` per collect_ports.
inline constexpr int kStepOutputIndex = 3;

// Descriptor-order index for a (lane, step) cell.
int  param_index_for(Lane lane, int step);

// Canonical param name, e.g. `"vel_7"`, `"note_override_12"`, `"gt_3"`.
std::string param_name_for(Lane lane, int step);

// Decode a Note Override int value into a short display label:
//   0      → "—"  (follow global mode; sentinel)
//   1..8   → "1".."8"
//   9      → "M"  (mute)
//   other  → "?"  (never expected)
const char* note_override_label(int value);

// Clamp the note-override value to its valid range (0..9).
inline int clamp_note_override(int v) {
    if (v < 0) return 0;
    if (v > 9) return 9;
    return v;
}

// --- Editor state clamp ---
//
// Given a current mod_steps value (potentially shrunk between frames),
// clamp a cursor + anchor + selection rect into the new bounds. Thin
// wrapper around `vivid::ui::clamp_editor_state` that also clamps the
// step-column (cols) to num_steps-1 and rows to kRowCount-1.
struct SelectionLike {
    int row_lo = 0, row_hi = 0, col_lo = 0, col_hi = 0;
};

void clamp_editor_state(int num_steps,
                        int* cur_row, int* cur_step,
                        int* anchor_row, int* anchor_col,
                        SelectionLike* sel);

} // namespace vivid::arpeggiator_editor
