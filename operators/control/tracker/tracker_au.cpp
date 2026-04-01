#include "tracker_core.h"

struct TrackerAu : TrackerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "tracker_au";

    void process_audio(const VividAudioContext* ctx) override {
        compute(ctx->input_float_values, ctx->param_values, ctx->output_lanes,
                ctx->output_float_values, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(TrackerAu)
VIVID_THUMBNAIL(TrackerAu)
VIVID_INSPECTOR(TrackerAu)
