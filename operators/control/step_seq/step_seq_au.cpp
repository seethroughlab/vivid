// Audio-rate StepSeq variant.
#include "step_seq.h"
#include "control/audio_scalar_utils.h"

struct StepSeq_AU : StepSeq, vivid::AudioProcessable {
    static constexpr const char* kName = "StepSeqAu";

    void process_audio(const VividAudioContext* ctx) override {
        float local_in[2] = {
            vivid::audio_scalar_block_start(ctx, 0),  // gate
            vivid::audio_scalar_block_start(ctx, 1),  // beat_phase
        };
        float local_out[2] = {};
        compute(local_in, ctx->delta_time, local_out);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 2; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(StepSeq_AU)
VIVID_INSPECTOR(StepSeq_AU)
