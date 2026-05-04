#include "euclidean_core.h"
#include "operator_api/thumbnail.h"

// --- Audio-rate operator entry + registration ---
//
// draw_thumbnail / draw_editor bodies live in euclidean_editor.cpp so
// the test target (which compiles that file) gets the operator's vtable
// without needing to pull in VIVID_REGISTER and its `extern "C"` bag.

#include "control/audio_scalar_utils.h"

struct Euclidean : EuclideanCore, vivid::AudioProcessable {
    static constexpr const char* kName = "Euclidean";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        const auto m = vivid::metronome_transport(ctx);
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), vivid::audio_scalar_block_start(ctx, 0), m);
        compute(beat_phase, m.beats_elapsed, m.beats_per_bar,
                ctx->param_values, ctx->output_lanes, local_out,
                ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_DEFINE_OP(Euclidean) {
}

VIVID_REGISTER(Euclidean)
VIVID_THUMBNAIL(Euclidean)
VIVID_EDITOR(Euclidean)
