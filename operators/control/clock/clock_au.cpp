#include "clock_core.h"
#include "operator_api/thumbnail.h"

struct ClockAu : ClockCore, vivid::AudioProcessable {
    static constexpr const char* kName = "ClockAu";

    void process_audio(const VividAudioContext* ctx) override {
        double delta_time = static_cast<double>(ctx->buffer_size) / ctx->sample_rate;
        float out4[4];
        advance(delta_time, out4);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 4; ++j)
                ctx->output_buffers[j][i] = out4[j];
        }
    }
};

VIVID_REGISTER(ClockAu)
VIVID_THUMBNAIL(ClockAu)
