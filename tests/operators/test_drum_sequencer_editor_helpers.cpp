// Pure-logic unit tests for the DrumSequencer editor helpers shared between
// the inspector and the dedicated editor window. No GLFW / no GPU.
#include "drum_sequencer_editor_shared.h"
#include "drum_sequencer_layout.h"
#include "operator_api/types.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "test_helpers.h"

namespace de = ::vivid_sequencers::drum_editor;
namespace layout = ::vivid_sequencers::drum_layout;

// Capture command API: records every set_param call for assertion.
struct CapturedSet {
    std::string name;
    float value;
};
struct CaptureCtx {
    std::vector<CapturedSet> calls;
};

static void capture_set_param(void* opaque, const char* name, float value) {
    auto* c = static_cast<CaptureCtx*>(opaque);
    c->calls.push_back({std::string(name ? name : ""), value});
}

static void capture_set_string_param(void*, const char*, const char*) {
    // unused in these tests
}

int main() {
    std::fprintf(stderr, "=== Test: DrumSequencer editor helpers ===\n\n");

    // --- param_name_for: known cells in every lane ---
    {
        check(de::param_name_for(de::LaneKind::Pattern, 0, 0) == "kick_0",
              "Pattern (0,0) → kick_0");
        check(de::param_name_for(de::LaneKind::Pattern, 5, 15) == "tom_15",
              "Pattern (5,15) → tom_15");
        check(de::param_name_for(de::LaneKind::ModA, 1, 7) == "snare_ma_7",
              "ModA (1,7) → snare_ma_7");
        check(de::param_name_for(de::LaneKind::ModB, 3, 15) == "oh_mb_15",
              "ModB (3,15) → oh_mb_15");
        check(de::param_name_for(de::LaneKind::Pattern, 2, 4) == "hat_4",
              "Pattern (2,4) → hat_4");
        check(de::param_name_for(de::LaneKind::ModA, 4, 0) == "clap_ma_0",
              "ModA (4,0) → clap_ma_0");
    }

    // --- param_index_for matches the layout-header helpers for every cell ---
    {
        bool all_match = true;
        for (std::size_t drum = 0; drum < layout::kDrumCount; ++drum) {
            for (int step = 0; step < static_cast<int>(layout::kStepCount); ++step) {
                if (de::param_index_for(de::LaneKind::Pattern, drum, step)
                    != layout::trigger_param_index(drum, step)) { all_match = false; }
                if (de::param_index_for(de::LaneKind::ModA, drum, step)
                    != layout::mod_a_param_index(drum, step))   { all_match = false; }
                if (de::param_index_for(de::LaneKind::ModB, drum, step)
                    != layout::mod_b_param_index(drum, step))   { all_match = false; }
            }
        }
        check(all_match, "param_index_for matches drum_layout for all (lane, drum, step)");
    }

    // --- compute_grid_metrics: typical block ---
    {
        auto m = de::compute_grid_metrics(/*x=*/0.0f, /*y=*/0.0f,
                                          /*w=*/1000.0f, /*h=*/300.0f,
                                          /*label_w=*/32.0f);
        check(m.cells_x == 32.0f, "cells_x = origin_x + label_w");
        check(m.cells_y == 0.0f,  "cells_y = origin_y");
        check(m.grid_w  == 968.0f, "grid_w = block_w - label_w");
        check(m.grid_h  == 300.0f, "grid_h = block_h");
        check(std::fabs(m.cell_w - 60.5f) < 1e-4f, "cell_w = grid_w / kStepCount");
        check(std::fabs(m.cell_h - 50.0f) < 1e-4f, "cell_h = grid_h / kDrumCount");
    }

    // --- compute_grid_metrics: zero/negative block dimensions stay safe ---
    {
        auto m = de::compute_grid_metrics(0.0f, 0.0f, 0.0f, 0.0f, 32.0f);
        check(m.cell_w >= 0.0f && m.cell_h >= 0.0f,
              "zero block yields non-negative cell dimensions");
        check(m.grid_w == 0.0f && m.grid_h == 0.0f,
              "zero block yields zero grid area");
    }

    // --- cell_from_mouse: positive and negative hits ---
    {
        auto m = de::compute_grid_metrics(0.0f, 0.0f, 1000.0f, 300.0f, 32.0f);
        de::CellHit hit{};

        // Cell (0, 0)
        check(de::cell_from_mouse(m, /*mx=*/33.0f, /*my=*/1.0f, 16, &hit),
              "cell at cells_x+1, cells_y+1 is hit");
        check(hit.drum == 0 && hit.step == 0,
              "→ (drum=0, step=0)");

        // Cell (5, 15) — last row, last step
        const float last_step_x = m.cells_x + 15 * m.cell_w + 1.0f;
        const float last_drum_y = m.cells_y + 5 * m.cell_h + 1.0f;
        check(de::cell_from_mouse(m, last_step_x, last_drum_y, 16, &hit),
              "last cell is hit");
        check(hit.drum == 5 && hit.step == 15, "→ (drum=5, step=15)");

        // Click inside label strip → no hit
        check(!de::cell_from_mouse(m, 10.0f, 50.0f, 16, &hit),
              "label strip click → no hit");

        // Click below the grid → no hit
        check(!de::cell_from_mouse(m, 50.0f, 400.0f, 16, &hit),
              "below grid → no hit");

        // Click beyond num_steps is rejected even if inside the grid box
        de::CellHit beyond{};
        check(!de::cell_from_mouse(m, m.cells_x + 10 * m.cell_w + 1.0f,
                                   m.cells_y + 1.0f, 8, &beyond),
              "step beyond num_steps → no hit");
    }

    // --- cursor_move: clamps at all edges, no wrap ---
    {
        // Shared cursor_move takes (max_row, max_col) inclusive bounds.
        // DrumSequencer uses row = drum (0..5), col = step (0..num_steps-1).
        constexpr int kMaxDrum = static_cast<int>(layout::kDrumCount) - 1;  // 5
        int drum = 0;
        int step = 0;
        de::cursor_move(-1, 0, kMaxDrum, 15, &drum, &step);
        check(drum == 0 && step == 0, "left at step=0 clamps");
        de::cursor_move(0, -1, kMaxDrum, 15, &drum, &step);
        check(drum == 0 && step == 0, "up at drum=0 clamps");

        drum = 5; step = 15;
        de::cursor_move(+1, 0, kMaxDrum, 15, &drum, &step);
        check(drum == 5 && step == 15, "right at step=last clamps");
        de::cursor_move(0, +1, kMaxDrum, 15, &drum, &step);
        check(drum == 5 && step == 15, "down at drum=last clamps");

        drum = 2; step = 4;
        de::cursor_move(+1, +1, kMaxDrum, 15, &drum, &step);
        check(drum == 3 && step == 5, "diagonal (+1,+1) moves both axes");

        // num_steps shrinks the upper bound (max_col = 7 ⇒ num_steps = 8).
        drum = 2; step = 10;
        de::cursor_move(+1, 0, kMaxDrum, 7, &drum, &step);
        check(step == 7, "num_steps=8 caps step at 7");

        // zero delta is a no-op
        drum = 1; step = 3;
        de::cursor_move(0, 0, kMaxDrum, 15, &drum, &step);
        check(drum == 1 && step == 3, "zero delta is a no-op");
    }

    // --- clear_step: issues trigger=0, mod_a=0.5, mod_b=0.5 in order ---
    {
        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;

        de::clear_step(api, /*drum=*/2, /*step=*/7);
        check(cap.calls.size() == 3, "clear_step issues 3 set_param calls");
        if (cap.calls.size() == 3) {
            check(cap.calls[0].name == "hat_7"    && cap.calls[0].value == 0.0f,
                  "call 0: hat_7 = 0.0");
            check(cap.calls[1].name == "hat_ma_7" && cap.calls[1].value == 0.5f,
                  "call 1: hat_ma_7 = 0.5");
            check(cap.calls[2].name == "hat_mb_7" && cap.calls[2].value == 0.5f,
                  "call 2: hat_mb_7 = 0.5");
        }
    }

    // --- clear_step with null set_param is a safe no-op ---
    {
        VividInspectorCommandAPI api{};
        api.opaque = nullptr;
        api.set_param = nullptr;
        api.set_string_param = nullptr;
        de::clear_step(api, 0, 0);
        check(true, "clear_step with null set_param does not crash");
    }

    // --- copy_step happy path: 18 values lifted from param_values ---
    {
        // Build a fully-sized param array. The DrumSequencer layout goes out
        // to kPtModBParamBases[5] + 15 = 282 + 15 = 297 for the last mod_b
        // slot; round up to a safe upper bound so param_count never short-circuits.
        constexpr std::size_t kSize = 320;
        std::vector<float> params(kSize, 0.0f);
        // Step 3: kick trigger = 1.0, snare mod_a = 0.7, tom mod_b = 0.2
        params[de::param_index_for(de::LaneKind::Pattern, 0, 3)] = 1.0f;
        params[de::param_index_for(de::LaneKind::ModA,    1, 3)] = 0.7f;
        params[de::param_index_for(de::LaneKind::ModB,    5, 3)] = 0.2f;
        // Also set a known mod_a on drum 4 so we verify per-drum reads.
        params[de::param_index_for(de::LaneKind::ModA, 4, 3)] = 0.33f;

        de::StepClipboard clip;
        de::copy_step(params.data(), static_cast<uint32_t>(params.size()),
                      3, &clip);
        check(clip.has_content, "copy_step: has_content true after read");
        check(clip.triggers[0] == 1.0f, "kick trigger at step 3 = 1.0");
        check(clip.triggers[1] == 0.0f, "snare trigger at step 3 = 0.0");
        check(clip.mod_a[1] == 0.7f,    "snare mod_a at step 3 = 0.7");
        check(clip.mod_a[4] == 0.33f,   "clap mod_a at step 3 = 0.33");
        check(clip.mod_b[5] == 0.2f,    "tom mod_b at step 3 = 0.2");
    }

    // --- copy_step defensive: short param array ---
    {
        // Only the first 16 entries present (no mod_a / mod_b slots).
        std::vector<float> short_params(16, 0.5f);
        de::StepClipboard clip;
        de::copy_step(short_params.data(),
                      static_cast<uint32_t>(short_params.size()),
                      0, &clip);
        // We don't crash; triggers for drums whose index is in range get
        // populated, mods remain zero (index beyond param_count).
        check(clip.has_content,
              "copy_step: has_content still true with short input");
        check(clip.mod_a[0] == 0.0f, "mod_a untouched when index out of range");
        check(clip.mod_b[0] == 0.0f, "mod_b untouched when index out of range");
    }

    // --- copy_step out-of-range step: clipboard stays empty ---
    {
        std::vector<float> params(320, 1.0f);
        de::StepClipboard clip;
        de::copy_step(params.data(),
                      static_cast<uint32_t>(params.size()),
                      /*step=*/-1, &clip);
        check(!clip.has_content, "copy_step(step=-1): has_content false");
        de::copy_step(params.data(),
                      static_cast<uint32_t>(params.size()),
                      /*step=*/16, &clip);
        check(!clip.has_content, "copy_step(step=16): has_content false");
    }

    // --- copy_step with nullptr param_values: safe, empty clipboard ---
    {
        de::StepClipboard clip;
        de::copy_step(nullptr, 0, 0, &clip);
        check(!clip.has_content, "copy_step(nullptr): has_content false");
    }

    // --- paste_step happy path: exactly 18 writes in drum order ---
    {
        de::StepClipboard clip;
        clip.has_content = true;
        clip.triggers[0] = 1.0f;
        clip.triggers[2] = 1.0f;
        clip.mod_a[1] = 0.6f;
        clip.mod_b[3] = 0.4f;

        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;

        check(de::paste_step(api, clip, 9),
              "paste_step: returns true on populated clipboard");
        check(cap.calls.size() == 18u,
              "paste_step: 18 set_param calls (6 drums x 3 lanes)");

        // First 3 calls target drum 0 (kick) at step 9.
        check(cap.calls[0].name == "kick_9"    && cap.calls[0].value == 1.0f,
              "paste call 0: kick_9 = 1.0");
        check(cap.calls[1].name == "kick_ma_9" && cap.calls[1].value == 0.0f,
              "paste call 1: kick_ma_9 = 0.0");
        check(cap.calls[2].name == "kick_mb_9" && cap.calls[2].value == 0.0f,
              "paste call 2: kick_mb_9 = 0.0");
        // Last 3 calls target drum 5 (tom).
        check(cap.calls[15].name == "tom_9"    && cap.calls[15].value == 0.0f,
              "paste call 15: tom_9 = 0.0");
        check(cap.calls[16].name == "tom_ma_9" && cap.calls[16].value == 0.0f,
              "paste call 16: tom_ma_9 = 0.0");
        check(cap.calls[17].name == "tom_mb_9" && cap.calls[17].value == 0.0f,
              "paste call 17: tom_mb_9 = 0.0");

        // Spot-check snare mod_a and oh mod_b in the middle of the sequence.
        bool snare_ma_found = false, oh_mb_found = false;
        for (const auto& c : cap.calls) {
            if (c.name == "snare_ma_9" && c.value == 0.6f) snare_ma_found = true;
            if (c.name == "oh_mb_9"    && c.value == 0.4f) oh_mb_found    = true;
        }
        check(snare_ma_found && oh_mb_found,
              "paste populates mid-drum values correctly");
    }

    // --- paste_step empty clipboard: no writes ---
    {
        de::StepClipboard clip;  // has_content stays false
        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;
        check(!de::paste_step(api, clip, 4),
              "paste_step: returns false on empty clipboard");
        check(cap.calls.empty(),
              "paste_step: zero set_param calls on empty clipboard");
    }

    // --- paste_step with null set_param is a safe no-op ---
    {
        de::StepClipboard clip;
        clip.has_content = true;
        VividInspectorCommandAPI api{};
        api.opaque = nullptr;
        api.set_param = nullptr;
        api.set_string_param = nullptr;
        check(!de::paste_step(api, clip, 0),
              "paste_step: returns false with null set_param");
    }

    // --- paste_step out-of-range step: no writes ---
    {
        de::StepClipboard clip;
        clip.has_content = true;
        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;
        check(!de::paste_step(api, clip, -1),
              "paste_step(step=-1): returns false");
        check(!de::paste_step(api, clip, 16),
              "paste_step(step=16): returns false");
        check(cap.calls.empty(),
              "out-of-range paste issues zero set_param calls");
    }

    // --- round-trip: copy a known pattern, paste via capture API, reproduce it ---
    {
        constexpr std::size_t kSize = 320;
        std::vector<float> params(kSize, 0.0f);
        // Step 7: a distinctive pattern across all drums.
        for (std::size_t drum = 0; drum < layout::kDrumCount; ++drum) {
            params[de::param_index_for(de::LaneKind::Pattern, drum, 7)]
                = (drum % 2 == 0) ? 1.0f : 0.0f;
            params[de::param_index_for(de::LaneKind::ModA, drum, 7)]
                = 0.1f * static_cast<float>(drum + 1);
            params[de::param_index_for(de::LaneKind::ModB, drum, 7)]
                = 1.0f - 0.1f * static_cast<float>(drum + 1);
        }

        de::StepClipboard clip;
        de::copy_step(params.data(),
                      static_cast<uint32_t>(params.size()),
                      7, &clip);
        check(clip.has_content, "round-trip: copy populated");

        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;
        check(de::paste_step(api, clip, 11),
              "round-trip: paste at step 11 succeeds");
        check(cap.calls.size() == 18u, "round-trip: 18 writes emitted");

        // Every paste value should match the source at step 7 for the same drum/lane.
        for (const auto& c : cap.calls) {
            // Parse "drum_[ma|mb]?_11" — by name lookup.
            float expected = 0.0f;
            bool matched = false;
            for (std::size_t drum = 0; drum < layout::kDrumCount && !matched; ++drum) {
                if (c.name == de::param_name_for(de::LaneKind::Pattern, drum, 11)) {
                    expected = params[de::param_index_for(de::LaneKind::Pattern, drum, 7)];
                    matched = true;
                } else if (c.name == de::param_name_for(de::LaneKind::ModA, drum, 11)) {
                    expected = params[de::param_index_for(de::LaneKind::ModA, drum, 7)];
                    matched = true;
                } else if (c.name == de::param_name_for(de::LaneKind::ModB, drum, 11)) {
                    expected = params[de::param_index_for(de::LaneKind::ModB, drum, 7)];
                    matched = true;
                }
            }
            check(matched, (std::string("round-trip: unrecognized name ") + c.name).c_str());
            check(std::fabs(c.value - expected) < 1e-6f,
                  (std::string("round-trip: value matches for ") + c.name).c_str());
        }
    }

    // --- Extended LaneKind: names + indices for prob / roll / pattern-B ---
    {
        check(de::param_name_for(de::LaneKind::PatternB, 0, 0) == "kick_b_0",
              "PatternB (0,0) → kick_b_0");
        check(de::param_name_for(de::LaneKind::PatternB, 5, 15) == "tom_b_15",
              "PatternB (5,15) → tom_b_15");
        check(de::param_name_for(de::LaneKind::Probability, 2, 8) == "hat_prob_8",
              "Probability (2,8) → hat_prob_8");
        check(de::param_name_for(de::LaneKind::Roll, 4, 3) == "clap_roll_3",
              "Roll (4,3) → clap_roll_3");

        bool indices_match = true;
        for (std::size_t d = 0; d < layout::kDrumCount; ++d) {
            for (int s = 0; s < static_cast<int>(layout::kStepCount); ++s) {
                if (de::param_index_for(de::LaneKind::PatternB, d, s)
                    != layout::trig_b_param_index(d, s)) indices_match = false;
                if (de::param_index_for(de::LaneKind::Probability, d, s)
                    != layout::prob_param_index(d, s))  indices_match = false;
                if (de::param_index_for(de::LaneKind::Roll, d, s)
                    != layout::roll_param_index(d, s))  indices_match = false;
            }
        }
        check(indices_match,
              "param_index_for matches layout helpers for new lane kinds");
    }

    // --- Selection: point + extend + contains + cell_count ---
    {
        auto sel = de::selection_from_point(3, 7);
        check(sel.row_lo == 3 && sel.row_hi == 3 &&
              sel.col_lo == 7 && sel.col_hi == 7,
              "selection_from_point(3, 7) → {3,3,7,7}");
        check(de::selection_cell_count(sel) == 1,
              "point selection has 1 cell");
        check(de::selection_contains(sel, 3, 7),
              "point contains its own cell");
        check(!de::selection_contains(sel, 4, 7),
              "point does not contain neighbour drum");

        // selection_from_anchor_tip is order-independent
        auto rect = de::selection_from_anchor_tip(/*anchor*/ 4, 9,
                                                  /*tip*/    2, 5);
        check(rect.row_lo == 2 && rect.row_hi == 4 &&
              rect.col_lo == 5 && rect.col_hi == 9,
              "anchor_tip swaps ends so lo<=hi");
        check(de::selection_cell_count(rect) == 3 * 5,
              "3x5 rect → 15 cells");

        // selection_extend grows the rect to include new cells
        auto extended = de::selection_extend(sel, /*drum*/ 1, /*step*/ 4);
        check(extended.row_lo == 1 && extended.row_hi == 3 &&
              extended.col_lo == 4 && extended.col_hi == 7,
              "extend grows toward included point");
        auto unchanged = de::selection_extend(extended, 2, 6);
        check(unchanged.row_lo == 1 && unchanged.row_hi == 3 &&
              unchanged.col_lo == 4 && unchanged.col_hi == 7,
              "extend with interior point is idempotent");

        // contains over a rect
        check(de::selection_contains(extended, 2, 5),
              "extended rect contains interior");
        check(!de::selection_contains(extended, 0, 5),
              "extended rect rejects drum outside");
        check(!de::selection_contains(extended, 2, 8),
              "extended rect rejects step outside");
    }

    // --- copy_selection happy path: rectangular read ---
    {
        // 588 = full param_count. Use a smaller buffer that still covers the
        // new blocks (roll max = layout::kRollParamBases[5] + 15 = 587).
        constexpr std::size_t kSize = 600;
        std::vector<float> params(kSize, 0.0f);
        // Seed a 2x2 patch at (drum=1..2, step=4..5) with distinctive values.
        params[layout::trigger_param_index(1, 4)] = 1.0f;
        params[layout::trigger_param_index(2, 5)] = 1.0f;
        params[layout::trig_b_param_index(2, 4)]  = 1.0f;
        params[layout::mod_a_param_index(1, 5)]   = 0.8f;
        params[layout::mod_b_param_index(2, 5)]   = 0.2f;
        params[layout::prob_param_index(1, 4)]    = 0.25f;
        params[layout::roll_param_index(2, 4)]    = 4.0f;

        de::SelectionClipboard clip;
        de::Selection sel{1, 2, 4, 5};
        de::copy_selection(params.data(),
                           static_cast<uint32_t>(params.size()), sel, &clip);
        check(clip.has_content, "copy_selection: has_content true");
        check(clip.rows == 2 && clip.cols == 2, "copy_selection: 2x2 geometry");

        // Row 0 (drum=1) ⋅ col 0 (step=4) — originates from (1,4).
        const auto& c00 = clip.cells[0 * 2 + 0];
        check(c00.trigger_a == 1.0f, "cell(0,0) trigger_a copied");
        check(c00.probability == 0.25f, "cell(0,0) probability copied");
        // Row 0 ⋅ col 1 (step=5) — originates from (1,5).
        const auto& c01 = clip.cells[0 * 2 + 1];
        check(c01.velocity == 0.8f, "cell(0,1) velocity copied");
        // Row 1 (drum=2) ⋅ col 0 (step=4).
        const auto& c10 = clip.cells[1 * 2 + 0];
        check(c10.trigger_b == 1.0f, "cell(1,0) trigger_b copied");
        check(c10.roll == 4.0f, "cell(1,0) roll copied");
        // Row 1 ⋅ col 1 (step=5).
        const auto& c11 = clip.cells[1 * 2 + 1];
        check(c11.trigger_a == 1.0f, "cell(1,1) trigger_a copied");
        check(c11.mod_b == 0.2f, "cell(1,1) mod_b copied");
    }

    // --- copy_selection clamps out-of-range rectangles ---
    {
        std::vector<float> params(600, 0.5f);
        de::SelectionClipboard clip;
        // Rectangle that spills past the last drum + last step.
        de::Selection overshoot{4, 99, 14, 99};
        de::copy_selection(params.data(),
                           static_cast<uint32_t>(params.size()),
                           overshoot, &clip);
        check(clip.has_content, "clamped copy still populates clipboard");
        check(clip.rows == (layout::kDrumCount - 4),
              "rows clamped to drum count - 4");
        check(clip.cols == (static_cast<int>(layout::kStepCount) - 14),
              "cols clamped to step count - 14");
    }

    // --- paste_selection emits 6 writes per in-bounds cell, clipped at edges ---
    {
        de::SelectionClipboard clip;
        clip.has_content = true;
        clip.rows = 2;
        clip.cols = 3;
        // Cell (0,0): trigger_a=1, (0,1): trigger_b=1, (1,2): roll=2
        clip.cells[0].trigger_a = 1.0f;
        clip.cells[1].trigger_b = 1.0f;
        clip.cells[2].velocity  = 0.9f;
        clip.cells[3].mod_b     = 0.1f;
        clip.cells[4].probability = 0.5f;
        clip.cells[5].roll      = 2.0f;

        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;

        check(de::paste_selection(api, clip, /*origin_drum=*/2, /*origin_step=*/10),
              "paste_selection: returns true on valid origin");
        check(cap.calls.size() == 6u * 6u,
              "paste_selection: 6 params × 6 cells = 36 writes");

        // Drum mapping: 0=kick 1=snare 2=hat 3=oh 4=clap 5=tom.
        // Origin = (drum=2, step=10). Rows grow along drums, cols along steps:
        //   (0,0) → hat_10    (0,1) → hat_11    (0,2) → hat_12
        //   (1,0) → oh_10     (1,1) → oh_11     (1,2) → oh_12
        bool found_hat_10    = false;
        bool found_hat_b_11  = false;
        bool found_hat_ma_12 = false;
        bool found_oh_roll_12 = false;
        for (const auto& c : cap.calls) {
            if (c.name == "hat_10"      && c.value == 1.0f) found_hat_10 = true;
            if (c.name == "hat_b_11"    && c.value == 1.0f) found_hat_b_11 = true;
            if (c.name == "hat_ma_12"   && c.value == 0.9f) found_hat_ma_12 = true;
            if (c.name == "oh_roll_12"  && c.value == 2.0f) found_oh_roll_12 = true;
        }
        check(found_hat_10,      "paste origin writes hat_10 = 1.0");
        check(found_hat_b_11,    "paste (0,1) writes hat_b_11 = 1.0");
        check(found_hat_ma_12,   "paste (0,2) writes hat_ma_12 = 0.9");
        check(found_oh_roll_12,  "paste (1,2) writes oh_roll_12 = 2.0");
    }

    // --- paste_selection clips to grid edges ---
    {
        de::SelectionClipboard clip;
        clip.has_content = true;
        clip.rows = 3;
        clip.cols = 4;
        // Mark every cell so we can count writes per in-bounds cell.
        for (std::size_t i = 0; i < 12; ++i)
            clip.cells[i].trigger_a = 1.0f;

        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;

        // Origin at (drum=4, step=14) → only 2×2 in-bounds cells fit:
        //   drums 4..5 (2 rows), steps 14..15 (2 cols). Remaining 8 cells clip.
        check(de::paste_selection(api, clip, 4, 14),
              "paste_selection: returns true with partial overlap");
        check(cap.calls.size() == 2u * 2u * 6u,
              "edge clip: only in-bounds cells emit 6 writes each");
    }

    // --- paste_selection: empty or invalid origin is a safe no-op ---
    {
        de::SelectionClipboard clip;  // has_content = false
        CaptureCtx cap;
        VividInspectorCommandAPI api{};
        api.opaque = &cap;
        api.set_param = capture_set_param;
        api.set_string_param = capture_set_string_param;
        check(!de::paste_selection(api, clip, 0, 0),
              "paste_selection: returns false on empty clipboard");
        check(cap.calls.empty(),
              "paste_selection: no writes on empty clipboard");

        clip.has_content = true;
        clip.rows = 1; clip.cols = 1;
        check(!de::paste_selection(api, clip, /*drum=*/99, /*step=*/0),
              "paste_selection: returns false on drum origin out of range");
        check(!de::paste_selection(api, clip, /*drum=*/0, /*step=*/-1),
              "paste_selection: returns false on step origin out of range");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
