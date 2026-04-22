#include "sequencer_editor_shared.h"

#include <algorithm>

namespace vivid::sequencer_editor {

std::string param_name_for(RowKind row, int step) {
    std::string name = (row == RowKind::Value) ? "step_value_" : "step_gate_";
    name += std::to_string(step);
    return name;
}

void copy_selection(const float* param_values, std::uint32_t param_count,
                    Selection sel, SelectionClipboard* out) {
    if (!out) return;
    *out = SelectionClipboard{};
    if (!param_values) return;

    const int rows = std::max(0, sel.row_hi - sel.row_lo + 1);
    const int cols = std::max(0, sel.col_hi - sel.col_lo + 1);
    if (rows <= 0 || cols <= 0) return;
    if (rows > kRowCount || cols > kMaxSteps) return;

    out->rows = rows;
    out->cols = cols;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int row  = sel.row_lo + r;
            const int step = sel.col_lo + c;
            const RowKind rk = (row == 0) ? RowKind::Value : RowKind::Gate;
            const int idx = param_index_for(rk, step);
            if (idx >= 0 && static_cast<std::uint32_t>(idx) < param_count)
                out->values[r * cols + c] = param_values[idx];
        }
    }
    out->has_content = true;
}

bool paste_selection(const VividInspectorCommandAPI& commands,
                     const SelectionClipboard& clip,
                     int origin_row, int origin_step) {
    if (!clip.has_content) return false;
    if (!commands.set_param) return false;
    if (clip.rows <= 0 || clip.cols <= 0) return false;
    if (origin_row  < 0 || origin_row  >= kRowCount) return false;
    if (origin_step < 0 || origin_step >= kMaxSteps) return false;

    for (int r = 0; r < clip.rows; ++r) {
        const int row = origin_row + r;
        if (row < 0 || row >= kRowCount) continue;
        const RowKind rk = (row == 0) ? RowKind::Value : RowKind::Gate;
        for (int c = 0; c < clip.cols; ++c) {
            const int step = origin_step + c;
            if (step < 0 || step >= kMaxSteps) continue;
            const std::string name = param_name_for(rk, step);
            commands.set_param(commands.opaque, name.c_str(),
                               clip.values[r * clip.cols + c]);
        }
    }
    return true;
}

} // namespace vivid::sequencer_editor
