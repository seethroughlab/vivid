#pragma once

#include "operator_api/operator.h"
#include <cmath>

struct SampleHoldThumbState;
/**
 * @brief Captures a signal value on trigger edge or tracks while gate is high.
 *
 * In sample mode, latches the input value on each rising edge. In
 * track-and-hold mode, continuously follows the input while trigger is
 * high and holds the last value when it goes low.
 *
 * @see Smooth, Gate, LFO
 */
struct SampleHold : vivid::OperatorBase {
    static constexpr const char* kName   = "SampleHold";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> mode{"mode", 0, {"sample", "track_and_hold"}};

    SampleHold() {
        vivid::semantic_shape(mode, "enum");
        vivid::description(mode, "Sample latches on rising edge; track-and-hold follows while high");
    }

    ~SampleHold() override;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"signal",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"trigger", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value",   VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void advance(float signal, bool trig, int m) {
        bool rising = trig && !prev_trigger_;
        prev_trigger_ = trig;
        if (m == 0) {
            if (rising) held_value_ = signal;
        } else {
            if (trig) held_value_ = signal;
        }
    }

    void process_frame_impl(const VividFrameContext* ctx) {
        advance(ctx->input_values[0], ctx->input_values[1] > 0.5f, mode.int_value());
        ctx->output_values[0] = held_value_;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

    float held_value_ = 0.0f;
    bool prev_trigger_ = false;

private:
    SampleHoldThumbState* thumb_state_ = nullptr;

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx);
};
