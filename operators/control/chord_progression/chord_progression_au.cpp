#include "chord_progression_core.h"

struct ChordProgressionAu : ChordProgressionCore, vivid::AudioProcessable {
    static constexpr const char* kName = "chord_progression_au";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        compute(0.0f, ctx->param_values, ctx->output_lanes,
                local_out, ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(ChordProgressionAu)
VIVID_THUMBNAIL(ChordProgressionAu)
