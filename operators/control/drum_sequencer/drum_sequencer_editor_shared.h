#pragma once
// Pure-logic helpers shared by the DrumSequencer inspector and the dedicated
// editor window. Keeps param-name encoding + cell hit-test math in one place
// so both surfaces stay in sync and can be unit-tested without GLFW/WGPU.

#include "drum_sequencer_layout.h"
#include "operator_api/types.h"
#include "operator_api/editor_ui/selection.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace vivid_sequencers::drum_editor {

// Re-export the shared rectangular-selection vocabulary so existing
// call sites continue to use the `de::` namespace. DrumSequencer's
// drum/step coordinates map to (row = drum, col = step) inside the
// shared Selection struct — member access changes from drum_lo/hi and
// step_lo/hi to row_lo/hi and col_lo/hi.
using ::vivid::ui::Selection;
using ::vivid::ui::selection_from_point;
using ::vivid::ui::selection_from_anchor_tip;
using ::vivid::ui::selection_extend;
using ::vivid::ui::selection_contains;
using ::vivid::ui::selection_cell_count;
using ::vivid::ui::cursor_move;
using ::vivid::ui::clamp_editor_state;

enum class LaneKind : std::uint8_t {
    Pattern     = 0,   // pattern A triggers (kick_0 .. tom_15)
    ModA        = 1,   // velocity (kick_ma_0 ..)
    ModB        = 2,   // generic modulation (kick_mb_0 ..)
    PatternB    = 3,   // pattern B triggers (kick_b_0 ..)
    Probability = 4,   // per-step probability (kick_prob_0 ..)
    Roll        = 5,   // per-step ratchet count (kick_roll_0 ..)
    PatternC    = 6,   // pattern C triggers (kick_c_0 ..)
    PatternD    = 7,   // pattern D triggers (kick_d_0 ..)
};

// Translate a pattern index 0..3 to the matching trigger lane. Pattern
// LaneKind values aren't contiguous (PatternB=3, PatternC=6, PatternD=7),
// so callers go through this helper.
inline constexpr LaneKind lane_for_pattern(int pattern) {
    switch (pattern) {
        case 0:  return LaneKind::Pattern;
        case 1:  return LaneKind::PatternB;
        case 2:  return LaneKind::PatternC;
        default: return LaneKind::PatternD;
    }
}

// Build the canonical param name for a (lane, drum, step) cell.
//   Pattern     → "kick_0" ... "tom_15"
//   ModA        → "kick_ma_0" ...
//   ModB        → "kick_mb_0" ...
//   PatternB    → "kick_b_0" ...
//   PatternC    → "kick_c_0" ...
//   PatternD    → "kick_d_0" ...
//   Probability → "kick_prob_0" ...
//   Roll        → "kick_roll_0" ...
std::string param_name_for(LaneKind lane, std::size_t drum, int step);

// Descriptor-order index into VividInspectorContext::param_values /
// VividEditorContext::param_values for the same (lane, drum, step).
int param_index_for(LaneKind lane, std::size_t drum, int step);

// Layout math for a single lane block (6 rows × kStepCount cells).
// label_w is the drum-row label strip on the left.
struct GridMetrics {
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float block_w  = 0.0f;
    float block_h  = 0.0f;
    float label_w  = 0.0f;
    float cells_x  = 0.0f;
    float cells_y  = 0.0f;
    float grid_w   = 0.0f;
    float grid_h   = 0.0f;
    float cell_w   = 0.0f;
    float cell_h   = 0.0f;
};

GridMetrics compute_grid_metrics(float origin_x, float origin_y,
                                 float block_w, float block_h,
                                 float label_w);

struct CellHit {
    std::size_t drum = 0;
    int step = 0;
};

// Hit-test (mx, my) in editor-local pixel coords against the grid cells.
// Writes into *out and returns true only if the point is inside the cells
// rectangle AND step < num_steps (so cells beyond the configured step count
// don't accept input even if they render dimmed).
bool cell_from_mouse(const GridMetrics& m, float mx, float my, int num_steps,
                     CellHit* out);

// Issue the commands to clear a step: trigger → 0, ModA → 0.5, ModB → 0.5.
// Helper stays pure by taking the command API + opaque through the caller.
void clear_step(const VividInspectorCommandAPI& commands,
                std::size_t drum, int step);

// --- Step clipboard (copy/paste of a full step-column across all drums) ---

// A step-column snapshot: trigger + Mod A + Mod B per drum. Lives on the
// operator so each DrumSequencer node has its own clipboard that survives
// closing/reopening the editor.
struct StepClipboard {
    bool  has_content = false;
    float triggers[drum_layout::kDrumCount]{};
    float mod_a   [drum_layout::kDrumCount]{};
    float mod_b   [drum_layout::kDrumCount]{};
};

// Reads the 6×3 = 18 values at `step` from param_values into *out.
// has_content becomes true on success (param_values non-null and
// 0 <= step < kStepCount). Missing slots beyond param_count stay zero.
void copy_step(const float* param_values, std::uint32_t param_count,
               int step, StepClipboard* out);

// Writes the 18 clipboard values to `step` via commands.set_param.
// Emission order is per-drum (drum 0..5) with trigger → mod_a → mod_b so
// downstream coalescing stays predictable. Returns false (no writes) when
// clipboard is empty, commands.set_param is null, or step is out of range.
bool paste_step(const VividInspectorCommandAPI& commands,
                const StepClipboard& clip, int step);

// --- Multi-cell selection clipboard ---
//
// Snapshot of all six per-cell values across an arbitrary rectangle.
// Sized to the grid's maximum extent (kDrumCount × kStepCount) so the
// buffer is never heap-allocated. Paste targets can be any origin; cells
// that fall outside the grid on paste are clipped silently.
struct SelectionClipboard {
    bool has_content = false;
    std::size_t rows = 0;   // row_hi - row_lo + 1 at copy time (drum span)
    int         cols = 0;   // col_hi - col_lo + 1 at copy time (step span)

    struct Cell {
        // triggers[i] = pattern i (0=A, 1=B, 2=C, 3=D). Default 0 (off).
        float triggers[drum_layout::kPatternCount]{};
        float velocity    = 0.5f;   // ModA
        float mod_b       = 0.5f;   // ModB
        float probability = 1.0f;
        float roll        = 1.0f;
    };

    // Row-major: cells[row * cols + col].
    Cell cells[drum_layout::kDrumCount * drum_layout::kStepCount]{};
};

// Read every cell inside `sel` from param_values into `*out`. Leaves
// out->has_content = true on success. Cells that index past param_count
// keep their default values so callers don't need to check.
void copy_selection(const float* param_values, std::uint32_t param_count,
                    Selection sel, SelectionClipboard* out);

// Paste the clipboard at (origin_drum, origin_step). Cells that would
// land outside the grid are clipped. Returns false (no writes) when the
// clipboard is empty, commands.set_param is null, or the origin is out
// of range. Emits six set_param calls per in-bounds cell.
bool paste_selection(const VividInspectorCommandAPI& commands,
                     const SelectionClipboard& clip,
                     std::size_t origin_drum, int origin_step);

} // namespace vivid_sequencers::drum_editor
