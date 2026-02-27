#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

struct ChordProgression : vivid::OperatorBase {
    static constexpr const char* kName   = "ChordProgression";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    // --- Global parameters ---
    vivid::Param<int>   steps          {"steps",          4, 1, 8};
    vivid::Param<int>   key_root       {"key_root",       0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   mode           {"mode",           0, {"Major","Minor","Dorian","Mixolydian","Harm Min","Mel Min"}};
    vivid::Param<int>   octave         {"octave",         4, 2, 7};
    vivid::Param<int>   beats_per_step {"beats_per_step", 4, 1, 16};
    vivid::Param<float> gate_length    {"gate_length",    0.8f, 0.01f, 1.0f};
    vivid::Param<float> velocity       {"velocity",       0.8f, 0.0f, 1.0f};

    // --- Per-step degree (0=I .. 6=VII) ---
    vivid::Param<int>   degree_0 {"degree_0", 0, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_1 {"degree_1", 3, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_2 {"degree_2", 4, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_3 {"degree_3", 0, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_4 {"degree_4", 0, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_5 {"degree_5", 0, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_6 {"degree_6", 0, {"I","II","III","IV","V","VI","VII"}};
    vivid::Param<int>   degree_7 {"degree_7", 0, {"I","II","III","IV","V","VI","VII"}};

    // --- Per-step voicing ---
    vivid::Param<int>   voicing_0 {"voicing_0", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_1 {"voicing_1", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_2 {"voicing_2", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_3 {"voicing_3", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_4 {"voicing_4", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_5 {"voicing_5", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_6 {"voicing_6", 0, {"Root","Inv1","Inv2","Drop2"}};
    vivid::Param<int>   voicing_7 {"voicing_7", 0, {"Root","Inv1","Inv2","Drop2"}};

    // --- Per-step extension ---
    vivid::Param<int>   ext_0 {"ext_0", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_1 {"ext_1", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_2 {"ext_2", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_3 {"ext_3", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_4 {"ext_4", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_5 {"ext_5", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_6 {"ext_6", 0, {"Triad","7th","Add9"}};
    vivid::Param<int>   ext_7 {"ext_7", 0, {"Triad","7th","Add9"}};

    // Internal state
    int beat_count_ = 0;
    float prev_phase_ = 0.0f;

    // Semitone intervals from key root for each scale degree
    static constexpr int kScaleIntervals[6][7] = {
        {0, 2, 4, 5, 7, 9, 11},  // Major (Ionian)
        {0, 2, 3, 5, 7, 8, 10},  // Natural Minor (Aeolian)
        {0, 2, 3, 5, 7, 9, 10},  // Dorian
        {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
        {0, 2, 3, 5, 7, 8, 11},  // Harmonic Minor
        {0, 2, 3, 5, 7, 9, 11},  // Melodic Minor (ascending)
    };

    // Chromatic note number -> circle-of-fifths position (for thumbnail)
    static constexpr int kChromaticToFifths[12] = {
        0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5
    };

    // Circle-of-fifths position -> chromatic note number (for thumbnail)
    static constexpr int kFifthsOrder[12] = {
        0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5
    };

    // Param indices:
    //  0       = steps
    //  1       = key_root
    //  2       = mode
    //  3       = octave
    //  4       = beats_per_step
    //  5       = gate_length
    //  6       = velocity
    //  7..14   = degree_0..degree_7
    //  15..22  = voicing_0..voicing_7
    //  23..30  = ext_0..ext_7

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);           // 0
        out.push_back(&key_root);        // 1
        out.push_back(&mode);            // 2
        out.push_back(&octave);          // 3
        out.push_back(&beats_per_step);  // 4
        out.push_back(&gate_length);     // 5
        out.push_back(&velocity);        // 6
        out.push_back(&degree_0);  out.push_back(&degree_1);   // 7..14
        out.push_back(&degree_2);  out.push_back(&degree_3);
        out.push_back(&degree_4);  out.push_back(&degree_5);
        out.push_back(&degree_6);  out.push_back(&degree_7);
        out.push_back(&voicing_0); out.push_back(&voicing_1);  // 15..22
        out.push_back(&voicing_2); out.push_back(&voicing_3);
        out.push_back(&voicing_4); out.push_back(&voicing_5);
        out.push_back(&voicing_6); out.push_back(&voicing_7);
        out.push_back(&ext_0);    out.push_back(&ext_1);       // 23..30
        out.push_back(&ext_2);    out.push_back(&ext_3);
        out.push_back(&ext_4);    out.push_back(&ext_5);
        out.push_back(&ext_6);    out.push_back(&ext_7);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"notes",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"velocities", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
        out.push_back({"gates",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
    }

    // Build chord intervals relative to the chord root using diatonic third-stacking.
    // Returns the number of notes written to out_intervals[].
    static int build_chord(int scale_mode, int degree, int ext,
                           int* out_intervals, int max_notes) {
        if (scale_mode < 0 || scale_mode > 5) scale_mode = 0;
        if (degree < 0 || degree > 6) degree = 0;

        const int* scale = kScaleIntervals[scale_mode];
        int root_st = scale[degree];
        int count = 0;

        // Root
        out_intervals[count++] = 0;

        // Third (degree + 2)
        if (count < max_notes) {
            int interval = scale[(degree + 2) % 7] - root_st;
            if (interval <= 0) interval += 12;
            out_intervals[count++] = interval;
        }

        // Fifth (degree + 4)
        if (count < max_notes) {
            int interval = scale[(degree + 4) % 7] - root_st;
            if (interval <= 0) interval += 12;
            out_intervals[count++] = interval;
        }

        if (ext == 1 && count < max_notes) {
            // 7th (degree + 6)
            int interval = scale[(degree + 6) % 7] - root_st;
            if (interval <= 0) interval += 12;
            out_intervals[count++] = interval;
        } else if (ext == 2 && count < max_notes) {
            // Add9: 2nd scale degree above root, shifted up an octave
            int interval = scale[(degree + 1) % 7] - root_st;
            if (interval <= 0) interval += 12;
            interval += 12;
            out_intervals[count++] = interval;
        }

        return count;
    }

    // Apply voicing transformation to chord intervals (in-place).
    static void apply_voicing(int voicing, int* intervals, int count) {
        if (count < 2) return;
        std::sort(intervals, intervals + count);

        switch (voicing) {
            case 0: // Root position
                break;
            case 1: // First inversion: move bottom note up 12
                intervals[0] += 12;
                std::sort(intervals, intervals + count);
                break;
            case 2: // Second inversion: move two bottom notes up 12
                intervals[0] += 12;
                if (count > 1) intervals[1] += 12;
                std::sort(intervals, intervals + count);
                break;
            case 3: // Drop 2: move 2nd-from-top down 12
                if (count >= 2) {
                    intervals[count - 2] -= 12;
                    std::sort(intervals, intervals + count);
                }
                break;
        }
    }

    void process(const VividProcessContext* ctx) override {
        float beat_phase = ctx->input_values[0];
        int num_steps = steps.int_value();
        int kr = key_root.int_value();
        int m  = mode.int_value();
        int oct = octave.int_value();
        int bps = beats_per_step.int_value();
        float gl  = gate_length.value;
        float vel = velocity.value;

        if (kr < 0) kr = 0; if (kr > 11) kr = 11;
        if (m < 0) m = 0;   if (m > 5) m = 5;

        // Detect beat_phase wraps (delta < -0.5) -> increment beat_count_
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) {
            beat_count_++;
        }
        prev_phase_ = beat_phase;

        int current_step = (beat_count_ / bps) % num_steps;

        // Per-step params via param_values indices
        int degree  = static_cast<int>(ctx->param_values[7 + current_step]);
        int voicing = static_cast<int>(ctx->param_values[15 + current_step]);
        int ext     = static_cast<int>(ctx->param_values[23 + current_step]);
        if (degree < 0)  degree = 0;  if (degree > 6)  degree = 6;
        if (voicing < 0) voicing = 0; if (voicing > 3) voicing = 3;
        if (ext < 0)     ext = 0;     if (ext > 2)     ext = 2;

        // Build chord
        int intervals[5];
        int chord_size = build_chord(m, degree, ext, intervals, 5);
        apply_voicing(voicing, intervals, chord_size);

        // MIDI base note for the chord root
        int base_note = kr + oct * 12 + kScaleIntervals[m][degree];

        float gate_val = (beat_phase < gl) ? 1.0f : 0.0f;

        // Write output spreads
        if (ctx->output_spreads) {
            auto& notes_sp = ctx->output_spreads[0];
            auto& vel_sp   = ctx->output_spreads[1];
            auto& gates_sp = ctx->output_spreads[2];

            uint32_t len = static_cast<uint32_t>(chord_size);
            if (notes_sp.capacity >= len) {
                notes_sp.length = len;
                vel_sp.length   = len;
                gates_sp.length = len;
                for (uint32_t i = 0; i < len; ++i) {
                    notes_sp.data[i] = static_cast<float>(base_note + intervals[i]);
                    vel_sp.data[i]   = vel;
                    gates_sp.data[i] = gate_val;
                }
            }
        }

        // Scalar fallback: first note of chord
        if (chord_size > 0) {
            ctx->output_values[0] = static_cast<float>(base_note + intervals[0]);
            ctx->output_values[1] = vel;
            ctx->output_values[2] = gate_val;
        }
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        int num_steps = (ctx->param_count > 0)
            ? std::max(1, std::min(8, static_cast<int>(ctx->param_values[0]))) : 4;
        int kr = (ctx->param_count > 1)
            ? std::max(0, std::min(11, static_cast<int>(ctx->param_values[1]))) : 0;
        int m = (ctx->param_count > 2)
            ? std::max(0, std::min(5, static_cast<int>(ctx->param_values[2]))) : 0;

        // Detect current step from output note value
        int current_step = -1;
        if (ctx->output_count > 0) {
            float out_note = ctx->output_values[0];
            int oct = (ctx->param_count > 3) ? static_cast<int>(ctx->param_values[3]) : 4;
            for (int s = 0; s < num_steps; ++s) {
                if (ctx->param_count <= 7 + s) break;
                int deg  = std::max(0, std::min(6, static_cast<int>(ctx->param_values[7 + s])));
                int voic = (ctx->param_count > 15 + s)
                    ? std::max(0, std::min(3, static_cast<int>(ctx->param_values[15 + s]))) : 0;
                int ext  = (ctx->param_count > 23 + s)
                    ? std::max(0, std::min(2, static_cast<int>(ctx->param_values[23 + s]))) : 0;

                int intervals[5];
                int csz = build_chord(m, deg, ext, intervals, 5);
                apply_voicing(voic, intervals, csz);

                int base = kr + oct * 12 + kScaleIntervals[m][deg];
                float expected = static_cast<float>(base + intervals[0]);
                if (std::fabs(out_note - expected) < 0.5f) {
                    current_step = s;
                    break;
                }
            }
        }

        float w  = static_cast<float>(ctx->width);
        float h  = static_cast<float>(ctx->height);
        float cx = w * 0.5f;
        float cy = h * 0.5f;
        float radius = std::min(cx, cy) - 6.0f;

        // Background
        for (uint32_t y = 0; y < ctx->height; ++y) {
            uint8_t* row = ctx->pixels + y * ctx->stride;
            for (uint32_t x = 0; x < ctx->width; ++x) {
                uint8_t* px = row + x * 4;
                px[0] = 18; px[1] = 20; px[2] = 23; px[3] = 230;
            }
        }

        // Which chromatic notes are in the current scale?
        bool in_scale[12] = {};
        for (int d = 0; d < 7; ++d) {
            in_scale[(kr + kScaleIntervals[m][d]) % 12] = true;
        }

        // Current chord root (chromatic note number)
        int current_chord_root = -1;
        if (current_step >= 0 && ctx->param_count > 7 + current_step) {
            int deg = std::max(0, std::min(6,
                static_cast<int>(ctx->param_values[7 + current_step])));
            current_chord_root = (kr + kScaleIntervals[m][deg]) % 12;
        }

        constexpr float pi = 3.14159265358979f;

        // Draw line from key root to current chord root
        if (current_chord_root >= 0 && current_chord_root != kr) {
            int kr_pos = kChromaticToFifths[kr];
            int cc_pos = kChromaticToFifths[current_chord_root];
            float a0 = static_cast<float>(kr_pos) * (2.0f * pi / 12.0f) - pi * 0.5f;
            float a1 = static_cast<float>(cc_pos) * (2.0f * pi / 12.0f) - pi * 0.5f;
            float x0 = cx + radius * std::cos(a0);
            float y0 = cy + radius * std::sin(a0);
            float x1 = cx + radius * std::cos(a1);
            float y1 = cy + radius * std::sin(a1);

            int line_steps = static_cast<int>(
                std::max(std::fabs(x1 - x0), std::fabs(y1 - y0))) + 1;
            for (int i = 0; i <= line_steps; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(std::max(1, line_steps));
                int lx = static_cast<int>(x0 + (x1 - x0) * t);
                int ly = static_cast<int>(y0 + (y1 - y0) * t);
                if (lx >= 0 && lx < static_cast<int>(ctx->width) &&
                    ly >= 0 && ly < static_cast<int>(ctx->height)) {
                    uint8_t* px = ctx->pixels + ly * ctx->stride + lx * 4;
                    px[0] = static_cast<uint8_t>(std::min(255, px[0] + 30));
                    px[1] = static_cast<uint8_t>(std::min(255, px[1] + 35));
                    px[2] = static_cast<uint8_t>(std::min(255, px[2] + 40));
                }
            }
        }

        // Draw 12 dots around the circle in fifths order
        for (int pos = 0; pos < 12; ++pos) {
            int chromatic = kFifthsOrder[pos];
            float angle = static_cast<float>(pos) * (2.0f * pi / 12.0f) - pi * 0.5f;
            float dx = cx + radius * std::cos(angle);
            float dy = cy + radius * std::sin(angle);

            float dot_r;
            uint8_t cr, cg, cb, ca;

            if (chromatic == kr) {
                // Key root: bright gold, largest
                dot_r = 4.0f;
                cr = 255; cg = 200; cb = 80; ca = 255;
            } else if (chromatic == current_chord_root) {
                // Current chord root: bright cyan
                dot_r = 3.5f;
                cr = 80; cg = 220; cb = 255; ca = 255;
            } else if (in_scale[chromatic]) {
                // Scale degree: muted blue-grey
                dot_r = 2.5f;
                cr = 100; cg = 120; cb = 160; ca = 200;
            } else {
                // Non-scale: very dim
                dot_r = 1.5f;
                cr = 50; cg = 55; cb = 65; ca = 140;
            }

            // Draw filled circle
            int ix0 = std::max(0, static_cast<int>(dx - dot_r));
            int iy0 = std::max(0, static_cast<int>(dy - dot_r));
            int ix1 = std::min(static_cast<int>(ctx->width),
                               static_cast<int>(dx + dot_r + 1));
            int iy1 = std::min(static_cast<int>(ctx->height),
                               static_cast<int>(dy + dot_r + 1));

            for (int py = iy0; py < iy1; ++py) {
                uint8_t* row = ctx->pixels + py * ctx->stride;
                for (int px_x = ix0; px_x < ix1; ++px_x) {
                    float ddx = static_cast<float>(px_x) + 0.5f - dx;
                    float ddy = static_cast<float>(py) + 0.5f - dy;
                    if (ddx * ddx + ddy * ddy <= dot_r * dot_r) {
                        uint8_t* px = row + px_x * 4;
                        px[0] = cr; px[1] = cg; px[2] = cb; px[3] = ca;
                    }
                }
            }
        }
    }
};

VIVID_REGISTER(ChordProgression)
VIVID_THUMBNAIL(ChordProgression)
