#include "operator_api/operator.h"
#include <cmath>
/**
 * @brief Captures a signal value on trigger edge or tracks while gate is high.
 *
 * In sample mode, latches the input value on each rising edge. In
 * track-and-hold mode, continuously follows the input while trigger is
 * high and holds the last value when it goes low.
 *
 * @see Smooth, Gate, LFO
 */
struct SampleHold : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "SampleHold";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> mode{"mode", 0, {"sample", "track_and_hold"}};

    SampleHold() {
        vivid::semantic_shape(mode, "enum");
        vivid::description(mode, "Sample latches on rising edge; track-and-hold follows while high");
    }

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

    void process_frame(const VividFrameContext* ctx) override {
        advance(ctx->input_values[0], ctx->input_values[1] > 0.5f, mode.int_value());
        ctx->output_values[0] = held_value_;
    }

    void process_audio(const VividAudioContext* ctx) override {
        float signal = ctx->input_float_values[0];
        bool trig = ctx->input_float_values[1] > 0.5f;
        int m = mode.int_value();
        advance(signal, trig, m);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            ctx->output_buffers[0][i] = held_value_;
    }

private:
    float held_value_ = 0.0f;
    bool prev_trigger_ = false;
};

// Legacy registration removed — use _fr/_au variants instead.
