#include "euclidean_core.h"
#include "control/audio_scalar_utils.h"
#include "operator_api/thumbnail.h"

struct EuclideanAu : EuclideanCore, vivid::AudioProcessable {
    static constexpr const char* kName = "EuclideanAu";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        float beat_phase = vivid::audio_scalar_block_start(ctx, 0);
        compute(beat_phase, ctx->param_values,
                ctx->output_lanes, local_out);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(EuclideanAu)
VIVID_THUMBNAIL(EuclideanAu)
