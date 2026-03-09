#include "operator_api/operator.h"
#include "operator_api/input_state.h"

struct Mouse : vivid::OperatorBase {
    static constexpr const char* kName   = "Mouse";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"x",      VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"y",      VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"left",   VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"right",  VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"middle", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(VividProcessContext* ctx) override {
        const VividInputState* input = vivid_input(ctx);
        if (!input) {
            ctx->output_values[0] = 0.0f;
            ctx->output_values[1] = 0.0f;
            ctx->output_values[2] = 0.0f;
            ctx->output_values[3] = 0.0f;
            ctx->output_values[4] = 0.0f;
            return;
        }

        ctx->output_values[0] = input->mouse_x;
        ctx->output_values[1] = input->mouse_y;
        ctx->output_values[2] = (input->buttons_held & (1 << 0)) ? 1.0f : 0.0f;
        ctx->output_values[3] = (input->buttons_held & (1 << 1)) ? 1.0f : 0.0f;
        ctx->output_values[4] = (input->buttons_held & (1 << 2)) ? 1.0f : 0.0f;
    }
};

VIVID_REGISTER(Mouse)
