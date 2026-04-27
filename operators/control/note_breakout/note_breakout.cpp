#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "operator_api/voice_table.h"
#include "voice_breakouts.h"

/**
 * @brief Lightweight per-voice breakout for native note streams.
 *
 * Consumes a note stream (notes_in) and exposes the standardized advanced
 * control lanes sorted by note_id ascending: the Phase 2 four (voice_ids,
 * voice_gates, voice_velocities, voice_freqs) plus the Phase 4 expression
 * lanes (voice_pitch_bend, voice_pressure, voice_timbre). Use NoteBreakout
 * when one note stream needs to drive multiple downstream operators that
 * share polyphonic control state (per-voice envelopes, polyphonic
 * key-tracking, explicit per-voice mixing, expression-driven filter /
 * amplitude / wavetable position modulation) without instantiating a synth
 * solely for the breakout lanes.
 *
 * This operator carries no oscillator state, no envelope state, and no
 * audio render path — by design it's the cheapest way to fan note-stream
 * control data into multiple consumers. It reflects the source note
 * stream's current held-note state, not any downstream synth's private
 * stealing/release policy. When the consumer IS a synth, read its `voice_*`
 * outputs directly instead of going through NoteBreakout.
 *
 * Per-note expression events (PITCH_BEND, PRESSURE, TIMBRE) on the input
 * stream mutate the matching voice slot. voice_freqs folds pitch_bend into
 * Hz; voice_pitch_bend exposes the raw semitone offset; voice_pressure /
 * voice_timbre carry slot.pressure / slot.timbre as 0..1 for downstream
 * filter / amplitude / wavetable bindings.
 *
 * @input notes_in Native note stream (VividNoteBuffer).
 * @output notes_out Pass-through of notes_in for fanning a single note stream
 *         to multiple downstream synths (e.g., inside subgraph modules where
 *         the bind expression is single-target). The output buffer is a copy
 *         of the input buffer for the current block.
 * @output voice_ids Per-voice note_id, sorted ascending.
 * @output voice_gates 1.0 while the source note is held. Release tails are
 *         preserved by downstream consumers such as EnvelopeAu using lane_ids.
 * @output voice_velocities Per-voice velocity, 0..1.
 * @output voice_freqs Per-voice frequency in Hz (includes pitch_bend_semis).
 * @output voice_pitch_bend Per-voice pitch bend in semitones (raw, ±48 typical).
 * @output voice_pressure Per-voice pressure, 0..1.
 * @output voice_timbre Per-voice timbre, 0..1.
 * @recipe note_source/notes_out -> NoteBreakout/notes_in
 * @recipe NoteBreakout/notes_out -> Synth/notes_in   (fanout to multiple synths)
 * @recipe NoteBreakout/voice_gates -> EnvelopeAu/gate
 * @recipe NoteBreakout/voice_ids -> EnvelopeAu/lane_ids
 * @recipe NoteBreakout/voice_freqs -> Filter/frequencies
 * @recipe NoteBreakout/voice_pressure -> Filter/cutoff_mod   (expression bindings)
 * @recipe NoteBreakout/voice_timbre -> WavetableLayer/position_mod_audio
 * @family note_source
 * @best_used_with EnvelopeAu, Filter, DualFilter, VoiceMixer, WavetableLayer
 */
struct NoteBreakout : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "NoteBreakout";
    static constexpr bool kTimeDependent = false;
    static constexpr int kMaxVoices = 16;

    vivid::VoiceTable<kMaxVoices> alloc_;
    uint64_t frame_counter_ = 0;
    VividNoteBuffer passthrough_buf_{};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        (void)out;
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));

        // Pass-through note stream — same buffer the input received, exposed
        // so a single source can fan to multiple downstream synths through one
        // module port (the subgraph-module bind expression is single-target).
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));

        // Lane-array breakouts. Order must match
        // vivid_sequencers::VoiceBreakoutLane:
        //   voice_ids / voice_gates / voice_velocities / voice_freqs (Phase 2)
        //   voice_pitch_bend / voice_pressure / voice_timbre (Phase 4 expression)
        out.push_back({"voice_ids",         VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_gates",       VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_velocities",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_freqs",       VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        // Phase 4: per-note expression breakouts. voice_pitch_bend exposes
        // the raw pitch_bend_semis (voice_freqs already folds bend into Hz);
        // voice_pressure / voice_timbre carry slot.pressure / slot.timbre as
        // 0..1 for downstream filter cutoff / amplitude / wavetable position
        // bindings. Tracker (Phase 4) and MidiInput emit the source events.
        out.push_back({"voice_pitch_bend",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_pressure",    VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_timbre",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
    }

    void process_audio(const VividAudioContext* ctx) override {
        // Drive the allocator from notes_in. Per-note expression events
        // mutate slot fields (pitch_bend_semis, pressure, timbre) inside
        // process_note_buffer; voice_freqs picks up pitch bend on read.
        const VividNoteBuffer* notes = nullptr;
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0])
            notes = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);

        // Mirror notes_in into the pass-through output buffer so notes_out
        // re-emits the input stream verbatim (used by modules to fan a
        // single note source to multiple internal synths).
        if (notes) passthrough_buf_ = *notes;
        else       passthrough_buf_.count = 0;
        if (ctx->custom_outputs && ctx->custom_output_count > 0)
            ctx->custom_outputs[0] = &passthrough_buf_;

        alloc_.process_note_buffer(
            notes, frame_counter_,
            [](int, int, float, uint32_t, uint64_t) {},
            [this](int slot, int, uint64_t) {
                // NoteBreakout reflects current held-note state only; it does
                // not synthesize release tails. Downstream consumers such as
                // EnvelopeAu preserve release by remembering lane_ids.
                alloc_.slots[slot].active = false;
            },
            [](int, VividNoteEventType, float) {});

        // Emit all 7 breakout lanes (Phase 2 + Phase 4).
        if (ctx->output_lanes) {
            VividLaneOutput lanes[vivid_sequencers::kVoiceBreakoutLaneCount] = {
                ctx->output_lanes[0],  // voice_ids
                ctx->output_lanes[1],  // voice_gates
                ctx->output_lanes[2],  // voice_velocities
                ctx->output_lanes[3],  // voice_freqs
                ctx->output_lanes[4],  // voice_pitch_bend
                ctx->output_lanes[5],  // voice_pressure
                ctx->output_lanes[6],  // voice_timbre
            };
            vivid_sequencers::emit_voice_breakouts(alloc_, lanes);
        }

        frame_counter_ += ctx->buffer_size;
    }
};

VIVID_REGISTER(NoteBreakout)
