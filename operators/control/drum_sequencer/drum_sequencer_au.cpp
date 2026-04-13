#include "drum_sequencer_core.h"
#include "control/audio_scalar_utils.h"

struct DrumSequencerAu : DrumSequencerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "DrumSequencerAu";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[1] = {};
        float beat_phase = vivid::resolve_bar_phase(
            clock_source.int_value(), vivid::audio_scalar_block_start(ctx, 0), vivid::metronome_transport(ctx));
        float reset = vivid::audio_scalar_block_start(ctx, 1);
        compute(beat_phase, reset, ctx->param_values,
                local_out, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            ctx->output_buffers[0][i] = local_out[0];
    }
};

VIVID_REGISTER(DrumSequencerAu)
VIVID_THUMBNAIL(DrumSequencerAu)
VIVID_INSPECTOR(DrumSequencerAu)
