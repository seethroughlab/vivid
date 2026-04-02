#pragma once

#include "clock_core.h"

// Bare-name Clock for backward compatibility. Delegates to ClockCore.
struct Clock : ClockCore, vivid::FrameProcessable {
    static constexpr const char* kName = "Clock";

    void process_frame(const VividFrameContext* ctx) override {
        float out4[4];
        advance(ctx->delta_time, out4);
        for (int i = 0; i < 4; ++i)
            ctx->output_values[i] = out4[i];
    }
};
