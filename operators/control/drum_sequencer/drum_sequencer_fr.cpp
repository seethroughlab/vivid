#include "drum_sequencer_core.h"

struct DrumSequencerFr : DrumSequencerCore, vivid::FrameProcessable {
    static constexpr const char* kName = "DrumSequencerFr";

    void process_frame(const VividFrameContext* ctx) override {
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), ctx->input_values[0], vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->input_values[1], ctx->param_values,
                ctx->output_values, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(DrumSequencerFr)
VIVID_THUMBNAIL(DrumSequencerFr)
VIVID_INSPECTOR(DrumSequencerFr)
