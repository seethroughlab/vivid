#pragma once

#include "operator_api/metronome_sync.h"
#include "operator_api/note_types.h"
#include "operator_api/operator.h"
#include "operator_api/type_id.h"
#include "operator_api/editor_ui.h"
#include "operator_api/editor_keys.h"
#include "euclidean_editor_shared.h"
#include "note_helpers.h"
#include "note_id_counter.h"
#include <algorithm>
#include <cmath>

/**
 * @brief Euclidean rhythm generator distributing hits evenly across steps.
 *
 * Uses the Bjorklund algorithm to space a given number of hits as evenly
 * as possible across a step count. Rotation shifts the pattern start point.
 *
 * @tip 3 hits in 8 steps gives the classic tresillo rhythm. 5 in 8 gives a cinquillo.
 * @param hits Number of active steps in the pattern.
 * @param steps Total pattern length.
 * @param rotation Rotates the pattern by N steps.
 * @see DrumSequencer, StepSeq, PatternSeq
 */
struct EuclideanCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   hits        {"hits",        3, 0, 32};
    vivid::Param<int>   steps       {"steps",       8, 1, 32};
    vivid::Param<int>   rotation    {"rotation",    0, 0, 31};
    vivid::Param<float> gate_length {"gate_length", 0.5f, 0.01f, 1.0f};
    vivid::Param<int>   rate        {"rate",        2, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"}};
    vivid::Param<int>   clock_source{"clock_source", vivid::kClockSourceExternal, vivid::clock_source_labels()};
    vivid::Param<int>   bar_sync    {"bar_sync",    0, {"off","1 bar","2 bar","4 bar","8 bar"}};
    vivid::Param<int>   note        {"note",        36, 0, 127};
    vivid::Param<int>   velocity    {"velocity",    100, 1, 127};

    EuclideanCore() {
        vivid::semantic_tag(hits, "count");
        vivid::semantic_shape(hits, "int");
        vivid::description(hits, "Number of active hits distributed across the pattern");

        vivid::semantic_tag(steps, "count");
        vivid::semantic_shape(steps, "int");
        vivid::description(steps, "Total number of steps in the pattern");

        vivid::semantic_tag(rotation, "index");
        vivid::semantic_shape(rotation, "int");
        vivid::description(rotation, "Rotates the pattern start point by N steps");

        vivid::semantic_tag(gate_length, "phase_01");
        vivid::semantic_shape(gate_length, "scalar");
        vivid::description(gate_length, "Fraction of each step during which the gate stays high");

        vivid::description(rate, "Clock subdivision for step timing");
        vivid::description(clock_source, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
        vivid::description(bar_sync,
            "Restart pattern at the top of every Nth bar (only when clock_source = metronome).");

        vivid::semantic_tag(note, "midi_note");
        vivid::semantic_shape(note, "int");
        vivid::description(note, "MIDI note number emitted on each pattern hit (default 36 = C2 / kick).");

        vivid::semantic_tag(velocity, "midi_velocity");
        vivid::semantic_shape(velocity, "int");
        vivid::description(velocity, "MIDI velocity (1-127) sent with each note-on.");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&hits);        // 0
        out.push_back(&steps);       // 1
        out.push_back(&rotation);    // 2
        out.push_back(&gate_length); // 3
        out.push_back(&rate);        // 4
        out.push_back(&clock_source);// 5
        out.push_back(&bar_sync);    // 6
        out.push_back(&note);        // 7
        out.push_back(&velocity);    // 8
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_SCALAR,     VIVID_PORT_INPUT});   // in[0]
        out.push_back({"trigger",    VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // out[0]
        out.push_back({"gate",       VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // out[1]
        out.push_back({"step",       VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // out[2]
        out.push_back({"pattern",    VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // lane_array[0]
        // Canonical native note output — drives any voice synth's notes_in directly.
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));
    }

    void compute(float beat_phase, double beats_elapsed, int beats_per_bar,
                 const float* params,
                 VividLaneOutput* output_lanes, float* output_values,
                 void** custom_outputs = nullptr,
                 uint32_t custom_output_count = 0) {
        int h   = std::clamp(static_cast<int>(params[0]), 0, 32);
        int n   = std::clamp(static_cast<int>(params[1]), 1, 32);
        int rot = std::clamp(static_cast<int>(params[2]), 0, 31);
        float gl = params[3];
        int r   = std::clamp(static_cast<int>(params[4]), 0, 8);
        int cs  = static_cast<int>(params[5]);
        int sync_idx = std::clamp(static_cast<int>(params[6]), 0, 4);
        int midi_note = std::clamp(static_cast<int>(params[7]), 0, 127);
        int midi_vel  = std::clamp(static_cast<int>(params[8]), 1, 127);

        if (h != prev_hits_ || n != prev_steps_ || rot != prev_rotation_) {
            ::vivid::euclidean_editor::compute_pattern(h, n, rot, pattern_);
            prev_hits_ = h;
            prev_steps_ = n;
            prev_rotation_ = rot;
        }

        // Phrase reset: when synced to the graph metronome, snap the internal
        // beat counter back to 0 at the start of each N-bar phrase so the
        // pattern always restarts at step 0 on phrase boundaries.
        if (sync_idx > 0 && cs == vivid::kClockSourceMetronome) {
            static constexpr int kSyncBars[] = {0, 1, 2, 4, 8};
            const int bpb = std::max(1, beats_per_bar);
            const double phrase_beats = static_cast<double>(bpb) * kSyncBars[sync_idx];
            const int64_t phrase_idx =
                static_cast<int64_t>(std::floor(beats_elapsed / phrase_beats));
            if (phrase_initialized_ && phrase_idx != prev_phrase_idx_) {
                beat_count_ = 0;
                prev_phase_ = beat_phase;
                prev_step_ = -1;
            }
            prev_phrase_idx_ = phrase_idx;
            phrase_initialized_ = true;
        }

        float delta = beat_phase - prev_phase_;
        if (delta < -0.5f) beat_count_++;
        prev_phase_ = beat_phase;

        float multiplier = kMultipliers[r];
        float total_beats = static_cast<float>(beat_count_) + beat_phase;
        float scaled_phase = total_beats * multiplier;

        int global_step = static_cast<int>(std::floor(scaled_phase));
        int current_step = ((global_step % n) + n) % n;
        float step_phase = scaled_phase - std::floor(scaled_phase);

        bool new_step = (current_step != prev_step_);
        prev_step_ = current_step;

        bool is_hit = (pattern_[current_step] != 0);
        const float trigger = (new_step && is_hit) ? 1.0f : 0.0f;
        const float gate    = (is_hit && step_phase < gl) ? 1.0f : 0.0f;
        output_values[0] = trigger;
        output_values[1] = gate;
        output_values[2] = static_cast<float>(current_step);

        if (output_lanes) {
            auto& pattern_lane = output_lanes[3];
            auto len = static_cast<uint32_t>(n);
            float* buf = pattern_lane.resize(pattern_lane.handle, len);
            if (buf) {
                for (uint32_t i = 0; i < len; ++i)
                    buf[i] = static_cast<float>(pattern_[i]);
                pattern_lane.commit(pattern_lane.handle, len);
            }
        }

        // Emit note-on at the rising edge of the gate, note-off at the
        // falling edge. The held id releases cleanly even if the `note`
        // param changes while the note is sustaining.
        notes_buf_.count = 0;
        const bool gate_high = gate > 0.5f;
        if (gate_high && !prev_gate_high_) {
            held_note_id_ = vivid_sequencers::next_note_id();
            vivid_sequencers::note_on(notes_buf_,
                static_cast<uint8_t>(midi_note),
                static_cast<float>(midi_vel) / 127.0f,
                held_note_id_);
        } else if (!gate_high && prev_gate_high_ && held_note_id_ != 0) {
            vivid_sequencers::note_off(notes_buf_, held_note_id_);
            held_note_id_ = 0;
        }
        prev_gate_high_ = gate_high;

        if (custom_outputs && custom_output_count > 0) {
            custom_outputs[0] = &notes_buf_;
        }
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

    // Editor window. Drives `hits` / `steps` / `rotation` via the host
    // command API; reuses the shared Bjorklund helper so the editor
    // preview and `compute()` can never drift.
    static VividEditorMetadata editor_metadata();
    void draw_editor(VividEditorContext* ctx);

    // Editor state: just a density-preset cursor for the D-key quick-cycle.
    int  editor_preset_cursor_ = 0;
    bool editor_drag_rotation_ = false;  // horizontal-drag-to-scrub-rotation

    // Expose pattern for thumbnail access
    const int* current_pattern() const { return pattern_; }

private:
    static constexpr float kMultipliers[] = {
        0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 1.5f, 3.0f, 6.0f
    };

    float prev_phase_ = 0.0f;
    int beat_count_ = 0;
    int prev_step_ = -1;
    int prev_hits_ = -1;
    int prev_steps_ = -1;
    int prev_rotation_ = -1;
    int pattern_[32] = {};
    int64_t prev_phrase_idx_ = 0;
    bool phrase_initialized_ = false;

    // Native note emission state.
    VividNoteBuffer notes_buf_ = {};
    bool     prev_gate_high_ = false;
    uint64_t held_note_id_   = 0;
};
