// Audio-rate SampleHold variant.
#include "sample_hold.h"
#include "control/audio_scalar_utils.h"
#include "operator_api/thumbnail.h"

struct SampleHoldAu : SampleHold, vivid::AudioProcessable {
    static constexpr const char* kName = "SampleHoldAu";

    void process_audio(const VividAudioContext* ctx) override {
        int m = mode.int_value();

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float signal = vivid::audio_scalar_sample(ctx, 0, i);
            bool trig = vivid::audio_scalar_sample(ctx, 1, i) > 0.5f;
            advance(signal, trig, m);
            ctx->output_buffers[0][i] = held_value_;
        }
    }
};

VIVID_REGISTER(SampleHoldAu)
VIVID_THUMBNAIL(SampleHoldAu)
