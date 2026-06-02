#include "operator_api/operator.h"
#include "control/audio_scalar_utils.h"
#include <algorithm>
#include <cmath>

struct StepCounter : vivid::OperatorBase, vivid::AudioProcessable {
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
        out.push_back({"reset", VIVID_PORT_SCALAR, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "trigger"});
        out.push_back({"index", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"wrapped", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        if (!initialized_) {
            step_ = initial.int_value();
            initialized_ = true;
        }

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float trigger = vivid::audio_scalar_sample(ctx, 0, i);
            int modulus = std::max(1, static_cast<int>(std::floor(vivid::audio_scalar_sample(ctx, 1, i))));
            bool reset = vivid::audio_scalar_sample(ctx, 2, i) > 0.5f;
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
            ctx->output_buffers[0][i] = static_cast<float>(step_);
            ctx->output_buffers[1][i] = wrapped ? 1.0f : 0.0f;
        }
    }

private:
    float prev_trigger_ = 0.0f;
    bool initialized_ = false;
    int step_ = 0;
};

VIVID_DEFINE_OP(StepCounter) {
    name = "StepCounter";
    keywords = {"counter", "trigger", "modulo", "step", "index", "advance"};
    summary = "Trigger-driven counter with modulo wrap and reset.";
}

