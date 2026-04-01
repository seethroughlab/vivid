#include "operator_api/operator.h"

#include <algorithm>
#include <cmath>
/**
 * @brief Trigger-driven counter with modulus wrapping.
 *
 * Increments on each rising edge of the trigger input. Wraps to zero
 * when reaching the modulus. Outputs the current index and a wrapped
 * flag on overflow. Reset returns to initial value.
 *
 * @see Euclidean, StepSeq, Math
 */
struct StepCounter : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName = "StepCounter";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int> initial{"initial", 0, -1000000, 1000000};

    StepCounter() {
        vivid::semantic_tag(initial, "index");
        vivid::semantic_shape(initial, "int");
        vivid::description(initial, "Starting count value and reset target");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&initial);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"trigger", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"modulus", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"reset", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"index", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"wrapped", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    // Shared advance logic: returns wrapped flag
    bool advance(float trigger, int modulus, bool reset) {
        if (!initialized_) {
            step_ = initial.int_value();
            initialized_ = true;
        }

        bool wrapped = false;
        if (reset) {
            step_ = initial.int_value();
            if (step_ >= modulus || step_ < 0) {
                step_ = ((step_ % modulus) + modulus) % modulus;
                wrapped = true;
            }
        } else if (trigger > 0.5f && prev_trigger_ <= 0.5f) {
            step_++;
            if (step_ >= modulus) {
                step_ = 0;
                wrapped = true;
            }
        }

        prev_trigger_ = trigger;
        return wrapped;
    }

    void process_frame(const VividFrameContext* ctx) override {
        int modulus = std::max(1, static_cast<int>(std::floor(ctx->input_values[1])));
        bool wrapped = advance(ctx->input_values[0], modulus, ctx->input_values[2] > 0.5f);
        ctx->output_values[0] = static_cast<float>(step_);
        ctx->output_values[1] = wrapped ? 1.0f : 0.0f;
    }

    void process_audio(const VividAudioContext* ctx) override {
        float trigger = 0.0f;
        int modulus = std::max(1, static_cast<int>(std::floor(0.0f)));
        bool reset = 0.0f > 0.5f;
        bool wrapped = advance(trigger, modulus, reset);
        float idx = static_cast<float>(step_);
        float wrap_val = wrapped ? 1.0f : 0.0f;
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            ctx->output_buffers[0][i] = idx;
            ctx->output_buffers[1][i] = wrap_val;
        }
    }

private:
    float prev_trigger_ = 0.0f;
    bool initialized_ = false;
    int step_ = 0;
};

// Legacy registration removed — use _fr/_au variants instead.
