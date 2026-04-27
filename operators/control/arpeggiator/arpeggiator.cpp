#include "arpeggiator_core.h"
#include "control/audio_scalar_utils.h"

struct Arpeggiator : ArpeggiatorCore, vivid::AudioProcessable {
    static constexpr const char* kName = "Arpeggiator";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[4] = {};
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), vivid::audio_scalar_block_start(ctx, 0), vivid::metronome_transport(ctx));
        const VividNoteBuffer* notes_in = nullptr;
        if (ctx->custom_inputs && ctx->custom_input_count > 0)
            notes_in = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);
        compute(beat_phase, ctx->param_values, notes_in,
                local_out,
                ctx->custom_outputs, ctx->custom_output_count);
        // SCALAR outputs (note/vel/gate/step) are now ports [0..3] — the
        // legacy LANE_ARRAY note/vel/gate outputs were removed in PR3.
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 4; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(Arpeggiator)
VIVID_THUMBNAIL(Arpeggiator)
VIVID_EDITOR(Arpeggiator)
