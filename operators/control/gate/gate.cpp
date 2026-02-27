#include "operator_api/operator.h"

struct Gate : vivid::OperatorBase {
    static constexpr const char* kName   = "Gate";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> threshold{"threshold", 0.5f, 0.0f, 1.0f};
    vivid::Param<bool>  invert{"invert", false};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&threshold);
        out.push_back(&invert);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"signal", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"gate",   VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value",  VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"open",   VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float signal = ctx->input_values[0];
        bool is_open = ctx->input_values[1] > threshold.value;
        if (invert.bool_value()) is_open = !is_open;

        ctx->output_values[0] = is_open ? signal : 0.0f;
        ctx->output_values[1] = is_open ? 1.0f : 0.0f;
    }
};

VIVID_REGISTER(Gate)
