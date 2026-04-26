#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "operator_api/voice_allocator.h"
#include "voice_breakouts.h"

/**
 * @brief Lightweight per-voice breakout for native note streams.
 *
 * Consumes a note stream (notes_in) and exposes the four standardized
 * advanced control lanes — voice_ids, voice_gates, voice_velocities,
 * voice_freqs — sorted by note_id ascending. Use NoteBreakout when one
 * note stream needs to drive multiple downstream operators that share
 * polyphonic control state (per-voice envelopes, polyphonic key-tracking,
 * explicit per-voice mixing) without instantiating a synth solely for the
 * breakout lanes.
 *
 * This operator carries no oscillator state, no envelope state, and no
 * audio render path — by design it's the cheapest way to fan note-stream
 * control data into multiple consumers. When the consumer IS a synth,
 * read its `voice_*` outputs directly instead of going through NoteBreakout.
 *
 * Per-note expression events (PITCH_BEND, PRESSURE, TIMBRE) on the input
 * stream mutate the matching voice slot — voice_freqs reflects pitch bend.
 *
 * @input notes_in Native note stream (VividNoteBuffer).
 * @output voice_ids Per-voice note_id, sorted ascending.
 * @output voice_gates 1.0 if held, 0.0 if released-tail.
 * @output voice_velocities Per-voice velocity, 0..1.
 * @output voice_freqs Per-voice frequency in Hz (includes pitch_bend_semis).
 * @recipe note_source/notes_out -> NoteBreakout/notes_in
 * @recipe NoteBreakout/voice_gates -> EnvelopeAu/gate
 * @recipe NoteBreakout/voice_ids -> EnvelopeAu/lane_ids
 * @recipe NoteBreakout/voice_freqs -> Filter/frequencies
 * @family note_source
 * @best_used_with EnvelopeAu, Filter, DualFilter, VoiceMixer
 */
struct NoteBreakout : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "NoteBreakout";
    static constexpr bool kTimeDependent = false;
    static constexpr int kMaxVoices = 16;

    vivid::VoiceAllocator<kMaxVoices> alloc_;
    uint64_t frame_counter_ = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        (void)out;
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));

        // Lane-array breakouts. Order must match
        // vivid_sequencers::VoiceBreakoutLane (ids/gates/velocities/freqs).
        out.push_back({"voice_ids",        VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_gates",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_velocities", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_freqs",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
    }

    void process_audio(const VividAudioContext* ctx) override {
        // Drive the allocator from notes_in. Per-note expression events
        // mutate slot fields (pitch_bend_semis, pressure, timbre) inside
        // process_note_buffer; voice_freqs picks up pitch bend on read.
        const VividNoteBuffer* notes = nullptr;
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0])
            notes = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);

        alloc_.process_note_buffer(
            notes, frame_counter_,
            [](int, int, float, uint32_t, uint64_t) {},
            [this](int slot, int, uint64_t) {
                // Synth-style envelopes would gate-off here. NoteBreakout has
                // no envelope, so the slot stays "active" until the consumer
                // is done. Free the slot immediately on note-off — there's
                // no release tail on this surface.
                alloc_.slots[slot].active = false;
            },
            [](int, VividNoteEventType, float) {});

        // Emit the four breakout lanes.
        if (ctx->output_lanes) {
            VividLaneOutput lanes[vivid_sequencers::kVoiceBreakoutLaneCount] = {
                ctx->output_lanes[0],  // voice_ids
                ctx->output_lanes[1],  // voice_gates
                ctx->output_lanes[2],  // voice_velocities
                ctx->output_lanes[3],  // voice_freqs
            };
            vivid_sequencers::emit_voice_breakouts(alloc_, lanes);
        }

        frame_counter_ += ctx->buffer_size;
    }
};

VIVID_REGISTER(NoteBreakout)
