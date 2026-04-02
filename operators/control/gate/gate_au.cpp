// Audio-rate Gate variant.
#include "gate.h"
#include "control/audio_scalar_utils.h"
#include "operator_api/thumbnail.h"

struct GateAu : Gate, vivid::AudioProcessable {
    static constexpr const char* kName = "GateAu";

    void process_audio(const VividAudioContext* ctx) override {
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float signal = vivid::audio_scalar_sample(ctx, 0, i);
            float gate_in = vivid::audio_scalar_sample(ctx, 1, i);
            bool is_open = gate_in > threshold.value;
            if (invert.bool_value()) is_open = !is_open;

            ctx->output_buffers[0][i] = is_open ? signal : 0.0f;
            ctx->output_buffers[1][i] = is_open ? 1.0f : 0.0f;
        }
    }
};

VIVID_REGISTER(GateAu)
VIVID_THUMBNAIL(GateAu)
