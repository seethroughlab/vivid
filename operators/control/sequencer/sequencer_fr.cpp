#include "sequencer_core.h"

struct SequencerFr : SequencerCore, vivid::FrameProcessable {
    static constexpr const char* kName = "SequencerFr";

    void process_frame(const VividFrameContext* ctx) override {
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), ctx->input_values[0], vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->input_values[1],
                ctx->input_lanes, ctx->output_values,
                ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(SequencerFr)
