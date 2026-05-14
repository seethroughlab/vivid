#pragma once
// Sequencer-specific editor helpers: param-name encoding, descriptor-
// order indices, and the clipboard's per-cell payload. The rectangular
// Selection struct and its geometric helpers live in the public editor UI API.
// This header just re-exports
// the Selection type so call sites can use one name.

#include "operator_api/types.h"
#include "operator_api/editor_ui/selection.h"

#include <cstdint>
#include <string>

namespace vivid::sequencer_editor {

enum class RowKind : std::uint8_t { Value = 0, Gate = 1 };
inline constexpr int kRowCount = 2;
inline constexpr int kMaxSteps = 32;  // matches SequencerCore::kMaxSteps.

// Descriptor-order param indices. Must stay in sync with
// SequencerCore::collect_params() in sequencer_core.h.
inline constexpr int kSourceParamIndex   = 0;
inline constexpr int kStepsParamIndex    = 1;
inline constexpr int kValueParamBase     = 2;     // step_value_0 .. step_value_31
inline constexpr int kGateParamBase      = 34;    // step_gate_0  .. step_gate_31
inline constexpr int kPolarityParamIndex = 72;    // 0 = bipolar, 1 = unipolar

// Output ordinals (per SequencerCore::collect_ports): 0 = value,
// 1 = step index, 2 = trigger. Editor reads output[1] for the playhead.
inline constexpr int kStepOutputIndex = 1;

// Descriptor-order index for a (row, step) cell.
inline int param_index_for(RowKind row, int step) {
    return (row == RowKind::Value ? kValueParamBase : kGateParamBase) + step;
}

// Canonical param name: "step_value_N" / "step_gate_N".
std::string param_name_for(RowKind row, int step);

// Re-export the shared geometry so Sequencer call sites can use a
// single `se::` namespace without caring where these live.
using ::vivid::ui::Selection;
using ::vivid::ui::selection_from_point;
using ::vivid::ui::selection_from_anchor_tip;
using ::vivid::ui::selection_contains;
using ::vivid::ui::selection_cell_count;
using ::vivid::ui::cursor_move;
using ::vivid::ui::clamp_editor_state;

// Row-major clipboard. `values[r * cols + c]` stores the param value at
// (row_lo + r, col_lo + c). Sized to the max grid so copy/paste never
// heap-allocates.
struct SelectionClipboard {
    bool  has_content = false;
    int   rows        = 0;
    int   cols        = 0;
    float values[kRowCount * kMaxSteps] = {};
};

// Snapshot the param values inside `sel` into *out. Cells whose
// descriptor index lies past param_count keep the default (0.0f).
void copy_selection(const float* param_values, std::uint32_t param_count,
                    Selection sel, SelectionClipboard* out);

// Emit commands.set_param for every cell in the clipboard starting at
// (origin_row, origin_step). Cells that would land outside the grid are
// clipped silently. Returns false when the clipboard is empty, the
// command API is absent, or the origin is invalid — no writes then.
bool paste_selection(const VividInspectorCommandAPI& commands,
                     const SelectionClipboard& clip,
                     int origin_row, int origin_step);

} // namespace vivid::sequencer_editor
