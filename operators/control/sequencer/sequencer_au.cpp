#include "sequencer_core.h"

struct SequencerAu : SequencerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "sequencer_au";

    void process_audio(const VividAudioContext* ctx) override {
        compute(ctx->input_float_values[0], ctx->input_float_values[1],
                ctx->input_lanes, ctx->output_float_values,
                ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(SequencerAu)
