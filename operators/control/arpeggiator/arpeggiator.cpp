#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>
#include <cstring>

struct Arpeggiator : vivid::OperatorBase {
    static constexpr const char* kName   = "Arpeggiator";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    // --- Core parameters ---
    vivid::Param<int>   mode        {"mode",        0, {"Up","Down","UpDown","DownUp","Random","Order","Converge","Diverge"}};
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

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);         // 0
        out.push_back(&octaves);      // 1
        out.push_back(&rate);         // 2
        out.push_back(&gate_length);  // 3
        out.push_back(&swing);        // 4
        out.push_back(&latch);        // 5
        out.push_back(&mod_steps);    // 6
        out.push_back(&vel_0); out.push_back(&vel_1);  // 7..14
        out.push_back(&vel_2); out.push_back(&vel_3);
        out.push_back(&vel_4); out.push_back(&vel_5);
        out.push_back(&vel_6); out.push_back(&vel_7);
        out.push_back(&tr_0);  out.push_back(&tr_1);   // 15..22
        out.push_back(&tr_2);  out.push_back(&tr_3);
        out.push_back(&tr_4);  out.push_back(&tr_5);
        out.push_back(&tr_6);  out.push_back(&tr_7);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Inputs
        out.push_back({"beat_phase", VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_INPUT});   // [0]
        out.push_back({"notes",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // [1]
        out.push_back({"velocities", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // [2]
        out.push_back({"gates",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // [3]
        // Outputs
        out.push_back({"notes",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});  // [0]
        out.push_back({"velocities", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});  // [1]
        out.push_back({"gates",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});  // [2]
        out.push_back({"step",       VIVID_PORT_CONTROL_FLOAT,  VIVID_PORT_OUTPUT});  // [3]
    }

    void process(const VividProcessContext* ctx) override {
        float beat_phase = ctx->input_values[0];
        int m = mode.int_value();
        int oct = octaves.int_value();
        int r = rate.int_value();
        float gl = gate_length.value;
        float sw = swing.value;
        bool latch_on = latch.bool_value();
        int msteps = mod_steps.int_value();

        if (m < 0) m = 0; if (m > 7) m = 7;
        if (oct < 1) oct = 1; if (oct > 4) oct = 4;
        if (r < 0) r = 0; if (r > 8) r = 8;
        if (msteps < 1) msteps = 1; if (msteps > 8) msteps = 8;

        // Read input spread notes
        int input_count = 0;
        float input_notes[16];
        float input_vels[16];
        bool any_gate_high = false;

        if (ctx->input_spreads) {
            auto& notes_sp = ctx->input_spreads[1];  // input port 1
            auto& vel_sp   = ctx->input_spreads[2];  // input port 2
            auto& gates_sp = ctx->input_spreads[3];  // input port 3

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

        if (m != 5) {  // mode 5 = Order (preserve press order)
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
        // (ChordProgression outputs notes with gate=0 during gate-off phase)
        bool has_notes = (pool_count > 0);

        // Reset step offset when notes transition from 0 to >0
        if (has_notes && !prev_had_notes_) {
            // Compute current global step so we can offset from it
            float total_beats = static_cast<float>(beat_count_) + beat_phase;
            step_offset_ = static_cast<int>(std::floor(total_beats * kMultipliers[r]));
            arp_direction_ = 1;  // reset bounce direction
        }
        prev_had_notes_ = has_notes;

        // No notes — output silence
        if (!has_notes) {
            write_output(ctx, 0.0f, 0.0f, 0.0f, 0);
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
        int note_idx = get_note_index(m, raw_step, pool_count);

        // Per-step modifier (polymetric: cycles independently through mod_steps)
        int mod_idx = raw_step % msteps;
        float vel_mod = ctx->param_values[7 + mod_idx];   // vel_0..vel_7
        int tr_mod = static_cast<int>(ctx->param_values[15 + mod_idx]);  // tr_0..tr_7

        float out_note = pool_notes[note_idx] + static_cast<float>(tr_mod);
        float out_vel = pool_vels[note_idx] * vel_mod;
        float out_gate = (step_phase < gl) ? 1.0f : 0.0f;

        write_output(ctx, out_note, out_vel, out_gate, raw_step);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        // Param layout: mode=0, octaves=1, rate=2, gate_length=3, swing=4,
        //               latch=5, mod_steps=6, vel_0..vel_7=7..14, tr_0..tr_7=15..22
        int msteps = (ctx->param_count > 6)
            ? std::max(1, std::min(8, static_cast<int>(ctx->param_values[6]))) : 8;

        // Current step from output
        int current_step = -1;
        if (ctx->output_count > 3) {
            current_step = static_cast<int>(ctx->output_values[3]);
        }
        int current_mod = (current_step >= 0) ? (current_step % msteps) : -1;

        float w = static_cast<float>(ctx->width);
        float h = static_cast<float>(ctx->height);
        float pad = 4.0f;
        float plot_w = w - 2.0f * pad;
        float plot_h = h - 2.0f * pad;
        float col_w = plot_w / static_cast<float>(msteps);

        // Background
        for (uint32_t y = 0; y < ctx->height; ++y) {
            uint8_t* row = ctx->pixels + y * ctx->stride;
            for (uint32_t x = 0; x < ctx->width; ++x) {
                uint8_t* px = row + x * 4;
                px[0] = 18; px[1] = 20; px[2] = 23; px[3] = 230;
            }
        }

        // Draw each modifier step as a colored bar
        for (int s = 0; s < msteps; ++s) {
            float vel_val = (ctx->param_count > 7 + s) ? ctx->param_values[7 + s] : 1.0f;
            int tr_val = (ctx->param_count > 15 + s)
                ? static_cast<int>(ctx->param_values[15 + s]) : 0;

            vel_val = std::max(0.0f, std::min(1.0f, vel_val));
            tr_val = std::max(-24, std::min(24, tr_val));

            // Bar height from velocity
            float bar_frac = vel_val;
            float bar_h = bar_frac * (plot_h - 2.0f);
            if (bar_h < 1.0f) bar_h = 1.0f;
            float bar_x = pad + s * col_w + 1.0f;
            float bar_w_px = col_w - 2.0f;
            float bar_y = pad + plot_h - bar_h;

            // Color from transpose: neutral=blue-grey, positive=warm, negative=cool
            uint8_t cr, cg, cb;
            if (tr_val > 0) {
                float t = static_cast<float>(tr_val) / 24.0f;
                cr = static_cast<uint8_t>(100 + 120 * t);
                cg = static_cast<uint8_t>(130 - 30 * t);
                cb = static_cast<uint8_t>(170 - 100 * t);
            } else if (tr_val < 0) {
                float t = static_cast<float>(-tr_val) / 24.0f;
                cr = static_cast<uint8_t>(100 - 50 * t);
                cg = static_cast<uint8_t>(130 + 50 * t);
                cb = static_cast<uint8_t>(170 + 60 * t);
            } else {
                cr = 100; cg = 130; cb = 170;
            }

            bool is_current = (s == current_mod);
            if (is_current) {
                cr = static_cast<uint8_t>(std::min(255, cr + 60));
                cg = static_cast<uint8_t>(std::min(255, cg + 60));
                cb = static_cast<uint8_t>(std::min(255, cb + 60));
            }

            uint32_t ix0 = static_cast<uint32_t>(bar_x);
            uint32_t ix1 = static_cast<uint32_t>(bar_x + bar_w_px);
            uint32_t iy0 = static_cast<uint32_t>(bar_y);
            uint32_t iy1 = static_cast<uint32_t>(pad + plot_h);
            ix1 = std::min(ix1, ctx->width);
            iy1 = std::min(iy1, ctx->height);

            // Current step background highlight
            if (is_current) {
                uint32_t col_x0 = static_cast<uint32_t>(pad + s * col_w);
                uint32_t col_x1 = std::min(static_cast<uint32_t>(pad + (s + 1) * col_w), ctx->width);
                uint32_t col_y0 = static_cast<uint32_t>(pad);
                uint32_t col_y1 = std::min(static_cast<uint32_t>(pad + plot_h), ctx->height);
                for (uint32_t y = col_y0; y < col_y1; ++y) {
                    uint8_t* row = ctx->pixels + y * ctx->stride;
                    for (uint32_t x = col_x0; x < col_x1; ++x) {
                        uint8_t* px = row + x * 4;
                        px[0] = 35; px[1] = 38; px[2] = 45; px[3] = 230;
                    }
                }
            }

            // Draw bar
            for (uint32_t y = iy0; y < iy1; ++y) {
                uint8_t* row = ctx->pixels + y * ctx->stride;
                for (uint32_t x = ix0; x < ix1; ++x) {
                    uint8_t* px = row + x * 4;
                    px[0] = cr; px[1] = cg; px[2] = cb; px[3] = 220;
                }
            }
        }

        // Baseline
        uint32_t base_y = std::min(static_cast<uint32_t>(pad + plot_h), ctx->height - 1);
        uint8_t* base_row = ctx->pixels + base_y * ctx->stride;
        for (uint32_t x = static_cast<uint32_t>(pad);
             x < std::min(static_cast<uint32_t>(pad + plot_w), ctx->width); ++x) {
            uint8_t* px = base_row + x * 4;
            px[0] = 80; px[1] = 85; px[2] = 95; px[3] = 200;
        }
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

    // Latch state
    bool prev_any_gate_ = false;
    float latch_notes_[16] = {};
    float latch_vels_[16] = {};
    int latch_count_ = 0;

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
            case 0: // Up
                return raw_step % pool_count;

            case 1: // Down
                return (pool_count - 1) - (raw_step % pool_count);

            case 2: { // UpDown (exclusive endpoints)
                if (pool_count == 1) return 0;
                int cycle = (pool_count - 1) * 2;
                int pos = raw_step % cycle;
                return (pos < pool_count) ? pos : (cycle - pos);
            }

            case 3: { // DownUp (exclusive endpoints)
                if (pool_count == 1) return 0;
                int cycle = (pool_count - 1) * 2;
                int pos = raw_step % cycle;
                int down_idx = (pool_count - 1) - pos;
                int up_idx = pos - (pool_count - 1);
                return (pos < pool_count) ? down_idx : up_idx;
            }

            case 4: // Random
                return static_cast<int>(rng_next() % static_cast<uint32_t>(pool_count));

            case 5: // Order (press order preserved — pool already in input order)
                return raw_step % pool_count;

            case 6: { // Converge: alternate lowest/highest moving inward
                if (pool_count == 1) return 0;
                int cycle_pos = raw_step % pool_count;
                int pair = cycle_pos / 2;
                bool is_high = (cycle_pos % 2) != 0;
                if (is_high) {
                    return (pool_count - 1) - pair;
                } else {
                    return pair;
                }
            }

            case 7: { // Diverge: start from middle, alternate outward
                if (pool_count == 1) return 0;
                int mid = pool_count / 2;
                int cycle_pos = raw_step % pool_count;
                int offset = (cycle_pos + 1) / 2;
                bool go_down = (cycle_pos % 2) == 0;
                int idx;
                if (go_down) {
                    idx = mid - offset;
                } else {
                    idx = mid + offset;
                }
                // Clamp
                if (idx < 0) idx = 0;
                if (idx >= pool_count) idx = pool_count - 1;
                return idx;
            }

            default:
                return raw_step % pool_count;
        }
    }

    void write_output(const VividProcessContext* ctx, float note, float vel, float gate, int step) {
        if (ctx->output_spreads) {
            auto& notes_sp = ctx->output_spreads[0];
            auto& vel_sp   = ctx->output_spreads[1];
            auto& gates_sp = ctx->output_spreads[2];

            if (notes_sp.capacity >= 1) {
                notes_sp.length = 1;
                vel_sp.length   = 1;
                gates_sp.length = 1;
                notes_sp.data[0] = note;
                vel_sp.data[0]   = vel;
                gates_sp.data[0] = gate;
            }
        }

        ctx->output_values[0] = note;
        ctx->output_values[1] = vel;
        ctx->output_values[2] = gate;
        ctx->output_values[3] = static_cast<float>(step);
    }
};

VIVID_REGISTER(Arpeggiator)
VIVID_THUMBNAIL(Arpeggiator)
