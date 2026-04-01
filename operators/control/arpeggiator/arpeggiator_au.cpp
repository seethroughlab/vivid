#include "arpeggiator_core.h"

struct ArpeggiatorAu : ArpeggiatorCore, vivid::AudioProcessable {
    static constexpr const char* kName = "arpeggiator_au";

    void process_audio(const VividAudioContext* ctx) override {
        compute(ctx->input_float_values[0], ctx->param_values, ctx->input_lanes,
                ctx->output_float_values, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(ArpeggiatorAu)
VIVID_THUMBNAIL(ArpeggiatorAu)
VIVID_INSPECTOR(ArpeggiatorAu)
