#pragma once
#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "operator_api/editor_ui.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "note_helpers.h"
#include "note_id_counter.h"
#include "sequencer_editor_shared.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * @brief Step sequencer with inline editing, ratchets, probability, and MIDI.
 *
 * In internal mode, sequences up to 32 steps with per-step values and gates,
 * edited via the inspector grid widget. In external mode, reads values,
 * probabilities, and ratchet counts from input lane arrays.
 *
 * Supports free-running, external beat-phase, and metronome clock modes.
 * Outputs value, step index, and trigger as scalars, plus MIDI note output.
 *
 * @see PatternSeq, Arpeggiator, LFO
 */
struct SequencerCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;
    static constexpr int kMaxSteps = 32;

    // --- Source toggle ---
    vivid::Param<int> source {"source", 0, {"internal", "external"}};

    // --- Step count ---
    vivid::Param<int> steps {"steps", 8, 1, kMaxSteps};

    // --- Inline step values (internal mode) ---
    vivid::Param<float> step_value[kMaxSteps] = {
        {"step_value_0",  0.5f, 0.0f, 1.0f}, {"step_value_1",  0.5f, 0.0f, 1.0f},
        {"step_value_2",  0.5f, 0.0f, 1.0f}, {"step_value_3",  0.5f, 0.0f, 1.0f},
        {"step_value_4",  0.5f, 0.0f, 1.0f}, {"step_value_5",  0.5f, 0.0f, 1.0f},
        {"step_value_6",  0.5f, 0.0f, 1.0f}, {"step_value_7",  0.5f, 0.0f, 1.0f},
        {"step_value_8",  0.5f, 0.0f, 1.0f}, {"step_value_9",  0.5f, 0.0f, 1.0f},
        {"step_value_10", 0.5f, 0.0f, 1.0f}, {"step_value_11", 0.5f, 0.0f, 1.0f},
        {"step_value_12", 0.5f, 0.0f, 1.0f}, {"step_value_13", 0.5f, 0.0f, 1.0f},
        {"step_value_14", 0.5f, 0.0f, 1.0f}, {"step_value_15", 0.5f, 0.0f, 1.0f},
        {"step_value_16", 0.5f, 0.0f, 1.0f}, {"step_value_17", 0.5f, 0.0f, 1.0f},
        {"step_value_18", 0.5f, 0.0f, 1.0f}, {"step_value_19", 0.5f, 0.0f, 1.0f},
        {"step_value_20", 0.5f, 0.0f, 1.0f}, {"step_value_21", 0.5f, 0.0f, 1.0f},
        {"step_value_22", 0.5f, 0.0f, 1.0f}, {"step_value_23", 0.5f, 0.0f, 1.0f},
        {"step_value_24", 0.5f, 0.0f, 1.0f}, {"step_value_25", 0.5f, 0.0f, 1.0f},
        {"step_value_26", 0.5f, 0.0f, 1.0f}, {"step_value_27", 0.5f, 0.0f, 1.0f},
        {"step_value_28", 0.5f, 0.0f, 1.0f}, {"step_value_29", 0.5f, 0.0f, 1.0f},
        {"step_value_30", 0.5f, 0.0f, 1.0f}, {"step_value_31", 0.5f, 0.0f, 1.0f},
    };

    // --- Inline step gates (internal mode) ---
    vivid::Param<float> step_gate[kMaxSteps] = {
        {"step_gate_0",  1.0f, 0.0f, 1.0f}, {"step_gate_1",  1.0f, 0.0f, 1.0f},
        {"step_gate_2",  1.0f, 0.0f, 1.0f}, {"step_gate_3",  1.0f, 0.0f, 1.0f},
        {"step_gate_4",  1.0f, 0.0f, 1.0f}, {"step_gate_5",  1.0f, 0.0f, 1.0f},
        {"step_gate_6",  1.0f, 0.0f, 1.0f}, {"step_gate_7",  1.0f, 0.0f, 1.0f},
        {"step_gate_8",  1.0f, 0.0f, 1.0f}, {"step_gate_9",  1.0f, 0.0f, 1.0f},
        {"step_gate_10", 1.0f, 0.0f, 1.0f}, {"step_gate_11", 1.0f, 0.0f, 1.0f},
        {"step_gate_12", 1.0f, 0.0f, 1.0f}, {"step_gate_13", 1.0f, 0.0f, 1.0f},
        {"step_gate_14", 1.0f, 0.0f, 1.0f}, {"step_gate_15", 1.0f, 0.0f, 1.0f},
        {"step_gate_16", 1.0f, 0.0f, 1.0f}, {"step_gate_17", 1.0f, 0.0f, 1.0f},
        {"step_gate_18", 1.0f, 0.0f, 1.0f}, {"step_gate_19", 1.0f, 0.0f, 1.0f},
        {"step_gate_20", 1.0f, 0.0f, 1.0f}, {"step_gate_21", 1.0f, 0.0f, 1.0f},
        {"step_gate_22", 1.0f, 0.0f, 1.0f}, {"step_gate_23", 1.0f, 0.0f, 1.0f},
        {"step_gate_24", 1.0f, 0.0f, 1.0f}, {"step_gate_25", 1.0f, 0.0f, 1.0f},
        {"step_gate_26", 1.0f, 0.0f, 1.0f}, {"step_gate_27", 1.0f, 0.0f, 1.0f},
        {"step_gate_28", 1.0f, 0.0f, 1.0f}, {"step_gate_29", 1.0f, 0.0f, 1.0f},
        {"step_gate_30", 1.0f, 0.0f, 1.0f}, {"step_gate_31", 1.0f, 0.0f, 1.0f},
    };

    // --- Clock and output shaping ---
    vivid::Param<int>   rate_mode      {"rate_mode",      vivid::kRateModeExternal, vivid::rate_mode_labels()};
    vivid::Param<float> frequency      {"frequency",      1.0f, 0.01f, 20.0f};
    vivid::Param<int>   sync_division  {"sync_division",  2, vivid::metronome_division_labels()};
    vivid::Param<float> glide          {"glide",          0.0f, 0.0f, 1.0f};
    vivid::Param<float> amplitude      {"amplitude",      1.0f, 0.0f, 10000.0f};
    vivid::Param<float> offset         {"offset",         0.0f, -20000.0f, 20000.0f};
    vivid::Param<int>   polarity       {"polarity",       0, {"bipolar", "unipolar"}};
    vivid::Param<int>   midi_channel   {"midi_channel",   1, 1, 16};

    // --- Editor window (phase-4 platform; follow-up to DrumSequencer) ---
    static VividEditorMetadata editor_metadata();
    void draw_editor(VividEditorContext* ctx);

    // Editor cursor + selection + clipboard. Cursor identifies a single
    // (row, step) cell; selection is the rectangle (anchor, cursor) across
    // the 2-row grid (row 0 = value, row 1 = gate). Clipboard persists
    // across editor close/reopen because it lives on the core instance.
    int editor_cursor_row_  = 0;
    int editor_cursor_step_ = 0;
    vivid::ui::GridState grid_state_{};
    vivid::sequencer_editor::Selection          editor_selection_{};
    vivid::sequencer_editor::SelectionClipboard selection_clipboard_{};

    SequencerCore() {
        vivid::description(source, "Internal uses the built-in step grid; external reads values from input ports");

        vivid::semantic_tag(steps, "count");
        vivid::semantic_shape(steps, "int");
        vivid::semantic_intent(steps, "sequence_length");
        vivid::description(steps, "Number of active steps in the sequence (1–32)");

        vivid::semantic_tag(frequency, "frequency_hz");
        vivid::semantic_shape(frequency, "scalar");
        vivid::semantic_unit(frequency, "Hz");
        vivid::description(frequency, "Cycle rate in Hz (free mode) or beat multiplier (external sync mode)");
        vivid::visible_when_ne(frequency, rate_mode, vivid::kRateModeMetronome);

        vivid::description(rate_mode, "Free runs internally, follows an external beat_phase input, or locks to the graph metronome");

        vivid::description(sync_division, "Musical note length used when rate_mode is metronome");
        vivid::visible_when_eq(sync_division, rate_mode, vivid::kRateModeMetronome);

        vivid::semantic_tag(glide, "probability_01");
        vivid::semantic_shape(glide, "scalar");
        vivid::description(glide, "Smoothing between step values, 0 = instant, 1 = full portamento");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::semantic_intent(amplitude, "env_amount");
        vivid::description(amplitude, "Scales the output value");

        vivid::semantic_tag(offset, "amplitude_linear");
        vivid::semantic_shape(offset, "scalar");
        vivid::semantic_intent(offset, "dc_offset");
        vivid::description(offset, "DC offset added to the output after amplitude scaling");

        vivid::description(polarity, "Output range: bipolar (-1 to 1) or unipolar (0 to 1)");
        vivid::description(midi_channel, "MIDI output channel (1–16)");
        vivid::description(step_value[0], "Value for step 1");
        vivid::description(step_gate[0], "Gate length for step 1, 0 = silent, 1 = full step");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::display_hint(frequency, VIVID_DISPLAY_KNOB);
        vivid::display_hint(glide, VIVID_DISPLAY_KNOB);
        vivid::layout_row(frequency, 2, 0);
        vivid::layout_row(glide, 2, 1);

        // Source toggle
        out.push_back(&source);

        // Inspector retirement (mirrors DrumSequencer phase 4): the compact
        // step-seq widget is gone; the dedicated editor owns per-step
        // authoring. `steps` stays visible as a plain int scrubber, the 64
        // step_value/step_gate params are hidden so the inspector isn't a
        // wall of knobs. `source` remains the only conditional surface.
        vivid::visible_when_eq(steps, source, 0);
        out.push_back(&steps);
        for (int i = 0; i < kMaxSteps; ++i) {
            vivid::display_hint(step_value[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&step_value[i]);
        }
        for (int i = 0; i < kMaxSteps; ++i) {
            vivid::display_hint(step_gate[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&step_gate[i]);
        }

        // Normal params
        out.push_back(&rate_mode);
        out.push_back(&frequency);
        out.push_back(&sync_division);
        out.push_back(&glide);
        out.push_back(&amplitude);
        out.push_back(&offset);
        out.push_back(&polarity);
        out.push_back(&midi_channel);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Scalar inputs (keep existing order for backward compat)
        out.push_back({"beat_phase", VIVID_PORT_SCALAR,     VIVID_PORT_INPUT});   // 0
        out.push_back({"reset",      VIVID_PORT_SCALAR,     VIVID_PORT_INPUT});   // 1
        out.push_back({"gate",       VIVID_PORT_SCALAR,     VIVID_PORT_INPUT});   // 2
        // Lane-array inputs (external mode)
        out.push_back({"values",     VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // 0
        out.push_back({"probs",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // 1
        out.push_back({"ratchets",   VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // 2
        // Scalar outputs
        out.push_back({"value",      VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // 0
        out.push_back({"step",       VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // 1
        out.push_back({"trigger",    VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // 2
        // Custom outputs
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));
    }

    void compute(const float* input_values, double delta_time,
                 const VividLaneView* in_lanes, float* output_values,
                 void** custom_outputs, uint32_t custom_output_count,
                 const vivid::MetronomeTransport& metronome) {
        float dt = static_cast<float>(delta_time);
        float beat_phase_in = input_values[0];
        float reset_in      = input_values[1];
        bool  use_external  = source.int_value() == 1;

        // Read lane-array inputs (external mode)
        const float* val_data = nullptr;
        uint32_t val_len = 0;
        const float* prob_data = nullptr;
        uint32_t prob_len = 0;
        const float* ratch_data = nullptr;
        uint32_t ratch_len = 0;
        if (use_external && in_lanes) {
            val_len   = in_lanes[0].length;
            val_data  = in_lanes[0].data;
            prob_len  = in_lanes[1].length;
            prob_data = in_lanes[1].data;
            ratch_len = in_lanes[2].length;
            ratch_data = in_lanes[2].data;
        }

        // Phase computation
        int mode = rate_mode.int_value();
        float freq = frequency.value;
        double phase;
        if (mode == vivid::kRateModeMetronome) {
            // Each step lasts one sync_division; full cycle = num_steps * division_beats
            int ns_metro = std::clamp(steps.int_value(), 1, kMaxSteps);
            double step_beats = static_cast<double>(vivid::sync_cycle_beats(sync_division.int_value()));
            double cycle_beats = static_cast<double>(ns_metro) * step_beats;
            phase = (cycle_beats > 0.0) ? std::fmod(metronome.beats_elapsed / cycle_beats, 1.0) : 0.0;
        } else if (mode == vivid::kRateModeExternal) {
            phase = std::fmod(static_cast<double>(beat_phase_in) * static_cast<double>(freq), 1.0);
            if (phase < 0.0) phase += 1.0;
        } else {
            // Free mode
            free_phase_ += static_cast<double>(dt) * static_cast<double>(freq);
            free_phase_ -= std::floor(free_phase_);
            phase = free_phase_;
        }

        // Rising-edge reset
        bool reset = reset_in > 0.5f;
        if (reset && !prev_reset_)
            phase_offset_ = phase;
        prev_reset_ = reset;

        float adj_phase = static_cast<float>(std::fmod(phase - phase_offset_ + 1.0, 1.0));

        // Step count
        int ns = std::clamp(steps.int_value(), 1, kMaxSteps);
        if (use_external && val_len > 0)
            ns = std::min(ns, static_cast<int>(val_len));

        float scaled = adj_phase * static_cast<float>(ns);
        int step = static_cast<int>(scaled);
        step = std::clamp(step, 0, ns - 1);

        // Step change detection
        bool step_changed = (step != prev_step_);
        if (step_changed) {
            // Probability
            float prob = 1.0f;
            if (prob_data && step < static_cast<int>(prob_len))
                prob = std::clamp(prob_data[step], 0.0f, 1.0f);
            step_active_ = (prob >= 1.0f) || (next_random() < prob);

            // Ratchets
            int raw_ratchet = 1;
            if (ratch_data && step < static_cast<int>(ratch_len))
                raw_ratchet = static_cast<int>(std::lround(ratch_data[step]));
            current_ratchet_ = std::clamp(raw_ratchet, 1, 8);
            prev_ratchet_index_ = -1;
            prev_step_ = step;
        }

        // Value
        float value = 0.0f;
        if (step_active_) {
            if (use_external && val_data && step < static_cast<int>(val_len)) {
                value = val_data[step];
            } else {
                value = step_value[step].value;
            }
        }

        // Gate (internal mode only)
        float frac_in_step = std::fmod(scaled, 1.0f);
        if (!use_external) {
            float gate_len = step_gate[step].value;
            if (frac_in_step > gate_len)
                value = 0.0f;
        }

        // Ratchet sub-step trigger
        float step_phase = std::clamp(frac_in_step, 0.0f, 0.999999f);
        int ratchet_index = static_cast<int>(step_phase * static_cast<float>(current_ratchet_));
        ratchet_index = std::clamp(ratchet_index, 0, current_ratchet_ - 1);
        bool ratchet_trigger = (ratchet_index != prev_ratchet_index_);
        if (ratchet_trigger) prev_ratchet_index_ = ratchet_index;

        bool trigger = step_active_ && ratchet_trigger;

        // Glide
        float glide_val = glide.value;
        if (glide_val < 0.001f) {
            current_value_ = value;
        } else {
            float target = value;
            float slew = 1.0f - glide_val * glide_val * glide_val;
            current_value_ += (target - current_value_) * slew;
        }

        // Polarity, amplitude, offset
        float out_val = current_value_;
        if (polarity.int_value() == 0)
            out_val = out_val * 2.0f - 1.0f;
        out_val = out_val * amplitude.value + offset.value;

        output_values[0] = out_val;
        output_values[1] = static_cast<float>(step);
        output_values[2] = trigger ? 1.0f : 0.0f;

        // Native note output. Each trigger releases the prior note (if any)
        // and allocates a fresh id for the new one — same-pitch retriggers
        // get distinct slots downstream.
        notes_buf_.count = 0;
        if (trigger) {
            uint8_t note = static_cast<uint8_t>(std::clamp(static_cast<int>(out_val), 0, 127));
            if (current_note_id_ != 0)
                vivid_sequencers::note_off(notes_buf_, current_note_id_);
            uint64_t id = vivid_sequencers::next_note_id();
            vivid_sequencers::note_on(notes_buf_, note, 100.0f / 127.0f, id);
            current_note_id_ = id;
        }
        if (custom_outputs && custom_output_count > 0)
            custom_outputs[0] = &notes_buf_;
    }

protected:
    float next_random() {
        rng_state_ ^= (rng_state_ << 13);
        rng_state_ ^= (rng_state_ >> 17);
        rng_state_ ^= (rng_state_ << 5);
        constexpr float kInvMax = 1.0f / 4294967295.0f;
        return static_cast<float>(rng_state_) * kInvMax;
    }

    double free_phase_       = 0.0;
    float  phase_offset_     = 0.0f;
    int    prev_step_        = -1;
    bool   prev_reset_       = false;
    bool   step_active_      = true;
    int    current_ratchet_  = 1;
    int    prev_ratchet_index_ = -1;
    float  current_value_    = 0.5f;
    uint32_t rng_state_      = 0xA5C31E59u;
    uint64_t current_note_id_ = 0;
    VividNoteBuffer notes_buf_ = {};
};
