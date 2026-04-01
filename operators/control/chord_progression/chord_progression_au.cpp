#include "chord_progression_core.h"

struct ChordProgressionAu : ChordProgressionCore, vivid::AudioProcessable {
    static constexpr const char* kName = "chord_progression_au";

    void process_audio(const VividAudioContext* ctx) override {
        compute(ctx->input_float_values[0], ctx->param_values, ctx->output_lanes,
                ctx->output_float_values, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(ChordProgressionAu)
VIVID_THUMBNAIL(ChordProgressionAu)
