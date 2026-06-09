#pragma once
#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "shared/timing/clock_block.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_ui.h"
#include "operator_api/editor_keys.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "note_helpers.h"
#include "note_id_counter.h"
#include "arpeggiator_patterns.h"
#include "operator_api/editor_ui/selection.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "operator_api/thumbnail.h"

/**
 * @brief Arpeggiation engine with 10 modes, swing, and per-step modulation.
 *
 * Consumes a native note stream on `notes_in` (NOTE_ON / NOTE_OFF events)
 * to maintain a held-note set, then walks that set in configurable patterns
 * (up, down, up-down, random, converge, diverge, etc.) and emits a fresh
 * NOTE_ON / NOTE_OFF pair per arp step on `notes_out`. Each step gets a new
 * `note_id` so downstream synths allocate distinct voices for same-pitch
 * retriggers.
 *
 * Per-note expression on the input (PRESSURE, TIMBRE) is sampled at step
 * fire and emitted as initial expression on the new step's note_id —
 * snapshot-and-bake. Live expression updates from the input do NOT
 * propagate; the arp is a step sequencer, not an expression bus. Route the
 * source through `NoteBreakout` if you need live expression on each voice.
 * Pitch_bend is not forwarded (the arp's emitted pitch is the held source's
 * MIDI pitch + per-step transpose).
 *
 * @tip Enable latch to keep playing after releasing keys. The latched set
 *      is cleared and replaced on the next NOTE_ON wave.
 * @param mode Arpeggiation pattern: Up, Down, UpDown, Random, Converge, etc.
 * @param octaves Range of octave transposition applied to the pattern.
 * @see Sequencer, ChordProgression, MidiInput, NoteBreakout
 */
struct ArpeggiatorCore : vivid::OperatorBase {
    static constexpr bool kTimeDependent = true;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE;

    // --- Core parameters ---
    vivid::Param<int>   mode        {"mode",        0, {"Up","Down","UpDown","DownUp","Random","Order","Converge","Diverge","RandomNoRepeat","OrderDown"}};
    vivid::Param<int>   octaves     {"octaves",     1, 1, 4};
    vivid::Param<int>   sync_division        {"sync_division",        3, vivid::metronome_division_labels()};
    vivid::Param<float> gate_length {"gate_length", 0.8f, 0.01f, 1.0f};
    vivid::Param<float> swing       {"swing",       0.0f, 0.0f, 1.0f};
    vivid::Param<bool>  latch       {"latch",       false};
    vivid::Param<int>   mod_steps   {"mod_steps",   8, 1, 16};
    vivid::Param<int>   clock_mode{"clock_mode", vivid::kClockModeSyncedMetronome, vivid::clock_mode_synced_labels()};

    // --- Per-step velocity modifiers (legacy 0..7 + new 8..15) ---
    vivid::Param<float> vel_0 {"vel_0", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_1 {"vel_1", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_2 {"vel_2", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_3 {"vel_3", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_4 {"vel_4", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_5 {"vel_5", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_6 {"vel_6", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_7 {"vel_7", 1.0f, 0.0f, 1.0f};

    // --- Per-step transpose modifiers (legacy 0..7 + new 8..15) ---
    vivid::Param<int> tr_0 {"tr_0", 0, -24, 24};
    vivid::Param<int> tr_1 {"tr_1", 0, -24, 24};
    vivid::Param<int> tr_2 {"tr_2", 0, -24, 24};
    vivid::Param<int> tr_3 {"tr_3", 0, -24, 24};
    vivid::Param<int> tr_4 {"tr_4", 0, -24, 24};
    vivid::Param<int> tr_5 {"tr_5", 0, -24, 24};
    vivid::Param<int> tr_6 {"tr_6", 0, -24, 24};
    vivid::Param<int> tr_7 {"tr_7", 0, -24, 24};
    vivid::Param<int> midi_channel {"midi_channel", 1, 1, 16};

    // --- New params: follow-up to keep existing param indices stable ---
    //
    // `vel_8..15`, `tr_8..15`, `note_override_0..15`, and `gt_0..15` all
    // sit PAST `midi_channel` in descriptor order. Graphs saved before
    // this expansion load with every new param at its default, which
    // preserves today's behaviour exactly.

    // vel_8..vel_15 (8 entries, float, default 1.0)
    vivid::Param<float> vel_8  {"vel_8",  1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_9  {"vel_9",  1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_10 {"vel_10", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_11 {"vel_11", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_12 {"vel_12", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_13 {"vel_13", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_14 {"vel_14", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> vel_15 {"vel_15", 1.0f, 0.0f, 1.0f};

    // tr_8..tr_15 (8 entries, int, default 0)
    vivid::Param<int> tr_8  {"tr_8",  0, -24, 24};
    vivid::Param<int> tr_9  {"tr_9",  0, -24, 24};
    vivid::Param<int> tr_10 {"tr_10", 0, -24, 24};
    vivid::Param<int> tr_11 {"tr_11", 0, -24, 24};
    vivid::Param<int> tr_12 {"tr_12", 0, -24, 24};
    vivid::Param<int> tr_13 {"tr_13", 0, -24, 24};
    vivid::Param<int> tr_14 {"tr_14", 0, -24, 24};
    vivid::Param<int> tr_15 {"tr_15", 0, -24, 24};

// 16-entry per-step arrays use a macro to stay compact. Each row
// declares a `Param<T>` with the canonical name "prefix_N" so saved
// graphs serialize/deserialize on the same keys.
#define VIVID_ARP_ROW_I(prefix, def, lo, hi) \
    {prefix "0",  def, lo, hi}, {prefix "1",  def, lo, hi}, \
    {prefix "2",  def, lo, hi}, {prefix "3",  def, lo, hi}, \
    {prefix "4",  def, lo, hi}, {prefix "5",  def, lo, hi}, \
    {prefix "6",  def, lo, hi}, {prefix "7",  def, lo, hi}, \
    {prefix "8",  def, lo, hi}, {prefix "9",  def, lo, hi}, \
    {prefix "10", def, lo, hi}, {prefix "11", def, lo, hi}, \
    {prefix "12", def, lo, hi}, {prefix "13", def, lo, hi}, \
    {prefix "14", def, lo, hi}, {prefix "15", def, lo, hi}

#define VIVID_ARP_ROW_F(prefix, def) \
    {prefix "0",  def, 0.0f, 1.0f}, {prefix "1",  def, 0.0f, 1.0f}, \
    {prefix "2",  def, 0.0f, 1.0f}, {prefix "3",  def, 0.0f, 1.0f}, \
    {prefix "4",  def, 0.0f, 1.0f}, {prefix "5",  def, 0.0f, 1.0f}, \
    {prefix "6",  def, 0.0f, 1.0f}, {prefix "7",  def, 0.0f, 1.0f}, \
    {prefix "8",  def, 0.0f, 1.0f}, {prefix "9",  def, 0.0f, 1.0f}, \
    {prefix "10", def, 0.0f, 1.0f}, {prefix "11", def, 0.0f, 1.0f}, \
    {prefix "12", def, 0.0f, 1.0f}, {prefix "13", def, 0.0f, 1.0f}, \
    {prefix "14", def, 0.0f, 1.0f}, {prefix "15", def, 0.0f, 1.0f}

    // note_override_N — 0 = follow global `mode` (sentinel), 1..8 =
    // force pool index N-1, 9 = mute. Default 0 preserves every
    // existing graph's behaviour.
    std::array<vivid::Param<int>, 16> note_override_ = {{
        VIVID_ARP_ROW_I("note_override_", 0, 0, 9),
    }};

    // gt_N — per-step gate-length multiplier. Default 1.0 (no effect).
    std::array<vivid::Param<float>, 16> gt_ = {{
        VIVID_ARP_ROW_F("gt_", 1.0f),
    }};

#undef VIVID_ARP_ROW_I
#undef VIVID_ARP_ROW_F

    ArpeggiatorCore() {
        vivid::description(mode, "Arpeggiation pattern: Up, Down, UpDown, Random, Converge, etc");
        vivid::description(octaves, "Number of octaves to span above the input notes");
        vivid::description(sync_division, "Clock subdivision for arp step timing");
        vivid::wire_clock_visibility_synced(sync_division, clock_mode);
        vivid::description(gate_length, "Fraction of each step during which the note sounds");
        vivid::description(swing, "Timing offset between even and odd steps, 0 = straight");
        vivid::description(latch, "Keep playing the pattern after all input gates go low");
        vivid::description(mod_steps, "Number of active steps in the velocity/transpose modulation cycle");
        vivid::description(clock_mode, "Choose whether beat timing comes from the external beat_phase input or the graph metronome");
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
    //  2       = sync_division
    //  3       = gate_length
    //  4       = swing
    //  5       = latch
    //  6       = mod_steps
    //  7       = clock_mode
    //  8..15   = vel_0..vel_7
    //  16..23  = tr_0..tr_7
    //  24      = midi_channel

    vivid::Param<float>* vel_params_[16] = {
        &vel_0,&vel_1,&vel_2,&vel_3,&vel_4,&vel_5,&vel_6,&vel_7,
        &vel_8,&vel_9,&vel_10,&vel_11,&vel_12,&vel_13,&vel_14,&vel_15};
    vivid::Param<int>*   tr_params_[16]  = {
        &tr_0,&tr_1,&tr_2,&tr_3,&tr_4,&tr_5,&tr_6,&tr_7,
        &tr_8,&tr_9,&tr_10,&tr_11,&tr_12,&tr_13,&tr_14,&tr_15};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        // Legacy descriptor indices 0..24 stay stable so saved graphs
        // load without migration. New params append past index 24.
        out.push_back(&mode);         // 0
        out.push_back(&octaves);      // 1
        out.push_back(&sync_division);         // 2
        out.push_back(&gate_length);  // 3
        out.push_back(&swing);        // 4
        out.push_back(&latch);        // 5
        out.push_back(&mod_steps);    // 6
        out.push_back(&clock_mode); // 7

        for (int i = 0; i < 8; ++i) {
            display_hint(*vel_params_[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(vel_params_[i]);   // 8..15
        }
        for (int i = 0; i < 8; ++i) {
            display_hint(*tr_params_[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(tr_params_[i]);    // 16..23
        }

        out.push_back(&midi_channel); // 24

        // --- Follow-up params (Cthulhu-inspired v1 expansion) ---
        for (int i = 8; i < 16; ++i) {
            display_hint(*vel_params_[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(vel_params_[i]);   // 25..32
        }
        for (int i = 8; i < 16; ++i) {
            display_hint(*tr_params_[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(tr_params_[i]);    // 33..40
        }
        for (int i = 0; i < 16; ++i) {
            display_hint(note_override_[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&note_override_[i]); // 41..56
        }
        for (int i = 0; i < 16; ++i) {
            display_hint(gt_[i], VIVID_DISPLAY_HIDDEN);
            out.push_back(&gt_[i]);          // 57..72
        }
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Inputs
        out.push_back({"beat_phase", VIVID_PORT_SCALAR,  VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "beat_phase"});   // [0]
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));  // [1]
        // Outputs
        out.push_back({"note", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});  // [0]
        out.push_back({"vel",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});  // [1]
        out.push_back({"gate", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});  // [2]
        out.push_back({"step", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});  // [3]
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));
    }

    void compute(float beat_phase, const float* params,
                 const VividNoteBuffer* notes_in,
                 float* output_values,
                 void** custom_outputs, uint32_t custom_output_count) {
        int m = mode.int_value();
        int oct = octaves.int_value();
        int r = sync_division.int_value();
        float gl = gate_length.value;
        float sw = swing.value;
        bool latch_on = latch.bool_value();
        int msteps = mod_steps.int_value();

        if (m < 0) m = 0; if (m > 9) m = 9;
        if (oct < 1) oct = 1; if (oct > 4) oct = 4;
        if (r < 0) r = 0; if (r > 8) r = 8;
        if (msteps < 1) msteps = 1; if (msteps > 16) msteps = 16;

        // Walk the input note buffer to maintain the held-note set.
        if (notes_in) {
            for (uint32_t i = 0; i < notes_in->count; ++i) {
                const auto& ev = notes_in->events[i];
                if (ev.note_id == 0) continue;
                switch (ev.type) {
                    case VIVID_NOTE_ON: {
                        held_note_on(ev.note_id, ev.note_number, ev.value);
                        break;
                    }
                    case VIVID_NOTE_OFF: {
                        held_note_off(ev.note_id);
                        break;
                    }
                    case VIVID_NOTE_PRESSURE: {
                        if (auto* h = find_held_by_id(ev.note_id))
                            h->pressure = ev.value;
                        break;
                    }
                    case VIVID_NOTE_TIMBRE: {
                        if (auto* h = find_held_by_id(ev.note_id))
                            h->timbre = ev.value;
                        break;
                    }
                    default: break;  // PITCH_BEND ignored
                }
            }
        }

        // Latch buffer = snapshot of held set; retained when held drops to 0.
        bool any_held = (held_count_ > 0);
        if (latch_on) {
            if (any_held && !prev_any_held_) {
                // Fresh NOTE_ON wave after a release — clear latch.
                latch_count_ = 0;
            }
            if (any_held) {
                latch_count_ = held_count_;
                for (int i = 0; i < held_count_; ++i) latch_buffer_[i] = held_buffer_[i];
            }
        }
        prev_any_held_ = any_held;

        // Source set for pool building: latch buffer when latch is on and we
        // have one, otherwise the live held buffer.
        const HeldNote* src = held_buffer_;
        int input_count = held_count_;
        if (latch_on && latch_count_ > 0) {
            src = latch_buffer_;
            input_count = latch_count_;
        }
        if (input_count > 16) input_count = 16;

        float input_notes[16];
        float input_vels[16];
        float input_pressures[16];
        float input_timbres[16];
        for (int i = 0; i < input_count; ++i) {
            input_notes[i]     = static_cast<float>(src[i].note);
            input_vels[i]      = src[i].velocity;
            input_pressures[i] = src[i].pressure;
            input_timbres[i]   = src[i].timbre;
        }

        // Build the arp note pool: input notes expanded across octaves.
        // Track each pool entry's source-note pressure/timbre so the
        // step-fire path can bake them onto the emitted note_id.
        int pool_count = 0;
        float pool_notes[64];  // 16 notes * 4 octaves max
        float pool_vels[64];
        float pool_pressures[64];
        float pool_timbres[64];

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
                pool_notes[pool_count]     = input_notes[idx] + static_cast<float>(o * 12);
                pool_vels[pool_count]      = input_vels[idx];
                pool_pressures[pool_count] = input_pressures[idx];
                pool_timbres[pool_count]   = input_timbres[idx];
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
            step_offset_ = static_cast<int>(std::floor(total_beats / vivid::sync_cycle_beats(r)));
            arp_direction_ = 1;
            last_selected_step_ = -1;
            last_selected_pool_ = -1;
            last_selected_idx_ = 0;
            last_random_idx_ = -1;
        }
        prev_had_notes_ = has_notes;

        // No notes — output silence
        if (!has_notes) {
            write_output(output_values, custom_outputs, custom_output_count,
                         0.0f, 0.0f, 0.0f, 0,
                         /*pressure=*/0.0f, /*timbre=*/0.0f);
            return;
        }

        // Rate multiplier
        float multiplier = (1.0f / vivid::sync_cycle_beats(r));

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

        // Per-step modifier index + param reads
        int mod_idx = raw_step % msteps;

        // vel_N and tr_N sit in two blocks because we kept the legacy
        // indices 8..23 stable: 0..7 live at their old positions, 8..15
        // at the follow-up indices 25..32 / 33..40.
        const int vel_base = (mod_idx < 8) ? 8 : 25;
        const int tr_base  = (mod_idx < 8) ? 16 : 33;
        const int vel_off  = (mod_idx < 8) ? mod_idx : mod_idx - 8;
        const int tr_off   = (mod_idx < 8) ? mod_idx : mod_idx - 8;
        float vel_mod = params[vel_base + vel_off];
        int   tr_mod  = static_cast<int>(params[tr_base + tr_off]);

        // Follow-up lanes: note_override (41..56) and gt (57..72).
        const int note_override = static_cast<int>(params[41 + mod_idx]);
        const float gt_mod = params[57 + mod_idx];

        // Note Override resolves which pool note to play:
        //   0      → follow the global `mode` (sentinel, default).
        //   1..8   → force pool index N-1.
        //   9      → mute step entirely.
        bool mute = (note_override == 9);
        int note_idx = 0;
        if (!mute) {
            if (note_override >= 1 && note_override <= 8) {
                note_idx = std::clamp(note_override - 1, 0, pool_count - 1);
                // Cache invalidate so mode-driven selection re-resolves
                // cleanly next time this step comes back to default.
                last_selected_step_ = -1;
                last_selected_pool_ = -1;
            } else if (raw_step != last_selected_step_ || pool_count != last_selected_pool_) {
                note_idx = get_note_index(m, raw_step, pool_count);
                last_selected_step_ = raw_step;
                last_selected_pool_ = pool_count;
                last_selected_idx_ = note_idx;
            } else {
                note_idx = last_selected_idx_;
            }
        }

        const float effective_gate = gl * std::clamp(gt_mod, 0.0f, 1.0f);
        float out_note = mute ? 0.0f
                              : pool_notes[note_idx] + static_cast<float>(tr_mod);
        float out_vel  = mute ? 0.0f : pool_vels[note_idx] * vel_mod;
        float out_gate = (!mute && step_phase < effective_gate) ? 1.0f : 0.0f;
        const float src_pressure = mute ? 0.0f : pool_pressures[note_idx];
        const float src_timbre   = mute ? 0.0f : pool_timbres[note_idx];

        write_output(output_values, custom_outputs, custom_output_count,
                     out_note, out_vel, out_gate, raw_step,
                     src_pressure, src_timbre);
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

    // Editor window — dedicated VIVID_EDITOR. The legacy custom
    // inspector (interactive vel/tr bars) was retired in favour of the
    // full per-step grid editor; same pattern as DrumSequencer /
    // Sequencer / Tracker adoption.
    static VividEditorMetadata editor_metadata();
    void draw_editor(VividEditorContext* ctx);

    // Editor UI state (persisted across frames; public so tests can
    // arrange and observe, mirroring the other Tier-3 adopters).
    int  editor_cursor_step_  = 0;       // 0..15
    int  editor_cursor_row_   = 0;       // 0=note_override, 1=vel, 2=tr, 3=gate
    vivid::ui::GridState grid_state_{};
    vivid::ui::Selection editor_selection_{};
    // Rectangular clipboard: 4 lanes × 16 steps of floats.
    struct EditorClipboard {
        bool  has_content = false;
        int   rows = 0;
        int   cols = 0;
        float values[4 * 16] = {};
    };
    EditorClipboard editor_clipboard_{};


protected:

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

    // Held-note set (driven by NOTE_ON / NOTE_OFF events on notes_in).
    // Keyed by uint64 note_id so same-pitch overlap from MPE / chord
    // sources allocates distinct entries cleanly.
    static constexpr int kMaxHeld = 16;
    struct HeldNote {
        uint64_t note_id  = 0;
        uint8_t  note     = 0;
        float    velocity = 0.0f;
        float    pressure = 0.0f;
        float    timbre   = 0.0f;
    };
    HeldNote held_buffer_[kMaxHeld] = {};
    int held_count_ = 0;

    // Latch snapshot — retained when held_count_ drops to 0 so the
    // pattern keeps walking the previous note set.
    HeldNote latch_buffer_[kMaxHeld] = {};
    int latch_count_ = 0;
    bool prev_any_held_ = false;

    HeldNote* find_held_by_id(uint64_t note_id) {
        if (note_id == 0) return nullptr;
        for (int i = 0; i < held_count_; ++i)
            if (held_buffer_[i].note_id == note_id) return &held_buffer_[i];
        return nullptr;
    }

    void held_note_on(uint64_t note_id, uint8_t note, float velocity) {
        // Same id repeated by an upstream bug → in-place update.
        if (auto* h = find_held_by_id(note_id)) {
            h->note     = note;
            h->velocity = velocity;
            return;
        }
        if (held_count_ >= kMaxHeld) return;  // capacity cap; drop excess
        auto& h = held_buffer_[held_count_++];
        h.note_id  = note_id;
        h.note     = note;
        h.velocity = velocity;
        h.pressure = 0.0f;
        h.timbre   = 0.0f;
    }

    void held_note_off(uint64_t note_id) {
        for (int i = 0; i < held_count_; ++i) {
            if (held_buffer_[i].note_id == note_id) {
                for (int j = i; j < held_count_ - 1; ++j)
                    held_buffer_[j] = held_buffer_[j + 1];
                --held_count_;
                return;
            }
        }
    }

    // MIDI state
    bool prev_midi_gate_ = false;
    uint64_t current_note_id_ = 0;
    VividNoteBuffer notes_buf_ = {};

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

    void write_output(float* output_values,
                      void** custom_outputs, uint32_t custom_output_count,
                      float note, float vel, float gate, int step,
                      float pressure, float timbre) {
        if (output_values) {
            output_values[0] = note;
            output_values[1] = vel;
            output_values[2] = gate;
            output_values[3] = static_cast<float>(step);
        }

        // Native note output. Each arp step gets a fresh id — same-pitch
        // retriggers always allocate distinct downstream voices.
        notes_buf_.count = 0;
        bool gate_high = (gate > 0.5f);
        if (gate_high && !prev_midi_gate_) {
            if (current_note_id_ != 0) {
                vivid_sequencers::note_off(notes_buf_, current_note_id_);
            }
            uint8_t midi_note = static_cast<uint8_t>(std::clamp(static_cast<int>(note), 0, 127));
            current_note_id_ = vivid_sequencers::next_note_id();
            vivid_sequencers::note_on(notes_buf_, midi_note, vel, current_note_id_);
            // Snapshot-and-bake: forward the source held note's last-known
            // pressure/timbre as initial expression on the new note_id so
            // downstream voices respond from the first sample.
            if (pressure > 0.0f)
                vivid_sequencers::note_pressure(notes_buf_, current_note_id_, pressure);
            if (timbre > 0.0f)
                vivid_sequencers::note_timbre(notes_buf_, current_note_id_, timbre);
        } else if (!gate_high && prev_midi_gate_) {
            if (current_note_id_ != 0) {
                vivid_sequencers::note_off(notes_buf_, current_note_id_);
                current_note_id_ = 0;
            }
        }
        prev_midi_gate_ = gate_high;
        if (custom_outputs && custom_output_count > 0) {
            custom_outputs[0] = &notes_buf_;
        }
    }
};
