#include "clock_core.h"
#include "operator_api/thumbnail.h"

struct Clock : ClockCore, vivid::AudioProcessable {
    static constexpr const char* kName = "Clock";

    void process_audio(const VividAudioContext* ctx) override {
        double delta_time = static_cast<double>(ctx->buffer_size) / ctx->sample_rate;
        float out4[4];
        MetronomeSample metronome;
        metronome.bpm = ctx->metronome_bpm;
        metronome.beats_per_bar = static_cast<int>(ctx->metronome_beats_per_bar);
        metronome.beats_elapsed = ctx->metronome_beats_elapsed;
        metronome.beat_phase = ctx->metronome_beat_phase;
        metronome.bar_phase = ctx->metronome_bar_phase;
        metronome.beat_ms = ctx->metronome_beat_ms;
        advance(delta_time, metronome, out4);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 4; ++j)
                ctx->output_buffers[j][i] = out4[j];
        }
    }
};

VIVID_REGISTER(Clock)
VIVID_THUMBNAIL(Clock)
