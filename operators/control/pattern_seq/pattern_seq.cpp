#include "pattern_seq_core.h"
#include "control/audio_scalar_utils.h"

struct PatternSeq : PatternSeqCore, vivid::AudioProcessable {
    static constexpr const char* kName = "PatternSeq";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[4] = {};
        float beat_phase = vivid::resolve_clock_phase(
            clock_mode.int_value(), vivid::audio_scalar_block_start(ctx, 0), vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->param_values, local_out,
                ctx->output_lanes, ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 4; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_DEFINE_OP(PatternSeq) {
}

VIVID_EDITOR(PatternSeq)
