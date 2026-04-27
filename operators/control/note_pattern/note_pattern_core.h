#pragma once
#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "note_helpers.h"
#include "note_id_counter.h"
#include <algorithm>
#include <cmath>
#include "operator_api/thumbnail.h"

namespace note_insp {
static const char* kNoteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static const char* kChordAbbr[] = {"M","m","dim","aug","7","m7","M7"};
static constexpr float kTypeColors[7][3] = {
    {0.39f, 0.63f, 0.86f}, {0.63f, 0.39f, 0.78f}, {0.78f, 0.39f, 0.39f},
    {0.86f, 0.71f, 0.31f}, {0.31f, 0.71f, 0.63f}, {0.55f, 0.47f, 0.78f},
    {0.31f, 0.55f, 0.86f},
};
static constexpr float kPreviewH = 60.0f;
static constexpr float kLineH = 18.0f;
} // namespace note_insp

/**
 * @brief Per-step chord pattern sequencer with visual grid editor.
 *
 * Sequences up to 8 steps, each with a configurable root note and chord
 * type (major, minor, 7th, dim, aug, sus2, sus4). Outputs chord notes
 * as a polyphonic spread with MIDI output.
 *
 * @see ChordProgression, Arpeggiator, PatternSeq
 */
struct NotePatternCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   steps        {"steps",          4, 1, 8};
    vivid::Param<int>   root_0       {"root_0",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_1       {"root_1",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_2       {"root_2",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_3       {"root_3",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_4       {"root_4",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_5       {"root_5",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_6       {"root_6",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   root_7       {"root_7",         0, {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}};
    vivid::Param<int>   type_0       {"type_0",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_1       {"type_1",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_2       {"type_2",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_3       {"type_3",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_4       {"type_4",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_5       {"type_5",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_6       {"type_6",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   type_7       {"type_7",         0, {"Major","Minor","Dim","Aug","Dom7","Min7","Maj7"}};
    vivid::Param<int>   octave       {"octave",         4, 2, 7};
    vivid::Param<int>   beats_per_step{"beats_per_step", 4, 1, 16};
    vivid::Param<float> gate_length  {"gate_length",    0.8f, 0.01f, 1.0f};
    vivid::Param<float> velocity     {"velocity",       0.8f, 0.0f, 1.0f};
    vivid::Param<int>   midi_channel {"midi_channel",   1, 1, 16};
    vivid::Param<int>   clock_source {"clock_source", vivid::kClockSourceExternal, vivid::clock_source_labels()};

    NotePatternCore() {
        vivid::description(steps, "Number of active chord steps in the sequence, 1 to 8");
        vivid::description(root_0, "Root note for step 1");
        vivid::description(root_1, "Root note for step 2");
        vivid::description(root_2, "Root note for step 3");
        vivid::description(root_3, "Root note for step 4");
        vivid::description(root_4, "Root note for step 5");
        vivid::description(root_5, "Root note for step 6");
        vivid::description(root_6, "Root note for step 7");
        vivid::description(root_7, "Root note for step 8");
        vivid::description(type_0, "Chord type for step 1");
        vivid::description(type_1, "Chord type for step 2");
        vivid::description(type_2, "Chord type for step 3");
        vivid::description(type_3, "Chord type for step 4");
        vivid::description(type_4, "Chord type for step 5");
        vivid::description(type_5, "Chord type for step 6");
        vivid::description(type_6, "Chord type for step 7");
        vivid::description(type_7, "Chord type for step 8");
        vivid::description(octave, "Base octave for all chord notes, 2 to 7");
        vivid::description(beats_per_step, "Number of beats each chord step lasts");
        vivid::description(gate_length, "Fraction of each step where notes are held, 0 to 1");
        vivid::description(velocity, "MIDI velocity for all chord notes, 0 to 1");
        vivid::description(midi_channel, "MIDI channel for chord output, 1 to 16");
        vivid::description(clock_source, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
    }

    // Internal state
    int beat_count_ = 0;
    float prev_phase_ = 0.0f;
    bool prev_gate_ = false;
    uint64_t prev_note_ids_[5] = {0, 0, 0, 0, 0};
    int prev_note_count_ = 0;
    VividNoteBuffer notes_buf_ = {};

    // Chord interval tables: [type][interval_count], terminated by -1
    // 0=major, 1=minor, 2=dim, 3=aug, 4=dom7, 5=min7, 6=maj7
    static constexpr int kChordIntervals[7][5] = {
        {0, 4, 7, -1, -1},    // major
        {0, 3, 7, -1, -1},    // minor
        {0, 3, 6, -1, -1},    // dim
        {0, 4, 8, -1, -1},    // aug
        {0, 4, 7, 10, -1},    // dom7
        {0, 3, 7, 10, -1},    // min7
        {0, 4, 7, 11, -1},    // maj7
    };

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);
        // Hide step params — rendered by custom inspector
        size_t hidden_start = out.size();
        out.push_back(&root_0); out.push_back(&root_1);
        out.push_back(&root_2); out.push_back(&root_3);
        out.push_back(&root_4); out.push_back(&root_5);
        out.push_back(&root_6); out.push_back(&root_7);
        out.push_back(&type_0); out.push_back(&type_1);
        out.push_back(&type_2); out.push_back(&type_3);
        out.push_back(&type_4); out.push_back(&type_5);
        out.push_back(&type_6); out.push_back(&type_7);
        for (size_t i = hidden_start; i < out.size(); ++i)
            out[i]->display_hint = VIVID_DISPLAY_HIDDEN;
        out.push_back(&octave);
        out.push_back(&beats_per_step);
        out.push_back(&gate_length);
        out.push_back(&velocity);
        out.push_back(&midi_channel);
        out.push_back(&clock_source);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"note", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"vel", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"gate", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));
    }

    void compute(float beat_phase, const float* params,
                 float* output_values, void** custom_outputs, uint32_t custom_output_count) {
        int num_steps = steps.int_value();
        int oct = octave.int_value();
        int bps = beats_per_step.int_value();
        float gl = gate_length.value;
        float vel = velocity.value;

        // Detect beat_phase wraps (delta < -0.5) -> increment beat_count_
        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) {
            beat_count_++;
        }
        prev_phase_ = beat_phase;

        // Current step
        int current_step = (beat_count_ / bps) % num_steps;

        // Look up root and chord type for current step
        // Params are ordered: steps, root_0..root_7, type_0..type_7, octave, beats_per_step, ...
        // root_0 is param index 1, type_0 is param index 9
        int root = static_cast<int>(params[1 + current_step]);
        int chord_type = static_cast<int>(params[9 + current_step]);
        if (chord_type < 0) chord_type = 0;
        if (chord_type > 6) chord_type = 6;

        // Count chord intervals
        const int* intervals = kChordIntervals[chord_type];
        int chord_size = 0;
        for (int i = 0; i < 5 && intervals[i] >= 0; ++i) {
            chord_size++;
        }

        // Gate: 1.0 if beat_phase < gate_length, else 0.0
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
                int note = std::clamp(root + oct * 12 + intervals[i], 0, 127);
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

        // Scalar fallback: first note
        if (chord_size > 0 && output_values) {
            output_values[0] = static_cast<float>(root + oct * 12 + intervals[0]);
            output_values[1] = vel;
            output_values[2] = gate_val;
        }
    }

    void draw_inspector(VividInspectorContext* ctx) override {
        namespace ni = note_insp;
        auto& d = ctx->draw;
        void* o = d.opaque;
        const auto& th = ctx->theme;

        float px = ctx->content_x;
        float py = ctx->content_y;
        float w = ctx->content_width;
        float h = ni::kPreviewH;

        int num_steps = (ctx->param_count > 0) ? static_cast<int>(ctx->param_values[0]) : 4;
        num_steps = std::max(1, std::min(8, num_steps));
        float cell_w = w / static_cast<float>(num_steps);

        // Detect current step from first output note (scalar fallback)
        int current_step = -1;
        if (ctx->output_count > 0) {
            float out_note = ctx->output_values[0];
            int oct = (ctx->param_count > 17) ? static_cast<int>(ctx->param_values[17]) : 4;
            for (int s = 0; s < num_steps; ++s) {
                int root = static_cast<int>(ctx->param_values[1 + s]);
                int chord_type = std::clamp(static_cast<int>(ctx->param_values[9 + s]), 0, 6);
                float expected = static_cast<float>(root + oct * 12 + kChordIntervals[chord_type][0]);
                if (std::fabs(out_note - expected) < 0.5f) {
                    current_step = s;
                    break;
                }
            }
        }

        py += 4;

        // Dark background
        d.draw_rect(o, px, py, w, h, {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

        for (int s = 0; s < num_steps; ++s) {
            int root = std::clamp(static_cast<int>(ctx->param_values[1 + s]), 0, 11);
            int chord_type = std::clamp(static_cast<int>(ctx->param_values[9 + s]), 0, 6);

            float cx = px + s * cell_w;
            bool is_current = (s == current_step);

            // Current step highlight
            if (is_current) {
                d.draw_rect(o, cx, py, cell_w, h, {0.18f, 0.22f, 0.30f, 0.6f});
            }

            // Note name centered
            const char* note = ni::kNoteNames[root];
            float nw = d.text_width(o, note, 1.0f);
            float text_x = cx + (cell_w - nw) * 0.5f;
            float text_y = py + 6;
            float bright = is_current ? 1.0f : 0.85f;
            d.draw_text(o, text_x, text_y, note, {bright, bright, bright, 1.0f}, 1.0f);

            // Chord abbreviation below in type color
            const char* chord = ni::kChordAbbr[chord_type];
            float cw2 = d.text_width(o, chord, 1.0f);
            float chord_x = cx + (cell_w - cw2) * 0.5f;
            float chord_y = text_y + ni::kLineH;
            const float* tc = ni::kTypeColors[chord_type];
            d.draw_text(o, chord_x, chord_y, chord,
                        {tc[0], tc[1], tc[2], is_current ? 1.0f : 0.7f}, 1.0f);

            // Colored bar at bottom
            float bar_h = 4.0f;
            float bar_y = py + h - bar_h - 2.0f;
            d.draw_rect(o, cx + 2, bar_y, cell_w - 4, bar_h,
                        {tc[0], tc[1], tc[2], is_current ? 0.9f : 0.6f});

            // Cell divider
            if (s > 0) {
                d.draw_rect(o, cx, py, 1, h,
                            {th.separator.r, th.separator.g, th.separator.b, 0.5f});
            }
        }

        ctx->consumed_height = 4.0f + h + 4.0f;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        const auto& d = ctx->draw;
        void* o = d.opaque;

        float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        int num_steps = (ctx->param_count > 0)
            ? std::max(1, std::min(8, static_cast<int>(ctx->param_values[0]))) : 4;

        // Detect current step
        int current_step = -1;
        if (ctx->output_count > 0) {
            float out_note = ctx->output_values[0];
            int oct = (ctx->param_count > 17) ? static_cast<int>(ctx->param_values[17]) : 4;
            for (int s = 0; s < num_steps; ++s) {
                int root = static_cast<int>(ctx->param_values[1 + s]);
                int chord_type = std::clamp(static_cast<int>(ctx->param_values[9 + s]), 0, 6);
                float expected = static_cast<float>(root + oct * 12 + kChordIntervals[chord_type][0]);
                if (std::fabs(out_note - expected) < 0.5f) {
                    current_step = s;
                    break;
                }
            }
        }

        // Dark background
        d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

        namespace ni = note_insp;

        float margin = 4.0f;
        float col_w = (w - 2 * margin) / static_cast<float>(num_steps);
        float gap = 1.5f;
        float text_h = 14.0f;
        float bar_area_h = h - margin - text_h;

        for (int s = 0; s < num_steps; ++s) {
            int root = std::clamp(static_cast<int>(ctx->param_values[1 + s]), 0, 11);
            int chord_type = std::clamp(static_cast<int>(ctx->param_values[9 + s]), 0, 6);
            bool is_current = (s == current_step);

            float cx = margin + s * col_w;
            float bw = col_w - 2 * gap;

            // Bar height proportional to root note (0-11)
            float bar_frac = (static_cast<float>(root) + 1.0f) / 12.0f;
            float bar_h = bar_frac * (bar_area_h - margin);
            float by = margin + bar_area_h - bar_h;

            // Current step highlight column
            if (is_current) {
                d.draw_rect(o, cx, margin, col_w, bar_area_h,
                            {0.15f, 0.17f, 0.2f, 0.4f});
            }

            float alpha = is_current ? 0.9f : 0.7f;
            d.draw_rounded_rect(o, cx + gap, by, bw, bar_h, 2.0f,
                                {ni::kTypeColors[chord_type][0], ni::kTypeColors[chord_type][1],
                                 ni::kTypeColors[chord_type][2], alpha});

            // Note name + chord type text below bars
            char label[8];
            std::snprintf(label, sizeof(label), "%s%s", ni::kNoteNames[root], ni::kChordAbbr[chord_type]);
            float text_x = cx + col_w * 0.5f - d.text_width(o, label, 0.7f) * 0.5f;
            d.draw_text(o, text_x, h - text_h + 1, label,
                        {0.5f, 0.55f, 0.6f, is_current ? 1.0f : 0.7f}, 0.7f);
        }
    }
};
