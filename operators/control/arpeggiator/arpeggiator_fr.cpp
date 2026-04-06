#include "arpeggiator_core.h"

struct ArpeggiatorFr : ArpeggiatorCore, vivid::FrameProcessable {
    static constexpr const char* kName = "ArpeggiatorFr";

    void process_frame(const VividFrameContext* ctx) override {
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), ctx->input_values[0], vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->param_values, ctx->input_lanes,
                ctx->output_values, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(ArpeggiatorFr)
VIVID_THUMBNAIL(ArpeggiatorFr)
VIVID_INSPECTOR(ArpeggiatorFr)
