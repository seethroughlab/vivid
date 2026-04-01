#include "operator_api/operator.h"
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
 * Collects notes from input lane arrays into a pool and plays them back in
 * configurable patterns (up, down, up-down, random, converge, diverge, etc.).
 * Supports per-step velocity and transpose modulation, swing timing, and
 * optional note latching.
 *
 * @tip Enable latch to keep playing after releasing keys.
 * @param mode Arpeggiation pattern: Up, Down, UpDown, Random, Converge, etc.
 * @param octaves Range of octave transposition applied to the pattern.
 * @see Sequencer, ChordProgression, MidiInput
 */
struct Arpeggiator : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "Arpeggiator";
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

    Arpeggiator() {
        vivid::description(mode, "Arpeggiation pattern: Up, Down, UpDown, Random, Converge, etc");
        vivid::description(octaves, "Number of octaves to span above the input notes");
        vivid::description(rate, "Clock subdivision for arp step timing");
        vivid::description(gate_length, "Fraction of each step during which the note sounds");
        vivid::description(swing, "Timing offset between even and odd steps, 0 = straight");
        vivid::description(latch, "Keep playing the pattern after all input gates go low");
        vivid::description(mod_steps, "Number of active steps in the velocity/transpose modulation cycle");
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

    WGPURenderPipeline thumb_pipeline_ = nullptr;
    WGPUBindGroup thumb_bind_group_ = nullptr;
    WGPUBindGroupLayout thumb_bind_layout_ = nullptr;
    WGPUBuffer thumb_uniform_buf_ = nullptr;
    WGPUShaderModule thumb_shader_ = nullptr;
    WGPUPipelineLayout thumb_pipe_layout_ = nullptr;
    WGPUTextureFormat thumb_pipeline_format_ = WGPUTextureFormat_Undefined;

    // Param indices:
    //  0       = mode
    //  1       = octaves
    //  2       = rate
    //  3       = gate_length
    //  4       = swing
    //  5       = latch
    //  6       = mod_steps
    //  7..14   = vel_0..vel_7
    //  15..22  = tr_0..tr_7
    //  23      = midi_channel

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

        for (int i = 0; i < 8; ++i) {
            display_hint(*vel_params_[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(vel_params_[i]);   // 7..14
        }
        for (int i = 0; i < 8; ++i) {
            display_hint(*tr_params_[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(tr_params_[i]);    // 15..22
        }

        out.push_back(&midi_channel); // 23
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

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values, ctx->input_lanes,
                ctx->output_values, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
    }

    void process_audio(const VividAudioContext* ctx) override {
        compute(0.0f, ctx->param_values, ctx->input_lanes,
                ctx->output_float_values, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
    }

    void compute(float beat_phase, const float* params, VividLanePort* input_lanes,
                 float* output_values, VividLanePort* output_lanes,
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

        // Read input lane-array notes
        int input_count = 0;
        float input_notes[16];
        float input_vels[16];
        bool any_gate_high = false;

        if (input_lanes) {
            auto& notes_lane = input_lanes[1];  // input port 1
            auto& velocity_lane   = input_lanes[2];  // input port 2
            auto& gates_lane = input_lanes[3];  // input port 3

            for (uint32_t i = 0; i < notes_lane.length && input_count < 16; ++i) {
                if (i < gates_lane.length && gates_lane.data[i] > 0.5f) {
                    any_gate_high = true;
                }
                input_notes[input_count] = notes_lane.data[i];
                input_vels[input_count] = (i < velocity_lane.length) ? velocity_lane.data[i] : 0.8f;
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

        // Note presence: lane array has notes regardless of input gate state
        // (ChordProgression outputs notes with gate=0 during gate-off phase)
        bool has_notes = (pool_count > 0);

        // Reset step offset when notes transition from 0 to >0
        if (has_notes && !prev_had_notes_) {
            // Compute current global step so we can offset from it
            float total_beats = static_cast<float>(beat_count_) + beat_phase;
            step_offset_ = static_cast<int>(std::floor(total_beats * kMultipliers[r]));
            arp_direction_ = 1;  // reset bounce direction
            last_selected_step_ = -1;
            last_selected_pool_ = -1;
            last_selected_idx_ = 0;
            last_random_idx_ = -1;
        }
        prev_had_notes_ = has_notes;

        // No notes — output silence
        if (!has_notes) {
            write_output(output_values, output_lanes, custom_outputs, custom_output_count,
                         0.0f, 0.0f, 0.0f, 0);
            return;
        }

        // Rate multiplier: 1/1=0.25, 1/2=0.5, 1/4=1, 1/8=2, 1/16=4, 1/32=8, 1/4T=1.5, 1/8T=3, 1/16T=6
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

        // Apply swing: even/odd step pairs
        // Even steps get (1.0 + swing * 0.333) of the pair time, odd get remainder
        int pair_step = raw_step / 2;
        bool is_odd = (raw_step % 2) != 0;
        float pair_phase = offset_phase - static_cast<float>(pair_step * 2);
        float swing_boundary = 1.0f + sw * 0.333f;

        if (is_odd) {
            // Odd step: phase within odd portion
            float odd_duration = 2.0f - swing_boundary;
            step_phase = (pair_phase - swing_boundary) / odd_duration;
        } else {
            // Even step: phase within even portion
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

        // Per-step modifier (polymetric: cycles independently through mod_steps)
        int mod_idx = raw_step % msteps;
        float vel_mod = params[7 + mod_idx];   // vel_0..vel_7
        int tr_mod = static_cast<int>(params[15 + mod_idx]);  // tr_0..tr_7

        float out_note = pool_notes[note_idx] + static_cast<float>(tr_mod);
        float out_vel = pool_vels[note_idx] * vel_mod;
        float out_gate = (step_phase < gl) ? 1.0f : 0.0f;

        write_output(output_values, output_lanes, custom_outputs, custom_output_count,
                     out_note, out_vel, out_gate, raw_step);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx) return;
        if (!thumb_pipeline_ || thumb_pipeline_format_ != ctx->thumbnail_format) {
            rebuild_thumb_pipeline(ctx);
        }
        if (!thumb_pipeline_ || !thumb_bind_group_ || !thumb_uniform_buf_) {
            vivid_report_thumbnail_error(ctx, "arpeggiator thumbnail pipeline init failed");
            return;
        }

        // Pack uniforms: meta + vel_0..7 + tr_0..7 (normalized)
        struct Uniforms {
            float meta[4];      // mod_steps, current_mod, pad, pad
            float vel_0123[4];
            float vel_4567[4];
            float tr_0123[4];   // normalized to [-1,1]
            float tr_4567[4];
        } u{};

        int msteps = (ctx->param_count > 6)
            ? std::max(1, std::min(8, static_cast<int>(ctx->param_values[6]))) : 8;
        int current_step = (ctx->output_count > 3)
            ? static_cast<int>(ctx->output_values[3]) : -1;
        int current_mod = (current_step >= 0) ? (current_step % msteps) : -1;

        u.meta[0] = static_cast<float>(msteps);
        u.meta[1] = static_cast<float>(current_mod);

        for (int i = 0; i < 8; ++i) {
            float vel = (ctx->param_count > static_cast<uint32_t>(7 + i)) ? ctx->param_values[7 + i] : 1.0f;
            float tr = (ctx->param_count > static_cast<uint32_t>(15 + i)) ? ctx->param_values[15 + i] : 0.0f;
            if (i < 4) {
                u.vel_0123[i] = vel;
                u.tr_0123[i] = tr / 24.0f;
            } else {
                u.vel_4567[i - 4] = vel;
                u.tr_4567[i - 4] = tr / 24.0f;
            }
        }

        wgpuQueueWriteBuffer(ctx->queue, thumb_uniform_buf_, 0, &u, sizeof(u));
        vivid::thumbnail::run_pass(ctx, thumb_pipeline_, thumb_bind_group_, "Arpeggiator Thumb Pass");
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

        // Current playing mod step from step output (output index 3)
        int current_mod = -1;
        if (ctx->output_count > 3) {
            int raw_step = static_cast<int>(ctx->output_values[3]);
            if (raw_step >= 0) current_mod = raw_step % msteps;
        }

        float bar_w = (w - 2.0f * pad) / static_cast<float>(msteps);

        // --- Velocity section ---
        float vy = py + section_gap;
        d.draw_text(o, px + pad, vy, "Velocity", {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 0.85f);
        vy += label_h;
        d.draw_rect(o, px, vy, w, vel_h, {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

        float vel_plot_x = px + pad;
        float vel_plot_y = vy + pad;
        float vel_plot_h = vel_h - 2.0f * pad;

        for (int i = 0; i < msteps; ++i) {
            float vel = (ctx->param_count > static_cast<uint32_t>(7 + i))
                ? std::clamp(ctx->param_values[7 + i], 0.0f, 1.0f) : 1.0f;

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
        d.draw_text(o, px + pad, ty, "Transpose", {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 0.85f);
        ty += label_h;
        d.draw_rect(o, px, ty, w, tr_h, {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

        float tr_plot_x = px + pad;
        float tr_plot_y = ty + pad;
        float tr_plot_h = tr_h - 2.0f * pad;
        float tr_center_y = tr_plot_y + tr_plot_h * 0.5f;

        // Center line
        d.draw_line(o, tr_plot_x, tr_center_y, tr_plot_x + (w - 2.0f * pad), tr_center_y, 1.0f,
                    {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.15f});

        for (int i = 0; i < msteps; ++i) {
            float tr = (ctx->param_count > static_cast<uint32_t>(15 + i))
                ? ctx->param_values[15 + i] : 0.0f;
            float norm = std::clamp(tr / 24.0f, -1.0f, 1.0f);

            float bx = tr_plot_x + static_cast<float>(i) * bar_w + bar_gap;
            float bw = bar_w - 2.0f * bar_gap;
            if (bw < 1.0f) bw = 1.0f;

            float bar_h = std::abs(norm) * (tr_plot_h * 0.5f);
            float by = (norm >= 0.0f) ? (tr_center_y - bar_h) : tr_center_y;

            // Color: warm for positive transpose, cool for negative
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
            d.draw_rect(o, bx, by, bw, bar_h, {r, g, b, alpha});
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
            // Map mouse Y to [-24, 24]: top = +24, bottom = -24
            float norm = 1.0f - (ctx->mouse.y - tr_plot_y) / tr_plot_h;  // 0..1 bottom..top
            float semitones = (norm - 0.5f) * 48.0f;  // -24..+24
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

    ~Arpeggiator() override {
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);
    }

private:
    static constexpr float kMultipliers[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 1.5f, 3.0f, 6.0f};

    // Beat tracking
    float prev_phase_ = 0.0f;
    int beat_count_ = 0;

    // Arp state
    int step_offset_ = 0;   // global step at last reset
    int arp_direction_ = 1;  // +1 or -1 for bounce modes
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
        // xorshift32
        rng_state_ ^= rng_state_ << 13;
        rng_state_ ^= rng_state_ >> 17;
        rng_state_ ^= rng_state_ << 5;
        return rng_state_;
    }

    int get_note_index(int mode, int raw_step, int pool_count) {
        if (pool_count <= 0) return 0;

        switch (mode) {
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
                return vivid_sequencers::arp_pattern_index(mode, raw_step, pool_count);
        }
    }

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx) {
        vivid::gpu::release(thumb_pipeline_);
        vivid::gpu::release(thumb_bind_group_);
        vivid::gpu::release(thumb_bind_layout_);
        vivid::gpu::release(thumb_uniform_buf_);
        vivid::gpu::release(thumb_shader_);
        vivid::gpu::release(thumb_pipe_layout_);

        static const char* kThumbFragment = R"(
struct Uniforms {
    info: vec4f,
    vel_0123: vec4f,
    vel_4567: vec4f,
    tr_0123: vec4f,
    tr_4567: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

fn get_vel(idx: i32) -> f32 {
    if (idx < 4) { return uniforms.vel_0123[idx]; }
    return uniforms.vel_4567[idx - 4];
}

fn get_tr(idx: i32) -> f32 {
    if (idx < 4) { return uniforms.tr_0123[idx]; }
    return uniforms.tr_4567[idx - 4];
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let bg = vec4f(18.0/255.0, 20.0/255.0, 23.0/255.0, 230.0/255.0);
    let highlight_bg = vec4f(35.0/255.0, 38.0/255.0, 45.0/255.0, 230.0/255.0);
    let baseline_col = vec4f(80.0/255.0, 85.0/255.0, 95.0/255.0, 200.0/255.0);

    let msteps = i32(uniforms.info.x);
    let current_mod = i32(uniforms.info.y);
    if (msteps < 1) { return bg; }

    let pad = 4.0 / 64.0;  // ~4px in normalized coords (assuming ~64px thumb)
    let plot_x = (uv.x - pad) / (1.0 - 2.0 * pad);
    let plot_y = (uv.y - pad) / (1.0 - 2.0 * pad);

    if (plot_x < 0.0 || plot_x > 1.0 || plot_y < 0.0 || plot_y > 1.0) {
        return bg;
    }

    let col_f = plot_x * f32(msteps);
    let col_idx = i32(floor(col_f));
    if (col_idx >= msteps) { return bg; }

    let is_current = (col_idx == current_mod);

    // Baseline
    if (abs(plot_y - 1.0) < 0.015) {
        return baseline_col;
    }

    // Current step background highlight
    if (is_current) {
        let vel = clamp(get_vel(col_idx), 0.0, 1.0);
        let bar_top = 1.0 - vel;
        let frac = fract(col_f);

        if (plot_y >= bar_top && frac > 0.02 && frac < 0.98) {
            // Bar with transpose-based color + brightness boost
            let tr = get_tr(col_idx);
            var cr = 100.0; var cg = 130.0; var cb = 170.0;
            if (tr > 0.0) {
                cr = cr + 120.0 * tr; cg = cg - 30.0 * tr; cb = cb - 100.0 * tr;
            } else if (tr < 0.0) {
                let at = -tr;
                cr = cr - 50.0 * at; cg = cg + 50.0 * at; cb = cb + 60.0 * at;
            }
            cr = min(255.0, cr + 60.0);
            cg = min(255.0, cg + 60.0);
            cb = min(255.0, cb + 60.0);
            return vec4f(cr/255.0, cg/255.0, cb/255.0, 220.0/255.0);
        }
        return highlight_bg;
    }

    // Non-current bars
    let vel = clamp(get_vel(col_idx), 0.0, 1.0);
    let bar_top = 1.0 - vel;
    let frac = fract(col_f);

    if (plot_y >= bar_top && frac > 0.02 && frac < 0.98) {
        let tr = get_tr(col_idx);
        var cr = 100.0; var cg = 130.0; var cb = 170.0;
        if (tr > 0.0) {
            cr = cr + 120.0 * tr; cg = cg - 30.0 * tr; cb = cb - 100.0 * tr;
        } else if (tr < 0.0) {
            let at = -tr;
            cr = cr - 50.0 * at; cg = cg + 50.0 * at; cb = cb + 60.0 * at;
        }
        return vec4f(cr/255.0, cg/255.0, cb/255.0, 220.0/255.0);
    }

    return bg;
}
)";

        static constexpr uint64_t kUniformSize = sizeof(float) * 20;
        thumb_shader_ = vivid::thumbnail::create_shader(ctx->device, kThumbFragment, "Arp Thumb Shader");
        thumb_uniform_buf_ =
            vivid::thumbnail::create_uniform_buffer(ctx->device, kUniformSize, "Arp Thumb Uniforms");
        thumb_bind_layout_ =
            vivid::thumbnail::create_uniform_bind_layout(ctx->device, kUniformSize, "Arp Thumb BGL");
        thumb_pipe_layout_ =
            vivid::thumbnail::create_pipeline_layout(ctx->device, thumb_bind_layout_, "Arp Thumb Layout");
        thumb_bind_group_ = vivid::thumbnail::create_uniform_bind_group(
            ctx->device, thumb_bind_layout_, thumb_uniform_buf_, kUniformSize, "Arp Thumb BG");
        thumb_pipeline_ = vivid::thumbnail::create_pipeline(
            ctx->device, thumb_shader_, thumb_pipe_layout_, ctx->thumbnail_format, "Arp Thumb Pipeline");
        if (!thumb_shader_ || !thumb_uniform_buf_ || !thumb_bind_layout_ || !thumb_pipe_layout_
            || !thumb_bind_group_ || !thumb_pipeline_) {
            vivid::gpu::release(thumb_pipeline_);
            vivid::gpu::release(thumb_bind_group_);
            vivid::gpu::release(thumb_bind_layout_);
            vivid::gpu::release(thumb_uniform_buf_);
            vivid::gpu::release(thumb_shader_);
            vivid::gpu::release(thumb_pipe_layout_);
            thumb_pipeline_format_ = WGPUTextureFormat_Undefined;
            return;
        }
        thumb_pipeline_format_ = ctx->thumbnail_format;
    }

    void write_output(float* output_values, VividLanePort* output_lanes,
                      void** custom_outputs, uint32_t custom_output_count,
                      float note, float vel, float gate, int step) {
        if (output_lanes) {
            auto& notes_lane = output_lanes[0];
            auto& velocity_lane   = output_lanes[1];
            auto& gates_lane = output_lanes[2];

            if (notes_lane.capacity >= 1) {
                notes_lane.length = 1;
                velocity_lane.length   = 1;
                gates_lane.length = 1;
                notes_lane.data[0] = note;
                velocity_lane.data[0]   = vel;
                gates_lane.data[0] = gate;
            }
        }

        if (output_values) {
            output_values[0] = note;
            output_values[1] = vel;
            output_values[2] = gate;
            output_values[3] = static_cast<float>(step);
        }

        // MIDI output: note-on on gate rising edge, note-off on falling edge
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

// Legacy registration removed — use _fr/_au variants instead.
VIVID_THUMBNAIL(Arpeggiator)
VIVID_INSPECTOR(Arpeggiator)
