#include "sequencer_core.h"
#include "control/audio_scalar_utils.h"

struct SequencerAu : SequencerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "SequencerAu";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), vivid::audio_scalar_block_start(ctx, 0), vivid::metronome_transport(ctx));
        float reset = vivid::audio_scalar_block_start(ctx, 1);
        compute(beat_phase, reset,
                ctx->input_lanes, local_out,
                ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(SequencerAu)
