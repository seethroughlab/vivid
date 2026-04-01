#include "arpeggiator_core.h"

struct ArpeggiatorAu : ArpeggiatorCore, vivid::AudioProcessable {
    static constexpr const char* kName = "arpeggiator_au";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[4] = {};
        compute(0.0f, ctx->param_values, ctx->input_lanes,
                local_out, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 4; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(ArpeggiatorAu)
VIVID_THUMBNAIL(ArpeggiatorAu)
VIVID_INSPECTOR(ArpeggiatorAu)
