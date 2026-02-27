#include "operator_api/operator.h"
#include <cmath>
#include <algorithm>

struct Sequencer : vivid::OperatorBase {
    static constexpr const char* kName   = "Sequencer";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   steps {"steps",  8, 1, 16};
    vivid::Param<float> val_0 {"val_0",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_1 {"val_1",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_2 {"val_2",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_3 {"val_3",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_4 {"val_4",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_5 {"val_5",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_6 {"val_6",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_7 {"val_7",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_8 {"val_8",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_9 {"val_9",  0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_10{"val_10", 0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_11{"val_11", 0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_12{"val_12", 0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_13{"val_13", 0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_14{"val_14", 0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> val_15{"val_15", 0.0f, -10000.0f, 10000.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&steps);                         // param 0
        out.push_back(&val_0);  out.push_back(&val_1); // param 1..2
        out.push_back(&val_2);  out.push_back(&val_3); // param 3..4
        out.push_back(&val_4);  out.push_back(&val_5); // param 5..6
        out.push_back(&val_6);  out.push_back(&val_7); // param 7..8
        out.push_back(&val_8);  out.push_back(&val_9); // param 9..10
        out.push_back(&val_10); out.push_back(&val_11); // param 11..12
        out.push_back(&val_12); out.push_back(&val_13); // param 13..14
        out.push_back(&val_14); out.push_back(&val_15); // param 15..16
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"phase",   VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"reset",   VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value",   VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"step",    VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"trigger", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float phase = ctx->input_values[0];
        bool reset = ctx->input_values[1] > 0.5f;

        // Rising-edge reset: capture current phase as offset
        if (reset && !prev_reset_)
            phase_offset_ = phase;
        prev_reset_ = reset;

        float adj_phase = std::fmod(phase - phase_offset_ + 1.0f, 1.0f);
        int n = std::max(steps.int_value(), 1);
        int step = static_cast<int>(adj_phase * n);
        step = std::clamp(step, 0, n - 1);

        // val_0 is param index 1 (steps is param 0)
        float value = ctx->param_values[1 + step];

        bool step_changed = (step != prev_step_);
        prev_step_ = step;

        ctx->output_values[0] = value;
        ctx->output_values[1] = static_cast<float>(step);
        ctx->output_values[2] = step_changed ? 1.0f : 0.0f;
    }

private:
    int prev_step_ = -1;
    float phase_offset_ = 0.0f;
    bool prev_reset_ = false;
};

VIVID_REGISTER(Sequencer)
