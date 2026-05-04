#include "sequencer_core.h"
#include "control/audio_scalar_utils.h"

struct Sequencer : SequencerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "Sequencer";

    void process_audio(const VividAudioContext* ctx) override {
        float local_in[3] = {
            vivid::audio_scalar_block_start(ctx, 0),  // beat_phase
            vivid::audio_scalar_block_start(ctx, 1),  // reset
            vivid::audio_scalar_block_start(ctx, 2),  // gate
        };
        float local_out[3] = {};
        compute(local_in, ctx->delta_time,
                ctx->input_lanes, local_out,
                ctx->custom_outputs, ctx->custom_output_count,
                vivid::metronome_transport(ctx));
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_DEFINE_OP(Sequencer) {
}

VIVID_REGISTER(Sequencer)
VIVID_EDITOR(Sequencer)
