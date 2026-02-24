#include "operator_api/operator.h"
#include <cmath>

struct Envelope : vivid::OperatorBase {
    static constexpr const char* kName   = "Envelope";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> attack   {"attack",    0.001f, 0.0f, 0.5f};
    vivid::Param<float> decay    {"decay",     0.2f,   0.01f, 2.0f};
    vivid::Param<float> amplitude{"amplitude", 1.0f,   0.0f, 10.0f};
    vivid::Param<float> offset   {"offset",    0.0f,   0.0f, 10.0f};

    double trigger_time_ = 1000.0;  // large = silent at startup
    float  prev_phase_   = 0.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&amplitude);
        out.push_back(&offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"phase", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float phase_in = ctx->input_values[0];

        // Trigger detection: phase wraps (drops by > 0.5)
        float delta = phase_in - prev_phase_;
        if (delta < -0.5f) {
            trigger_time_ = 0.0;
        }
        prev_phase_ = phase_in;

        trigger_time_ += ctx->delta_time;

        // Compute envelope
        double env = 0.0;
        double atk = attack.value;
        double dec = decay.value;

        if (trigger_time_ <= atk && atk > 0.0) {
            // Attack phase: linear ramp from 0 to 1
            env = trigger_time_ / atk;
        } else {
            // Decay phase: exponential decay from 1
            double elapsed = trigger_time_ - atk;
            env = std::exp(-elapsed * 5.0 / dec);
        }

        ctx->output_values[0] = static_cast<float>(env) * amplitude.value + offset.value;
    }
};

VIVID_REGISTER(Envelope)
