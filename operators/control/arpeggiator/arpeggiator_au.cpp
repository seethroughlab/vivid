#include "arpeggiator_core.h"
#include "control/audio_scalar_utils.h"

struct ArpeggiatorAu : ArpeggiatorCore, vivid::AudioProcessable {
    static constexpr const char* kName = "arpeggiator_au";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[4] = {};
        float beat_phase = vivid::audio_scalar_block_start(ctx, 0);
        compute(beat_phase, ctx->param_values, ctx->input_lanes,
                local_out, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 4; ++j)
                ctx->output_buffers[3 + j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(ArpeggiatorAu)
VIVID_THUMBNAIL(ArpeggiatorAu)
VIVID_INSPECTOR(ArpeggiatorAu)
