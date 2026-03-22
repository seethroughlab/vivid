#include "operator_api/operator.h"
#include <cmath>
#include <algorithm>

struct Math : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "Math";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> operation{"operation", 0, {"add", "multiply", "min", "max"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&operation);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a",      VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"b",      VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"result", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float a = ctx->input_values[0];
        float b = ctx->input_values[1];
        float result = 0.0f;

        switch (operation.int_value()) {
            case 0: result = a + b; break;
            case 1: result = a * b; break;
            case 2: result = std::min(a, b); break;
            case 3: result = std::max(a, b); break;
        }

        ctx->output_values[0] = result;
    }
};

VIVID_REGISTER(Math)
