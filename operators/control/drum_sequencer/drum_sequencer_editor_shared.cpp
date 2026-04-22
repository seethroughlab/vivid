#include "drum_sequencer_editor_shared.h"

#include <algorithm>

namespace vivid_sequencers::drum_editor {

namespace layout = ::vivid_sequencers::drum_layout;

std::string param_name_for(LaneKind lane, std::size_t drum, int step) {
    const char* prefix = nullptr;
    switch (lane) {
        case LaneKind::Pattern:     prefix = layout::kTriggerPrefixes[drum]; break;
        case LaneKind::ModA:        prefix = layout::kModAPrefixes[drum];    break;
        case LaneKind::ModB:        prefix = layout::kModBPrefixes[drum];    break;
        case LaneKind::PatternB:    prefix = layout::kTrigBPrefixes[drum];   break;
        case LaneKind::Probability: prefix = layout::kProbPrefixes[drum];    break;
        case LaneKind::Roll:        prefix = layout::kRollPrefixes[drum];    break;
    }
    std::string name(prefix);
    name += std::to_string(step);
    return name;
}

int param_index_for(LaneKind lane, std::size_t drum, int step) {
    switch (lane) {
        case LaneKind::Pattern:     return layout::trigger_param_index(drum, step);
        case LaneKind::ModA:        return layout::mod_a_param_index(drum, step);
        case LaneKind::ModB:        return layout::mod_b_param_index(drum, step);
        case LaneKind::PatternB:    return layout::trig_b_param_index(drum, step);
        case LaneKind::Probability: return layout::prob_param_index(drum, step);
        case LaneKind::Roll:        return layout::roll_param_index(drum, step);
    }
    return 0;
}

GridMetrics compute_grid_metrics(float origin_x, float origin_y,
                                 float block_w, float block_h,
                                 float label_w) {
    GridMetrics m{};
    m.origin_x = origin_x;
    m.origin_y = origin_y;
    m.block_w  = std::max(0.0f, block_w);
    m.block_h  = std::max(0.0f, block_h);
    m.label_w  = std::max(0.0f, std::min(label_w, m.block_w));
    m.cells_x  = origin_x + m.label_w;
    m.cells_y  = origin_y;
    m.grid_w   = std::max(0.0f, m.block_w - m.label_w);
    m.grid_h   = m.block_h;
    m.cell_w   = (layout::kStepCount > 0)
        ? m.grid_w / static_cast<float>(layout::kStepCount) : 0.0f;
    m.cell_h   = (layout::kDrumCount > 0)
        ? m.grid_h / static_cast<float>(layout::kDrumCount) : 0.0f;
    return m;
}

bool cell_from_mouse(const GridMetrics& m, float mx, float my, int num_steps,
                     CellHit* out) {
    if (!out) return false;
    if (m.cell_w <= 0.0f || m.cell_h <= 0.0f) return false;
    if (mx < m.cells_x || my < m.cells_y) return false;
    if (mx >= m.cells_x + m.grid_w) return false;
    if (my >= m.cells_y + m.grid_h) return false;

    int step = static_cast<int>((mx - m.cells_x) / m.cell_w);
    int drum = static_cast<int>((my - m.cells_y) / m.cell_h);
    if (step < 0 || drum < 0) return false;
    if (static_cast<std::size_t>(drum) >= layout::kDrumCount) return false;
    if (static_cast<std::size_t>(step) >= layout::kStepCount) return false;
    if (step >= num_steps) return false;  // cell beyond active range

    out->drum = static_cast<std::size_t>(drum);
    out->step = step;
    return true;
}

void clear_step(const VividInspectorCommandAPI& commands,
                std::size_t drum, int step) {
    if (!commands.set_param) return;
    const std::string trig = param_name_for(LaneKind::Pattern, drum, step);
    const std::string mod_a = param_name_for(LaneKind::ModA,   drum, step);
    const std::string mod_b = param_name_for(LaneKind::ModB,   drum, step);
    commands.set_param(commands.opaque, trig.c_str(),  0.0f);
    commands.set_param(commands.opaque, mod_a.c_str(), 0.5f);
    commands.set_param(commands.opaque, mod_b.c_str(), 0.5f);
}

void copy_step(const float* param_values, std::uint32_t param_count,
               int step, StepClipboard* out) {
    if (!out) return;
    *out = StepClipboard{};
    if (!param_values) return;
    if (step < 0 || step >= static_cast<int>(layout::kStepCount)) return;

    for (std::size_t drum = 0; drum < layout::kDrumCount; ++drum) {
        const int ti = param_index_for(LaneKind::Pattern, drum, step);
        const int ai = param_index_for(LaneKind::ModA,    drum, step);
        const int bi = param_index_for(LaneKind::ModB,    drum, step);
        if (ti >= 0 && static_cast<std::uint32_t>(ti) < param_count)
            out->triggers[drum] = param_values[ti];
        if (ai >= 0 && static_cast<std::uint32_t>(ai) < param_count)
            out->mod_a[drum] = param_values[ai];
        if (bi >= 0 && static_cast<std::uint32_t>(bi) < param_count)
            out->mod_b[drum] = param_values[bi];
    }
    out->has_content = true;
}

bool paste_step(const VividInspectorCommandAPI& commands,
                const StepClipboard& clip, int step) {
    if (!commands.set_param) return false;
    if (!clip.has_content) return false;
    if (step < 0 || step >= static_cast<int>(layout::kStepCount)) return false;

    for (std::size_t drum = 0; drum < layout::kDrumCount; ++drum) {
        const std::string trig = param_name_for(LaneKind::Pattern, drum, step);
        const std::string ma   = param_name_for(LaneKind::ModA,    drum, step);
        const std::string mb   = param_name_for(LaneKind::ModB,    drum, step);
        commands.set_param(commands.opaque, trig.c_str(), clip.triggers[drum]);
        commands.set_param(commands.opaque, ma.c_str(),   clip.mod_a[drum]);
        commands.set_param(commands.opaque, mb.c_str(),   clip.mod_b[drum]);
    }
    return true;
}

// --- Selection clipboard ---

namespace {

float read_param(const float* params, std::uint32_t count, int idx, float fallback) {
    if (idx < 0) return fallback;
    if (static_cast<std::uint32_t>(idx) >= count) return fallback;
    return params[idx];
}

} // namespace

void copy_selection(const float* param_values, std::uint32_t param_count,
                    Selection sel, SelectionClipboard* out) {
    if (!out) return;
    *out = SelectionClipboard{};
    if (!param_values) return;

    // Clamp the selection to the grid; callers may hand us an unsanitized
    // rectangle (e.g. stored in the core across reloads). Shared Selection
    // uses int axes; DrumSequencer's row axis indexes drums 0..5 and its
    // col axis indexes steps 0..15 — the cast to size_t happens on read.
    sel.row_hi = std::min<int>(sel.row_hi, static_cast<int>(layout::kDrumCount) - 1);
    sel.col_hi = std::min<int>(sel.col_hi, static_cast<int>(layout::kStepCount) - 1);
    if (sel.row_lo > sel.row_hi || sel.row_lo < 0) return;
    if (sel.col_lo > sel.col_hi || sel.col_lo < 0) return;

    const std::size_t rows = static_cast<std::size_t>(sel.row_hi - sel.row_lo + 1);
    const int         cols = sel.col_hi - sel.col_lo + 1;
    out->rows = rows;
    out->cols = cols;

    for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t drum = static_cast<std::size_t>(sel.row_lo) + row;
        for (int col = 0; col < cols; ++col) {
            const int step = sel.col_lo + col;
            auto& c = out->cells[row * static_cast<std::size_t>(cols) + col];
            c.trigger_a   = read_param(param_values, param_count,
                                       layout::trigger_param_index(drum, step), 0.0f);
            c.trigger_b   = read_param(param_values, param_count,
                                       layout::trig_b_param_index(drum, step), 0.0f);
            c.velocity    = read_param(param_values, param_count,
                                       layout::mod_a_param_index(drum, step), 0.5f);
            c.mod_b       = read_param(param_values, param_count,
                                       layout::mod_b_param_index(drum, step), 0.5f);
            c.probability = read_param(param_values, param_count,
                                       layout::prob_param_index(drum, step), 1.0f);
            c.roll        = read_param(param_values, param_count,
                                       layout::roll_param_index(drum, step), 1.0f);
        }
    }
    out->has_content = true;
}

bool paste_selection(const VividInspectorCommandAPI& commands,
                     const SelectionClipboard& clip,
                     std::size_t origin_drum, int origin_step) {
    if (!commands.set_param) return false;
    if (!clip.has_content) return false;
    if (origin_drum >= layout::kDrumCount) return false;
    if (origin_step < 0 || origin_step >= static_cast<int>(layout::kStepCount)) return false;
    if (clip.rows == 0 || clip.cols <= 0) return false;

    for (std::size_t row = 0; row < clip.rows; ++row) {
        const std::size_t drum = origin_drum + row;
        if (drum >= layout::kDrumCount) break;
        for (int col = 0; col < clip.cols; ++col) {
            const int step = origin_step + col;
            if (step >= static_cast<int>(layout::kStepCount)) break;
            const auto& c = clip.cells[row * static_cast<std::size_t>(clip.cols) + col];

            const std::string trig_a = param_name_for(LaneKind::Pattern,     drum, step);
            const std::string trig_b = param_name_for(LaneKind::PatternB,    drum, step);
            const std::string vel    = param_name_for(LaneKind::ModA,        drum, step);
            const std::string modb   = param_name_for(LaneKind::ModB,        drum, step);
            const std::string prob   = param_name_for(LaneKind::Probability, drum, step);
            const std::string roll   = param_name_for(LaneKind::Roll,        drum, step);

            commands.set_param(commands.opaque, trig_a.c_str(), c.trigger_a);
            commands.set_param(commands.opaque, trig_b.c_str(), c.trigger_b);
            commands.set_param(commands.opaque, vel.c_str(),    c.velocity);
            commands.set_param(commands.opaque, modb.c_str(),   c.mod_b);
            commands.set_param(commands.opaque, prob.c_str(),   c.probability);
            commands.set_param(commands.opaque, roll.c_str(),   c.roll);
        }
    }
    return true;
}

} // namespace vivid_sequencers::drum_editor
