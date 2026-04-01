#include "tracker_core.h"

struct TrackerAu : TrackerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "tracker_au";

    void process_audio(const VividAudioContext* ctx) override {
        float local_in[2] = {};
        float local_out[3] = {};
        compute(local_in, ctx->param_values, ctx->output_lanes,
                local_out, ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(TrackerAu)
VIVID_THUMBNAIL(TrackerAu)
VIVID_INSPECTOR(TrackerAu)
