#include "drum_sequencer_core.h"

struct DrumSequencerAu : DrumSequencerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "drum_sequencer_au";

    void process_audio(const VividAudioContext* ctx) override {
        compute(ctx->input_float_values[0], ctx->input_float_values[1], ctx->param_values,
                ctx->output_float_values, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(DrumSequencerAu)
VIVID_THUMBNAIL(DrumSequencerAu)
VIVID_INSPECTOR(DrumSequencerAu)
