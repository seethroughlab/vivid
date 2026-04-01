#include "drum_sequencer_core.h"

struct DrumSequencerAu : DrumSequencerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "drum_sequencer_au";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[1] = {};
        compute(0.0f, 0.0f, ctx->param_values,
                local_out, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            ctx->output_buffers[0][i] = local_out[0];
    }
};

VIVID_REGISTER(DrumSequencerAu)
VIVID_THUMBNAIL(DrumSequencerAu)
VIVID_INSPECTOR(DrumSequencerAu)
