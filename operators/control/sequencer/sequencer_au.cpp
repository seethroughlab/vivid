#include "sequencer_core.h"

struct SequencerAu : SequencerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "sequencer_au";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        compute(0.0f, 0.0f,
                ctx->input_lanes, local_out,
                ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(SequencerAu)
