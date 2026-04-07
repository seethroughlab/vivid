#pragma once
#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include "midi_helpers.h"
#include "arpeggiator_patterns.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "operator_api/thumbnail.h"

/**
 * @brief Arpeggiation engine with 10 modes, swing, and per-step modulation.
 *
 * Collects notes from input spreads into a pool and plays them back in
 * configurable patterns (up, down, up-down, random, converge, diverge, etc.).
 * Supports per-step velocity and transpose modulation, swing timing, and
 * optional note latching.
 *
 * @tip Enable latch to keep playing after releasing keys.
 * @param mode Arpeggiation pattern: Up, Down, UpDown, Random, Converge, etc.
 * @param octaves Range of octave transposition applied to the pattern.
 * @see Sequencer, ChordProgression, MidiInput
 */
struct ArpeggiatorCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    // --- Core parameters ---
    vivid::Param<int>   mode        {"mode",        0, {"Up","Down","UpDown","DownUp","Random","Order","Converge","Diverge","RandomNoRepeat","OrderDown"}};
    vivid::Param<int>   octaves     {"octaves",     1, 1, 4};
    vivid::Param<int>   rate        {"rate",        3, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"}};
    vivid::Param<float> gate_length {"gate_length", 0.8f, 0.01f, 1.0f};
    vivid::Param<float> swing       {"swing",       0.0f, 0.0f, 1.0f};
    vivid::Param<bool>  latch       {"latch",       false};
    vivid::Param<int>   mod_steps   {"mod_steps",   8, 1, 8};
    vivid::Param<int>   clock_source{"clock_source", vivid::kClockSourceExternal, vivid::clock_source_labels()};

    // --- Per-step velocity modifiers ---
    vivid::Param<float> vel_0 {"vel_0", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_1 {"vel_1", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_2 {"vel_2", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_3 {"vel_3", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_4 {"vel_4", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_5 {"vel_5", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_6 {"vel_6", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_7 {"vel_7", 1.0f, 0.0f, 1.0f};

    // --- Per-step transpose modifiers ---
    vivid::Param<int> tr_0 {"tr_0", 0, -24, 24};
    vivid::Param<int> tr_1 {"tr_1", 0, -24, 24};
    vivid::Param<int> tr_2 {"tr_2", 0, -24, 24};
    vivid::Param<int> tr_3 {"tr_3", 0, -24, 24};
    vivid::Param<int> tr_4 {"tr_4", 0, -24, 24};
    vivid::Param<int> tr_5 {"tr_5", 0, -24, 24};
    vivid::Param<int> tr_6 {"tr_6", 0, -24, 24};
    vivid::Param<int> tr_7 {"tr_7", 0, -24, 24};
    vivid::Param<int> midi_channel {"midi_channel", 1, 1, 16};

    ArpeggiatorCore() {
        vivid::description(mode, "Arpeggiation pattern: Up, Down, UpDown, Random, Converge, etc");
        vivid::description(octaves, "Number of octaves to span above the input notes");
        vivid::description(rate, "Clock subdivision for arp step timing");
        vivid::description(gate_length, "Fraction of each step during which the note sounds");
        vivid::description(swing, "Timing offset between even and odd steps, 0 = straight");
        vivid::description(latch, "Keep playing the pattern after all input gates go low");
        vivid::description(mod_steps, "Number of active steps in the velocity/transpose modulation cycle");
        vivid::description(clock_source, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
        vivid::description(vel_0, "Velocity multiplier for modulation step 1");
        vivid::description(vel_1, "Velocity multiplier for modulation step 2");
        vivid::description(vel_2, "Velocity multiplier for modulation step 3");
        vivid::description(vel_3, "Velocity multiplier for modulation step 4");
        vivid::description(vel_4, "Velocity multiplier for modulation step 5");
        vivid::description(vel_5, "Velocity multiplier for modulation step 6");
        vivid::description(vel_6, "Velocity multiplier for modulation step 7");
        vivid::description(vel_7, "Velocity multiplier for modulation step 8");
        vivid::description(tr_0, "Semitone transpose for modulation step 1, -24 to +24");
        vivid::description(tr_1, "Semitone transpose for modulation step 2, -24 to +24");
        vivid::description(tr_2, "Semitone transpose for modulation step 3, -24 to +24");
        vivid::description(tr_3, "Semitone transpose for modulation step 4, -24 to +24");
        vivid::description(tr_4, "Semitone transpose for modulation step 5, -24 to +24");
        vivid::description(tr_5, "Semitone transpose for modulation step 6, -24 to +24");
        vivid::description(tr_6, "Semitone transpose for modulation step 7, -24 to +24");
        vivid::description(tr_7, "Semitone transpose for modulation step 8, -24 to +24");
        vivid::description(midi_channel, "MIDI channel for note output, 1 to 16");
    }

    // Param indices:
    //  0       = mode
    //  1       = octaves
    //  2       = rate
    //  3       = gate_length
    //  4       = swing
    //  5       = latch
    //  6       = mod_steps
    //  7       = clock_source
    //  8..15   = vel_0..vel_7
    //  16..23  = tr_0..tr_7
    //  24      = midi_channel

    vivid::Param<float>* vel_params_[8] = {&vel_0,&vel_1,&vel_2,&vel_3,&vel_4,&vel_5,&vel_6,&vel_7};
    vivid::Param<int>*   tr_params_[8]  = {&tr_0,&tr_1,&tr_2,&tr_3,&tr_4,&tr_5,&tr_6,&tr_7};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);         // 0
        out.push_back(&octaves);      // 1
        out.push_back(&rate);         // 2
        out.push_back(&gate_length);  // 3
        out.push_back(&swing);        // 4
        out.push_back(&latch);        // 5
        out.push_back(&mod_steps);    // 6
        out.push_back(&clock_source); // 7

        for (int i = 0; i < 8; ++i) {
            display_hint(*vel_params_[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(vel_params_[i]);   // 8..15
        }
        for (int i = 0; i < 8; ++i) {
            display_hint(*tr_params_[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(tr_params_[i]);    // 16..23
        }

        out.push_back(&midi_channel); // 24
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Inputs
        out.push_back({"beat_phase", VIVID_PORT_SCALAR,  VIVID_PORT_INPUT});   // [0]
        out.push_back({"notes",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // [1]
        out.push_back({"velocities", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // [2]
        out.push_back({"gates",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // [3]
        // Outputs
        out.push_back({"notes",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [0]
        out.push_back({"velocities", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [1]
        out.push_back({"gates",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [2]
        out.push_back({"note",       VIVID_PORT_SCALAR,  VIVID_PORT_OUTPUT});  // [0]
        out.push_back({"vel",        VIVID_PORT_SCALAR,  VIVID_PORT_OUTPUT});  // [1]
        out.push_back({"gate",       VIVID_PORT_SCALAR,  VIVID_PORT_OUTPUT});  // [2]
        out.push_back({"step",       VIVID_PORT_SCALAR,  VIVID_PORT_OUTPUT});  // [3]
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
    }

    void compute(float beat_phase, const float* params, const VividLaneView* in_spreads,
                 float* output_values, VividLaneOutput* out_spreads,
                 void** custom_outputs, uint32_t custom_output_count) {
        int m = mode.int_value();
        int oct = octaves.int_value();
        int r = rate.int_value();
        float gl = gate_length.value;
        float sw = swing.value;
        bool latch_on = latch.bool_value();
        int msteps = mod_steps.int_value();

        if (m < 0) m = 0; if (m > 9) m = 9;
        if (oct < 1) oct = 1; if (oct > 4) oct = 4;
        if (r < 0) r = 0; if (r > 8) r = 8;
        if (msteps < 1) msteps = 1; if (msteps > 8) msteps = 8;

        // Read input spread notes
        int input_count = 0;
        float input_notes[16];
        float input_vels[16];
        bool any_gate_high = false;

        if (in_spreads) {
            auto& notes_sp = in_spreads[1];  // input port 1
            auto& vel_sp   = in_spreads[2];  // input port 2
            auto& gates_sp = in_spreads[3];  // input port 3

            for (uint32_t i = 0; i < notes_sp.length && input_count < 16; ++i) {
                if (i < gates_sp.length && gates_sp.data[i] > 0.5f) {
                    any_gate_high = true;
                }
                input_notes[input_count] = notes_sp.data[i];
                input_vels[input_count] = (i < vel_sp.length) ? vel_sp.data[i] : 0.8f;
                input_count++;
            }
        }

        // Latch logic
        if (latch_on) {
            if (any_gate_high && !prev_any_gate_) {
                // New notes arrived after all gates were low — clear latch buffer
                latch_count_ = 0;
            }
            if (any_gate_high) {
                // Copy current input to latch buffer
                latch_count_ = input_count;
                for (int i = 0; i < input_count; ++i) {
                    latch_notes_[i] = input_notes[i];
                    latch_vels_[i] = input_vels[i];
                }
            }
            // Use latched notes
            if (latch_count_ > 0) {
                input_count = latch_count_;
                for (int i = 0; i < input_count; ++i) {
                    input_notes[i] = latch_notes_[i];
                    input_vels[i] = latch_vels_[i];
                }
                any_gate_high = true;  // latched notes are always "on"
            }
        }
        prev_any_gate_ = any_gate_high;

        // Build the arp note pool: input notes expanded across octaves
        int pool_count = 0;
        float pool_notes[64];  // 16 notes * 4 octaves max
        float pool_vels[64];

        // Sort input notes for ordered modes
        // For "Order" mode we preserve input order; for others we sort by pitch
        int sorted_indices[16];
        for (int i = 0; i < input_count; ++i) sorted_indices[i] = i;

        if (m != 5 && m != 9) {  // Order and OrderDown preserve input order
            // Simple insertion sort by note value
            for (int i = 1; i < input_count; ++i) {
                int key = sorted_indices[i];
                int j = i - 1;
                while (j >= 0 && input_notes[sorted_indices[j]] > input_notes[key]) {
                    sorted_indices[j + 1] = sorted_indices[j];
                    j--;
                }
                sorted_indices[j + 1] = key;
            }
        }

        // Expand across octaves
        for (int o = 0; o < oct; ++o) {
            for (int i = 0; i < input_count && pool_count < 64; ++i) {
                int idx = sorted_indices[i];
                pool_notes[pool_count] = input_notes[idx] + static_cast<float>(o * 12);
                pool_vels[pool_count] = input_vels[idx];
                pool_count++;
            }
        }

        // Detect beat_phase wraps -> count beats
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) {
            beat_count_++;
        }
        prev_phase_ = beat_phase;

        // Note presence: spread has notes regardless of input gate state
        bool has_notes = (pool_count > 0);

        // Reset step offset when notes transition from 0 to >0
        if (has_notes && !prev_had_notes_) {
            float total_beats = static_cast<float>(beat_count_) + beat_phase;
            step_offset_ = static_cast<int>(std::floor(total_beats * kMultipliers[r]));
            arp_direction_ = 1;
            last_selected_step_ = -1;
            last_selected_pool_ = -1;
            last_selected_idx_ = 0;
            last_random_idx_ = -1;
        }
        prev_had_notes_ = has_notes;

        // No notes — output silence
        if (!has_notes) {
            write_output(output_values, out_spreads, custom_outputs, custom_output_count,
                         0.0f, 0.0f, 0.0f, 0);
            return;
        }

        // Rate multiplier
        float multiplier = kMultipliers[r];

        // Calculate arp phase from cumulative beats
        float total_beats = static_cast<float>(beat_count_) + beat_phase;
        float arp_phase = total_beats * multiplier;

        int global_step = static_cast<int>(std::floor(arp_phase));
        int raw_step = global_step - step_offset_;
        if (raw_step < 0) raw_step = 0;

        // Offset-adjusted phase for gate/swing calculations
        float offset_phase = arp_phase - static_cast<float>(step_offset_);
        float step_phase = offset_phase - std::floor(offset_phase);

        // Apply swing
        int pair_step = raw_step / 2;
        bool is_odd = (raw_step % 2) != 0;
        float pair_phase = offset_phase - static_cast<float>(pair_step * 2);
        float swing_boundary = 1.0f + sw * 0.333f;

        if (is_odd) {
            float odd_duration = 2.0f - swing_boundary;
            step_phase = (pair_phase - swing_boundary) / odd_duration;
        } else {
            step_phase = pair_phase / swing_boundary;
        }
        step_phase = std::max(0.0f, std::min(1.0f, step_phase));

        // Determine which note in the pool to play based on mode
        int note_idx = 0;
        if (raw_step != last_selected_step_ || pool_count != last_selected_pool_) {
            note_idx = get_note_index(m, raw_step, pool_count);
            last_selected_step_ = raw_step;
            last_selected_pool_ = pool_count;
            last_selected_idx_ = note_idx;
        } else {
            note_idx = last_selected_idx_;
        }

        // Per-step modifier
        int mod_idx = raw_step % msteps;
        float vel_mod = params[8 + mod_idx];
        int tr_mod = static_cast<int>(params[16 + mod_idx]);

        float out_note = pool_notes[note_idx] + static_cast<float>(tr_mod);
        float out_vel = pool_vels[note_idx] * vel_mod;
        float out_gate = (step_phase < gl) ? 1.0f : 0.0f;

        write_output(output_values, out_spreads, custom_outputs, custom_output_count,
                     out_note, out_vel, out_gate, raw_step);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        const auto& d = ctx->draw;
        void* o = d.opaque;

        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        int msteps = (ctx->param_count > 6)
            ? std::max(1, std::min(8, static_cast<int>(ctx->param_values[6]))) : 8;
        int current_step = (ctx->output_count > 3)
            ? static_cast<int>(ctx->output_values[3]) : -1;
        int current_mod = (current_step >= 0) ? (current_step % msteps) : -1;

        // Mode label
        static const char* kModes[] = {"Up", "Down", "UpDn", "Rand", "Order"};
        int mode = (ctx->param_count > 0) ? std::clamp(static_cast<int>(ctx->param_values[0]), 0, 4) : 0;

        // Dark background
        d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

        // Mode text
        d.draw_text(o, 4, 2, kModes[mode], {0.45f, 0.55f, 0.65f, 0.8f}, 0.8f);

        // Layout: velocity bars top half, transpose bars bottom half
        float margin = 4.0f;
        float label_h = 14.0f;
        float mid_gap = 3.0f;
        float vel_y = label_h;
        float section_h = (h - label_h - mid_gap - margin) * 0.5f;
        float tr_y = vel_y + section_h + mid_gap;
        float bar_w = (w - 2 * margin) / static_cast<float>(msteps);
        float gap = 1.5f;

        VividColor accent = {0.45f, 0.55f, 0.65f, 1.0f};

        // Velocity bars (fill from bottom)
        for (int i = 0; i < msteps; ++i) {
            float vel = (ctx->param_count > static_cast<uint32_t>(7 + i))
                ? std::clamp(ctx->param_values[8 + i], 0.0f, 1.0f) : 1.0f;
            float bx = margin + i * bar_w + gap;
            float bw = bar_w - 2 * gap;
            float bh = vel * section_h;
            float by = vel_y + section_h - bh;
            float alpha = (i == current_mod) ? 0.9f : 0.45f;
            d.draw_rect(o, bx, by, bw, bh, {accent.r, accent.g, accent.b, alpha});
        }

        // Transpose bars (centered, extend up/down from middle)
        float tr_center = tr_y + section_h * 0.5f;
        d.draw_line(o, margin, tr_center, w - margin, tr_center, 0.5f,
                    {0.3f, 0.33f, 0.36f, 0.4f});

        for (int i = 0; i < msteps; ++i) {
            float tr = (ctx->param_count > static_cast<uint32_t>(15 + i))
                ? ctx->param_values[16 + i] : 0.0f;
            float norm = std::clamp(tr / 24.0f, -1.0f, 1.0f);
            float bx = margin + i * bar_w + gap;
            float bw = bar_w - 2 * gap;
            float bh = std::fabs(norm) * section_h * 0.5f;
            float by = (norm >= 0) ? (tr_center - bh) : tr_center;
            float alpha = (i == current_mod) ? 0.9f : 0.45f;
            // Warm for positive transpose, cool for negative
            VividColor col = (norm >= 0)
                ? VividColor{0.85f, 0.55f, 0.3f, alpha}
                : VividColor{0.3f, 0.55f, 0.85f, alpha};
            d.draw_rect(o, bx, by, bw, bh, col);
        }
    }

    void draw_inspector(VividInspectorContext* ctx) override {
        auto& d = ctx->draw;
        void* o = d.opaque;
        const auto& th = ctx->theme;

        float px = ctx->content_x;
        float py = ctx->content_y;
        float w = ctx->content_width;

        constexpr float vel_h = 70.0f;
        constexpr float tr_h = 70.0f;
        constexpr float label_h = 16.0f;
        constexpr float section_gap = 6.0f;
        constexpr float pad = 4.0f;
        constexpr float bar_gap = 1.0f;

        int msteps = std::clamp(
            (ctx->param_count > 6) ? static_cast<int>(ctx->param_values[6]) : 8, 1, 8);

        int current_mod = -1;
        if (ctx->output_count > 3) {
            int raw_step = static_cast<int>(ctx->output_values[3]);
            if (raw_step >= 0) current_mod = raw_step % msteps;
        }

        float bar_w = (w - 2.0f * pad) / static_cast<float>(msteps);

        // --- Velocity section ---
        float vy = py + section_gap;
        vivid::draw_ui::draw_section_header(d, o, px + pad, vy, w - 2.0f * pad,
                                            "Velocity",
                                            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f});
        vy += label_h;
        vivid::draw_ui::draw_panel(d, o, px, vy, w, vel_h, {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

        float vel_plot_x = px + pad;
        float vel_plot_y = vy + pad;
        float vel_plot_h = vel_h - 2.0f * pad;

        for (int i = 0; i < msteps; ++i) {
            float vel = (ctx->param_count > static_cast<uint32_t>(7 + i))
                ? std::clamp(ctx->param_values[8 + i], 0.0f, 1.0f) : 1.0f;

            float bx = vel_plot_x + static_cast<float>(i) * bar_w + bar_gap;
            float bw = bar_w - 2.0f * bar_gap;
            if (bw < 1.0f) bw = 1.0f;

            float bar_h = vel * vel_plot_h;
            float by = vel_plot_y + vel_plot_h - bar_h;

            float alpha = (i == current_mod) ? 0.8f : 0.45f;
            d.draw_rect(o, bx, by, bw, bar_h,
                        {th.accent.r, th.accent.g, th.accent.b, alpha});
        }

        // --- Transpose section ---
        float ty = vy + vel_h + section_gap;
        vivid::draw_ui::draw_section_header(d, o, px + pad, ty, w - 2.0f * pad,
                                            "Transpose",
                                            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f});
        ty += label_h;
        vivid::draw_ui::draw_panel(d, o, px, ty, w, tr_h, {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

        float tr_plot_x = px + pad;
        float tr_plot_y = ty + pad;
        float tr_plot_h = tr_h - 2.0f * pad;
        float tr_center_y = tr_plot_y + tr_plot_h * 0.5f;

        d.draw_line(o, tr_plot_x, tr_center_y, tr_plot_x + (w - 2.0f * pad), tr_center_y, 1.0f,
                    {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.15f});

        for (int i = 0; i < msteps; ++i) {
            float tr = (ctx->param_count > static_cast<uint32_t>(15 + i))
                ? ctx->param_values[16 + i] : 0.0f;
            float norm = std::clamp(tr / 24.0f, -1.0f, 1.0f);

            float bx = tr_plot_x + static_cast<float>(i) * bar_w + bar_gap;
            float bw = bar_w - 2.0f * bar_gap;
            if (bw < 1.0f) bw = 1.0f;

            float bar_h_val = std::abs(norm) * (tr_plot_h * 0.5f);
            float by = (norm >= 0.0f) ? (tr_center_y - bar_h_val) : tr_center_y;

            float r = th.accent.r, g = th.accent.g, b = th.accent.b;
            if (norm > 0.0f) {
                r = std::min(1.0f, r + 0.3f * norm);
                g = std::max(0.0f, g - 0.1f * norm);
                b = std::max(0.0f, b - 0.3f * norm);
            } else if (norm < 0.0f) {
                float an = -norm;
                r = std::max(0.0f, r - 0.2f * an);
                g = std::min(1.0f, g + 0.15f * an);
                b = std::min(1.0f, b + 0.2f * an);
            }

            float alpha = (i == current_mod) ? 0.8f : 0.45f;
            d.draw_rect(o, bx, by, bw, bar_h_val, {r, g, b, alpha});
        }

        // --- Drag interaction (velocity) ---
        if (ctx->mouse.left_clicked) {
            dragged_vel_ = -1;
            dragged_tr_ = -1;
            float mx = ctx->mouse.x;
            float my = ctx->mouse.y;
            if (mx >= vel_plot_x && mx <= vel_plot_x + (w - 2.0f * pad)) {
                int hit = static_cast<int>((mx - vel_plot_x) / bar_w);
                if (hit >= 0 && hit < msteps) {
                    if (my >= vel_plot_y && my <= vel_plot_y + vel_plot_h) {
                        dragged_vel_ = hit;
                    } else if (my >= tr_plot_y && my <= tr_plot_y + tr_plot_h) {
                        dragged_tr_ = hit;
                    }
                }
            }
        }

        if (ctx->mouse.left_down && dragged_vel_ >= 0 && dragged_vel_ < msteps) {
            float new_val = 1.0f - (ctx->mouse.y - vel_plot_y) / vel_plot_h;
            new_val = std::clamp(new_val, 0.0f, 1.0f);
            char name[16];
            std::snprintf(name, sizeof(name), "vel_%d", dragged_vel_);
            ctx->commands.set_param(ctx->commands.opaque, name, new_val);
        }

        if (ctx->mouse.left_down && dragged_tr_ >= 0 && dragged_tr_ < msteps) {
            float norm = 1.0f - (ctx->mouse.y - tr_plot_y) / tr_plot_h;
            float semitones = (norm - 0.5f) * 48.0f;
            semitones = std::clamp(semitones, -24.0f, 24.0f);
            semitones = std::round(semitones);
            char name[16];
            std::snprintf(name, sizeof(name), "tr_%d", dragged_tr_);
            ctx->commands.set_param(ctx->commands.opaque, name, semitones);
        }

        if (!ctx->mouse.left_down) {
            dragged_vel_ = -1;
            dragged_tr_ = -1;
        }

        ctx->consumed_height = section_gap + label_h + vel_h + section_gap + label_h + tr_h + section_gap;
    }


protected:
    static constexpr float kMultipliers[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 1.5f, 3.0f, 6.0f};

    // Beat tracking
    float prev_phase_ = 0.0f;
    int beat_count_ = 0;

    // Arp state
    int step_offset_ = 0;
    int arp_direction_ = 1;
    bool prev_had_notes_ = false;
    int last_selected_step_ = -1;
    int last_selected_pool_ = -1;
    int last_selected_idx_ = 0;
    int last_random_idx_ = -1;

    // Latch state
    bool prev_any_gate_ = false;
    float latch_notes_[16] = {};
    float latch_vels_[16] = {};
    int latch_count_ = 0;

    // Inspector drag state
    int dragged_vel_ = -1;
    int dragged_tr_ = -1;

    // MIDI state
    bool prev_midi_gate_ = false;
    int prev_midi_note_ = -1;
    VividMidiBuffer midi_buf_ = {};

    // RNG state for Random mode
    uint32_t rng_state_ = 12345;

    uint32_t rng_next() {
        rng_state_ ^= rng_state_ << 13;
        rng_state_ ^= rng_state_ >> 17;
        rng_state_ ^= rng_state_ << 5;
        return rng_state_;
    }

    int get_note_index(int mode_val, int raw_step, int pool_count) {
        if (pool_count <= 0) return 0;

        switch (mode_val) {
            case 4: // Random
                return static_cast<int>(rng_next() % static_cast<uint32_t>(pool_count));

            case 8: { // RandomNoRepeat
                int idx = static_cast<int>(rng_next() % static_cast<uint32_t>(pool_count));
                if (pool_count > 1 && idx == last_random_idx_) {
                    int off = 1 + static_cast<int>(rng_next() % static_cast<uint32_t>(pool_count - 1));
                    idx = (idx + off) % pool_count;
                }
                last_random_idx_ = idx;
                return idx;
            }

            default:
                return vivid_sequencers::arp_pattern_index(mode_val, raw_step, pool_count);
        }
    }

    void write_output(float* output_values, VividLaneOutput* out_spreads,
                      void** custom_outputs, uint32_t custom_output_count,
                      float note, float vel, float gate, int step) {
        if (out_spreads) {
            auto& notes_sp = out_spreads[0];
            auto& vel_sp   = out_spreads[1];
            auto& gates_sp = out_spreads[2];

            float* notes_buf = notes_sp.resize(notes_sp.handle, 1);
            float* vel_buf   = vel_sp.resize(vel_sp.handle, 1);
            float* gates_buf = gates_sp.resize(gates_sp.handle, 1);
            if (notes_buf && vel_buf && gates_buf) {
                notes_buf[0] = note; notes_sp.commit(notes_sp.handle, 1);
                vel_buf[0] = vel;    vel_sp.commit(vel_sp.handle, 1);
                gates_buf[0] = gate; gates_sp.commit(gates_sp.handle, 1);
            }
        }

        if (output_values) {
            output_values[0] = note;
            output_values[1] = vel;
            output_values[2] = gate;
            output_values[3] = static_cast<float>(step);
        }

        // MIDI output
        uint8_t ch = static_cast<uint8_t>(midi_channel.int_value() - 1);
        midi_buf_.count = 0;
        bool gate_high = (gate > 0.5f);
        if (gate_high && !prev_midi_gate_) {
            if (prev_midi_note_ >= 0) {
                vivid_sequencers::midi_note_off(midi_buf_,
                    static_cast<uint8_t>(prev_midi_note_), ch);
            }
            uint8_t midi_note = static_cast<uint8_t>(std::clamp(static_cast<int>(note), 0, 127));
            uint8_t midi_vel = static_cast<uint8_t>(std::clamp(static_cast<int>(vel * 127.0f), 0, 127));
            vivid_sequencers::midi_note_on(midi_buf_, midi_note, midi_vel, ch);
            prev_midi_note_ = midi_note;
        } else if (!gate_high && prev_midi_gate_) {
            if (prev_midi_note_ >= 0) {
                vivid_sequencers::midi_note_off(midi_buf_,
                    static_cast<uint8_t>(prev_midi_note_), ch);
                prev_midi_note_ = -1;
            }
        }
        prev_midi_gate_ = gate_high;
        if (custom_outputs && custom_output_count > 0) {
            custom_outputs[0] = &midi_buf_;
        }
    }
};
