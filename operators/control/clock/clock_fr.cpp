#include "clock_core.h"
#include "operator_api/thumbnail.h"

struct ClockFr : ClockCore, vivid::FrameProcessable {
    static constexpr const char* kName = "ClockFr";

    void process_frame(const VividFrameContext* ctx) override {
        float out4[4];
        MetronomeSample metronome;
        metronome.bpm = ctx->metronome_bpm;
        metronome.beats_per_bar = static_cast<int>(ctx->metronome_beats_per_bar);
        metronome.beats_elapsed = ctx->metronome_beats_elapsed;
        metronome.beat_phase = ctx->metronome_beat_phase;
        metronome.bar_phase = ctx->metronome_bar_phase;
        metronome.beat_ms = ctx->metronome_beat_ms;
        advance(ctx->delta_time, metronome, out4);
        for (int i = 0; i < 4; ++i)
            ctx->output_values[i] = out4[i];
    }
};

VIVID_REGISTER(ClockFr)
VIVID_THUMBNAIL(ClockFr)
