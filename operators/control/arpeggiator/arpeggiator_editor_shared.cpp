#include "arpeggiator_editor_shared.h"

#include <algorithm>

namespace vivid::arpeggiator_editor {

int param_index_for(Lane lane, int step) {
    switch (lane) {
        case Lane::NoteOverride: return kNoteOverrideBase + step;
        case Lane::Velocity:
            return (step < 8) ? (kVelBase_0_7 + step) : (kVelBase_8_15 + (step - 8));
        case Lane::Transpose:
            return (step < 8) ? (kTrBase_0_7 + step) : (kTrBase_8_15 + (step - 8));
        case Lane::Gate:         return kGtBase + step;
    }
    return 0;
}

std::string param_name_for(Lane lane, int step) {
    const char* prefix = "?";
    switch (lane) {
        case Lane::NoteOverride: prefix = "note_override_"; break;
        case Lane::Velocity:     prefix = "vel_";           break;
        case Lane::Transpose:    prefix = "tr_";            break;
        case Lane::Gate:         prefix = "gt_";            break;
    }
    return std::string(prefix) + std::to_string(step);
}

const char* note_override_label(int value) {
    switch (value) {
        case 0:  return "—";
        case 1:  return "1";
        case 2:  return "2";
        case 3:  return "3";
        case 4:  return "4";
        case 5:  return "5";
        case 6:  return "6";
        case 7:  return "7";
        case 8:  return "8";
        case 9:  return "M";
        default: return "?";
    }
}

void clamp_editor_state(int num_steps,
                        int* cur_row, int* cur_step,
                        int* anchor_row, int* anchor_col,
                        SelectionLike* sel) {
    if (num_steps < 1) num_steps = 1;
    const int max_row = kRowCount - 1;
    const int max_col = std::min(num_steps, kMaxSteps) - 1;
    auto cr = [&](int v) { return std::clamp(v, 0, max_row); };
    auto cc = [&](int v) { return std::clamp(v, 0, max_col); };

    if (cur_row)    *cur_row    = cr(*cur_row);
    if (cur_step)   *cur_step   = cc(*cur_step);
    if (anchor_row) *anchor_row = cr(*anchor_row);
    if (anchor_col) *anchor_col = cc(*anchor_col);
    if (sel) {
        sel->row_lo = cr(sel->row_lo);
        sel->row_hi = cr(sel->row_hi);
        sel->col_lo = cc(sel->col_lo);
        sel->col_hi = cc(sel->col_hi);
        if (sel->row_hi < sel->row_lo) sel->row_hi = sel->row_lo;
        if (sel->col_hi < sel->col_lo) sel->col_hi = sel->col_lo;
    }
}

} // namespace vivid::arpeggiator_editor
