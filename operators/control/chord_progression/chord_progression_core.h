#pragma once
#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "note_helpers.h"
#include "note_id_counter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "operator_api/thumbnail.h"
#include <cstring>

namespace chord_insp {
static constexpr float kCardGap  = 2.0f;
static constexpr float kRowH     = 16.0f;
static constexpr float kNameRowH = 18.0f;
static constexpr float kPad      = 2.0f;

static const char* kNoteNames[12] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};
static const char* kDegreeLabels[7] = {"I","II","III","IV","V","VI","VII"};
static const char* kVoicingLabels[4] = {"Root","Inv1","Inv2","Drop2"};
static const char* kExtLabels[3] = {"Triad","7th","Add9"};
static const char* kRowLabels[3] = {"Deg","Voic","Ext"};

// Build a human-readable chord name like "Cmaj7", "Am", "Gadd9".
// buf must be at least 12 bytes.
static void chord_display_name(char* buf, int buf_size,
                               const int scale[7], int key_root, int degree, int ext) {
    int root_chromatic = (key_root + scale[degree]) % 12;
    // Determine quality from the 3rd interval
    int third_interval = scale[(degree + 2) % 7] - scale[degree];
    if (third_interval <= 0) third_interval += 12;
    bool is_minor = (third_interval == 3);

    const char* quality = is_minor ? "m" : "";
    const char* ext_suffix = "";
    if (ext == 1) ext_suffix = "7";
    else if (ext == 2) ext_suffix = "add9";

    std::snprintf(buf, buf_size, "%s%s%s", kNoteNames[root_chromatic], quality, ext_suffix);
}
} // namespace chord_insp

/**
 * @brief Diatonic chord sequencer with per-step voicing and extensions.
 *
 * Sequences up to 8 chords in a chosen key and mode, each with configurable
 * scale degree, inversion, and extension (triad, 7th, add9). Emits a native
 * note stream on `notes_out` plus scalar convenience signals for the first
 * chord tone.
 *
 * @input beat_phase Optional beat phase override (0-1 sawtooth). When unconnected, syncs to the graph metronome via clock_source.
 * @output note First note of the current chord as a scalar convenience output.
 * @output vel First velocity of the current chord as a scalar convenience output.
 * @output gate First gate of the current chord as a scalar convenience output.
 * @output notes_out Native note-stream output carrying the chord's note events.
 * @recipe ChordProgression/notes_out -> Synth/notes_in (e.g. WavetableOsc, FmSynth, AnalogOsc)
 * @recipe ChordProgression/notes_out -> NoteBreakout/notes_in (when shared per-voice control state is needed)
 * @pitfall ChordProgression emits a note stream; downstream voice shaping uses notes_in or NoteBreakout's voice_* breakouts.
 * @family note_source
 * @best_used_with NoteBreakout, Envelope
 * @common_companions Arpeggiator, WavetableOsc, VoiceMixer
 * @param mode Scale mode: Major, Minor, Dorian, Mixolydian, Harmonic Minor, Melodic Minor.
 * @see NotePattern, Arpeggiator, Sequencer
 */
struct ChordProgressionCore : vivid::OperatorBase {
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

    vivid::Param<int> midi_channel {"midi_channel", 1, 1, 16};
    vivid::Param<int> clock_source {"clock_source", vivid::kClockSourceMetronome, vivid::clock_source_labels()};

    // Internal state
    int beat_count_ = 0;
    float prev_phase_ = 0.0f;
    bool prev_gate_ = false;
    uint64_t prev_note_ids_[5] = {0, 0, 0, 0, 0};
    int prev_note_count_ = 0;
    VividNoteBuffer notes_buf_ = {};

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
    //  31      = midi_channel

    ChordProgressionCore() {
        vivid::description(steps, "Number of chords in the progression (1-8)");
        vivid::description(key_root, "Root note of the scale");
        vivid::description(mode, "Scale mode: Major, Minor, Dorian, Mixolydian, Harmonic/Melodic Minor");
        vivid::description(octave, "Base octave for chord voicings");
        vivid::description(beats_per_step, "How many beats each chord is held");
        vivid::description(gate_length, "Fraction of each step where the gate is high (0-1)");
        vivid::description(velocity, "MIDI velocity for all chord notes (0-1)");
        vivid::description(clock_source, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");

        vivid::description(degree_0, "Scale degree for step 1");
        vivid::description(degree_1, "Scale degree for step 2");
        vivid::description(degree_2, "Scale degree for step 3");
        vivid::description(degree_3, "Scale degree for step 4");
        vivid::description(degree_4, "Scale degree for step 5");
        vivid::description(degree_5, "Scale degree for step 6");
        vivid::description(degree_6, "Scale degree for step 7");
        vivid::description(degree_7, "Scale degree for step 8");

        vivid::description(voicing_0, "Voicing for step 1: root position, inversions, or drop-2");
        vivid::description(voicing_1, "Voicing for step 2: root position, inversions, or drop-2");
        vivid::description(voicing_2, "Voicing for step 3: root position, inversions, or drop-2");
        vivid::description(voicing_3, "Voicing for step 4: root position, inversions, or drop-2");
        vivid::description(voicing_4, "Voicing for step 5: root position, inversions, or drop-2");
        vivid::description(voicing_5, "Voicing for step 6: root position, inversions, or drop-2");
        vivid::description(voicing_6, "Voicing for step 7: root position, inversions, or drop-2");
        vivid::description(voicing_7, "Voicing for step 8: root position, inversions, or drop-2");

        vivid::description(ext_0, "Extension for step 1: triad, 7th, or add9");
        vivid::description(ext_1, "Extension for step 2: triad, 7th, or add9");
        vivid::description(ext_2, "Extension for step 3: triad, 7th, or add9");
        vivid::description(ext_3, "Extension for step 4: triad, 7th, or add9");
        vivid::description(ext_4, "Extension for step 5: triad, 7th, or add9");
        vivid::description(ext_5, "Extension for step 6: triad, 7th, or add9");
        vivid::description(ext_6, "Extension for step 7: triad, 7th, or add9");
        vivid::description(ext_7, "Extension for step 8: triad, 7th, or add9");

        vivid::description(midi_channel, "MIDI output channel (1-16)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);           // 0
        out.push_back(&key_root);        // 1
        out.push_back(&mode);            // 2
        out.push_back(&octave);          // 3
        out.push_back(&beats_per_step);  // 4
        out.push_back(&gate_length);     // 5
        out.push_back(&velocity);        // 6

        // Per-step params: rendered by custom inspector, hidden from standard list
        size_t hidden_start = out.size();
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
        for (size_t i = hidden_start; i < out.size(); ++i)
            out[i]->display_hint = VIVID_DISPLAY_HIDDEN;

        out.push_back(&midi_channel); // 31
        out.push_back(&clock_source); // 32
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "beat_phase",  "scalar", "global_transport_phase",      "Global beat phase used to advance the chord progression."});
        out.push_back({"note",       VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "midi_note",   "scalar", "monophonic_preview_pitch",    "First note of the current chord as a scalar convenience output."});
        out.push_back({"vel",        VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "velocity",    "scalar", "monophonic_preview_velocity", "First velocity of the current chord as a scalar convenience output."});
        out.push_back({"gate",       VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "gate",        "scalar", "monophonic_preview_gate",     "First gate of the current chord as a scalar convenience output."});
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));
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

    void compute(float beat_phase, const float* params,
                 float* output_values, void** custom_outputs, uint32_t custom_output_count) {
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
        int degree  = static_cast<int>(params[7 + current_step]);
        int voicing = static_cast<int>(params[15 + current_step]);
        int ext     = static_cast<int>(params[23 + current_step]);
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

        // Native note output: polyphonic on/off on gate edges. Each chord
        // tone gets its own note_id so downstream synths allocate distinct
        // voices and same-pitch retriggers work cleanly.
        notes_buf_.count = 0;
        bool gate_high = (gate_val > 0.5f);
        if (gate_high && !prev_gate_) {
            // Gate rising: note-off previous chord, note-on new chord
            for (int i = 0; i < prev_note_count_; ++i) {
                if (prev_note_ids_[i] != 0) {
                    vivid_sequencers::note_off(notes_buf_, prev_note_ids_[i]);
                    prev_note_ids_[i] = 0;
                }
            }
            prev_note_count_ = chord_size;
            for (int i = 0; i < chord_size && i < 5; ++i) {
                int note = std::clamp(base_note + intervals[i], 0, 127);
                uint64_t id = vivid_sequencers::next_note_id();
                vivid_sequencers::note_on(notes_buf_,
                    static_cast<uint8_t>(note), vel, id);
                prev_note_ids_[i] = id;
            }
        } else if (!gate_high && prev_gate_) {
            // Gate falling: note-off all
            for (int i = 0; i < prev_note_count_; ++i) {
                if (prev_note_ids_[i] != 0) {
                    vivid_sequencers::note_off(notes_buf_, prev_note_ids_[i]);
                    prev_note_ids_[i] = 0;
                }
            }
            prev_note_count_ = 0;
        }
        prev_gate_ = gate_high;
        if (custom_outputs && custom_output_count > 0) {
            custom_outputs[0] = &notes_buf_;
        }

        // Scalar fallback: first note of chord
        if (chord_size > 0 && output_values) {
            output_values[0] = static_cast<float>(base_note + intervals[0]);
            output_values[1] = vel;
            output_values[2] = gate_val;
        }
    }

    void draw_inspector(VividInspectorContext* ctx) override {
        namespace ci = chord_insp;
        auto& d = ctx->draw;
        void* o = d.opaque;
        const auto& th = ctx->theme;
        const auto& mouse = ctx->mouse;

        float px = ctx->content_x;
        float base_y = ctx->content_y;
        float panel_w = ctx->content_width;

        int num_steps = (ctx->param_count > 0)
            ? std::clamp(static_cast<int>(ctx->param_values[0]), 1, 8) : 4;
        int kr = (ctx->param_count > 1)
            ? std::clamp(static_cast<int>(ctx->param_values[1]), 0, 11) : 0;
        int m = (ctx->param_count > 2)
            ? std::clamp(static_cast<int>(ctx->param_values[2]), 0, 5) : 0;

        // Detect current playing step from scalar "note" output (output index 0)
        int current_step = -1;
        if (ctx->output_count > 0) {
            float out_note = ctx->output_values[0];
            int oct = (ctx->param_count > 3) ? static_cast<int>(ctx->param_values[3]) : 4;
            for (int s = 0; s < num_steps; ++s) {
                if (ctx->param_count <= static_cast<uint32_t>(7 + s)) break;
                int deg = std::clamp(static_cast<int>(ctx->param_values[7 + s]), 0, 6);
                int voic = (ctx->param_count > static_cast<uint32_t>(15 + s))
                    ? std::clamp(static_cast<int>(ctx->param_values[15 + s]), 0, 3) : 0;
                int ext = (ctx->param_count > static_cast<uint32_t>(23 + s))
                    ? std::clamp(static_cast<int>(ctx->param_values[23 + s]), 0, 2) : 0;

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

        float y = 4.0f; // relative y offset

        // Row labels column
        float label_w = 30.0f;
        float grid_x = label_w;
        float actual_card_w = (panel_w - label_w - ci::kCardGap * 7.0f) / 8.0f;
        if (actual_card_w < 28.0f) actual_card_w = 28.0f;

        float total_h = ci::kNameRowH + ci::kRowH * 3.0f;

        vivid::draw_ui::draw_panel(d, o, px, base_y + y, panel_w, total_h + 4.0f,
                                   {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

        float grid_y = y + 2.0f;

        // Row labels
        const char* row_labels[4] = {"", "Deg", "Voic", "Ext"};
        float row_tops[4] = {grid_y, grid_y + ci::kNameRowH,
                             grid_y + ci::kNameRowH + ci::kRowH,
                             grid_y + ci::kNameRowH + ci::kRowH * 2.0f};
        float row_heights[4] = {ci::kNameRowH, ci::kRowH, ci::kRowH, ci::kRowH};
        for (int r = 1; r < 4; ++r) {
            d.draw_text(o, px + 2.0f, base_y + row_tops[r] + 2.0f, row_labels[r],
                        {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.5f}, 1.0f);
        }

        const int* scale = kScaleIntervals[m];

        for (int s = 0; s < 8; ++s) {
            float cx = grid_x + s * (actual_card_w + ci::kCardGap);
            bool beyond = (s >= num_steps);
            float col_alpha = beyond ? 0.25f : 1.0f;

            // Current step highlight
            if (s == current_step && !beyond) {
                vivid::draw_ui::draw_selection_highlight(d, o,
                    px + cx, base_y + grid_y, actual_card_w, total_h,
                    {th.accent.r, th.accent.g, th.accent.b, 1.0f});
            }

            // Read per-step param values
            int deg = (ctx->param_count > static_cast<uint32_t>(7 + s))
                ? std::clamp(static_cast<int>(ctx->param_values[7 + s]), 0, 6) : 0;
            int voic = (ctx->param_count > static_cast<uint32_t>(15 + s))
                ? std::clamp(static_cast<int>(ctx->param_values[15 + s]), 0, 3) : 0;
            int ext = (ctx->param_count > static_cast<uint32_t>(23 + s))
                ? std::clamp(static_cast<int>(ctx->param_values[23 + s]), 0, 2) : 0;

            // Row 0: Chord name (display only)
            char name_buf[16];
            ci::chord_display_name(name_buf, sizeof(name_buf), scale, kr, deg, ext);
            vivid::draw_ui::draw_text_aligned(d, o,
                px + cx, base_y + row_tops[0] + 2.0f, actual_card_w,
                name_buf,
                {th.bright_text.r, th.bright_text.g, th.bright_text.b, col_alpha},
                1.0f, 0.5f);

            // Rows 1-3: degree, voicing, extension (clickable)
            const char* cell_labels[3] = {ci::kDegreeLabels[deg], ci::kVoicingLabels[voic], ci::kExtLabels[ext]};
            int cell_maxes[3] = {7, 4, 3};
            int cell_vals[3] = {deg, voic, ext};
            const char* param_prefixes[3] = {"degree_", "voicing_", "ext_"};

            for (int r = 0; r < 3; ++r) {
                float ry = row_tops[1 + r];
                float rh = row_heights[1 + r];

                // Cell background
                vivid::draw_ui::draw_grid_cell(d, o,
                    px + cx + ci::kPad, base_y + ry + ci::kPad,
                    actual_card_w - 2.0f * ci::kPad, rh - 2.0f * ci::kPad,
                    cell_labels[r],
                    {th.slider_track.r, th.slider_track.g, th.slider_track.b, 0.6f * col_alpha},
                    {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.9f * col_alpha},
                    2.0f, 1.0f);

                // Click interaction: left=forward, right or shift+left=backward
                bool left_hit = mouse.left_clicked &&
                    mouse.x >= cx && mouse.x < cx + actual_card_w &&
                    mouse.y >= ry && mouse.y < ry + rh;
                bool right_hit = mouse.right_clicked &&
                    mouse.x >= cx && mouse.x < cx + actual_card_w &&
                    mouse.y >= ry && mouse.y < ry + rh;

                if (left_hit || right_hit) {
                    int max_val = cell_maxes[r];
                    int new_val;
                    if (right_hit || mouse.shift_down) {
                        new_val = (cell_vals[r] + max_val - 1) % max_val;
                    } else {
                        new_val = (cell_vals[r] + 1) % max_val;
                    }
                    char param_name[16];
                    std::snprintf(param_name, sizeof(param_name), "%s%d", param_prefixes[r], s);
                    ctx->commands.set_param(ctx->commands.opaque, param_name, static_cast<float>(new_val));
                }
            }
        }

        y += total_h + 8.0f;
        ctx->consumed_height = y;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        const auto& d = ctx->draw;
        void* o = d.opaque;

        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        int kr = (ctx->param_count > 1)
            ? std::max(0, std::min(11, static_cast<int>(ctx->param_values[1]))) : 0;
        int m = (ctx->param_count > 2)
            ? std::max(0, std::min(5, static_cast<int>(ctx->param_values[2]))) : 0;
        int num_steps = (ctx->param_count > 0)
            ? std::max(1, std::min(8, static_cast<int>(ctx->param_values[0]))) : 4;

        // Compute scale mask
        uint32_t scale_mask = 0;
        for (int i = 0; i < 7; ++i)
            scale_mask |= (1u << ((kr + kScaleIntervals[m][i]) % 12));

        // Detect current chord root
        int current_chord_root = -1;
        if (ctx->output_count > 0) {
            float out_note = ctx->output_values[0];
            int oct = (ctx->param_count > 3) ? static_cast<int>(ctx->param_values[3]) : 4;
            for (int s = 0; s < num_steps; ++s) {
                if (ctx->param_count <= static_cast<uint32_t>(7 + s)) break;
                int deg = std::max(0, std::min(6, static_cast<int>(ctx->param_values[7 + s])));
                int voic = (ctx->param_count > static_cast<uint32_t>(15 + s))
                    ? std::max(0, std::min(3, static_cast<int>(ctx->param_values[15 + s]))) : 0;
                int ext = (ctx->param_count > static_cast<uint32_t>(23 + s))
                    ? std::max(0, std::min(2, static_cast<int>(ctx->param_values[23 + s]))) : 0;
                int intervals[5];
                int csz = build_chord(m, deg, ext, intervals, 5);
                apply_voicing(voic, intervals, csz);
                int base = kr + oct * 12 + kScaleIntervals[m][deg];
                float expected = static_cast<float>(base + intervals[0]);
                if (std::fabs(out_note - expected) < 0.5f) {
                    current_chord_root = (kr + kScaleIntervals[m][deg]) % 12;
                    break;
                }
            }
        }

        // Dark background
        d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

        // Circle of fifths: chromatic note -> position on circle
        static constexpr int kFifths[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5};
        // Fifths position -> chromatic note
        static constexpr int kFifthsOrder[12] = {0, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10, 5};
        static const char* kNoteNames[12] = {"C","G","D","A","E","B","F#","Db","Ab","Eb","Bb","F"};

        float cx = w * 0.5f;
        float cy = h * 0.45f;
        float radius = std::min(w, h) * 0.35f;
        constexpr float pi = 3.14159265359f;

        VividColor key_col   = {1.0f, 0.78f, 0.31f, 1.0f};   // gold
        VividColor chord_col = {0.31f, 0.86f, 1.0f, 1.0f};   // cyan
        VividColor scale_col = {0.39f, 0.47f, 0.63f, 0.78f};  // blue-gray
        VividColor dim_col   = {0.2f, 0.22f, 0.25f, 0.55f};   // dark

        // Draw line from key root to chord root
        if (current_chord_root >= 0 && current_chord_root != kr) {
            int kr_pos = kFifths[kr];
            int cc_pos = kFifths[current_chord_root];
            float a0 = static_cast<float>(kr_pos) * (2.0f * pi / 12.0f) - pi * 0.5f;
            float a1 = static_cast<float>(cc_pos) * (2.0f * pi / 12.0f) - pi * 0.5f;
            float x0 = cx + radius * std::cos(a0);
            float y0 = cy + radius * std::sin(a0);
            float x1 = cx + radius * std::cos(a1);
            float y1 = cy + radius * std::sin(a1);
            d.draw_line(o, x0, y0, x1, y1, 1.0f, {0.3f, 0.33f, 0.38f, 0.5f});
        }

        // Draw 12 dots as circles (rounded rects with radius = half size)
        for (int pos = 0; pos < 12; ++pos) {
            int chromatic = kFifthsOrder[pos];
            float angle = static_cast<float>(pos) * (2.0f * pi / 12.0f) - pi * 0.5f;
            float dx = cx + radius * std::cos(angle);
            float dy = cy + radius * std::sin(angle);

            float dot_r;
            VividColor col;

            if (chromatic == kr) {
                dot_r = 5.0f; col = key_col;
            } else if (chromatic == current_chord_root) {
                dot_r = 4.5f; col = chord_col;
            } else if ((scale_mask & (1u << chromatic)) != 0u) {
                dot_r = 3.0f; col = scale_col;
            } else {
                dot_r = 2.0f; col = dim_col;
            }

            d.draw_rounded_rect(o, dx - dot_r, dy - dot_r, dot_r * 2, dot_r * 2, dot_r, col);
        }

        // Key root note name label
        static const char* kChromaticNames[12] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        float tw = d.text_width(o, kChromaticNames[kr], 0.85f);
        d.draw_text(o, cx - tw * 0.5f, h - 14, kChromaticNames[kr], key_col, 0.85f);
    }
};
