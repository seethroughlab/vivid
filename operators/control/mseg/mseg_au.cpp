// Audio-rate MSEG variant.
#include "mseg.h"
#include "control/audio_scalar_utils.h"
#include "operator_api/thumbnail.h"

struct MSEG_AU : MSEG, vivid::AudioProcessable {
    static constexpr const char* kName = "MsegAu";

    void process_audio(const VividAudioContext* ctx) override {
        float sample_dt = ctx->sample_rate > 0
            ? 1.0f / static_cast<float>(ctx->sample_rate)
            : static_cast<float>(ctx->delta_time);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float gate_in = vivid::audio_scalar_sample(ctx, 0, i);
            compute(gate_in, sample_dt);
            ctx->output_buffers[0][i] = current_value_ * amplitude.value;
        }
    }
};

VIVID_REGISTER(MSEG_AU)
VIVID_THUMBNAIL(MSEG_AU)
VIVID_INSPECTOR(MSEG_AU)
