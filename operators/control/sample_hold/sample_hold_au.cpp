#include "operator_api/operator.h"
#include <cmath>

struct SampleHoldAu : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "sample_hold_au";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> mode{"mode", 0, {"sample", "track_and_hold"}};

    SampleHoldAu() {
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

    void process_audio(const VividAudioContext* ctx) override {
        float signal = 0.0f;
        bool trig = 0.0f > 0.5f;
        int m = mode.int_value();

        bool rising = trig && !prev_trigger_;
        prev_trigger_ = trig;
        if (m == 0) {
            if (rising) held_value_ = signal;
        } else {
            if (trig) held_value_ = signal;
        }

        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            ctx->output_buffers[0][i] = held_value_;
    }

private:
    float held_value_ = 0.0f;
    bool prev_trigger_ = false;
};

VIVID_REGISTER(SampleHoldAu)
