#include "tracker_core.h"
#include "control/audio_scalar_utils.h"

struct Tracker : TrackerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "Tracker";

    void process_audio(const VividAudioContext* ctx) override {
        float local_in[2] = {
            vivid::resolve_clock_phase(
                clock_source.int_value(), vivid::audio_scalar_block_start(ctx, 0), vivid::metronome_transport(ctx)),
            vivid::audio_scalar_block_start(ctx, 1),  // reset
        };
        float local_out[3] = {};
        compute(local_in, ctx->param_values, ctx->output_lanes,
                local_out, ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[3 + j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(Tracker)
VIVID_THUMBNAIL(Tracker)
VIVID_INSPECTOR(Tracker)
