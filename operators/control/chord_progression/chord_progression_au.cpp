#include "chord_progression_core.h"
#include "control/audio_scalar_utils.h"

struct ChordProgressionAu : ChordProgressionCore, vivid::AudioProcessable {
    static constexpr const char* kName = "chord_progression_au";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        float beat_phase = vivid::audio_scalar_block_start(ctx, 0);
        compute(beat_phase, ctx->param_values, ctx->output_lanes,
                local_out, ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[3 + j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(ChordProgressionAu)
VIVID_THUMBNAIL(ChordProgressionAu)
