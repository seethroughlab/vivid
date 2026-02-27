#include "operator_api/operator.h"

struct Logic : vivid::OperatorBase {
    static constexpr const char* kName   = "Logic";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> operation{"operation", 0, {"AND", "OR", "XOR", "NOT", "NAND", "NOR"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&operation);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a",      VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"b",      VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"result", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        bool a = ctx->input_values[0] > 0.5f;
        bool b = ctx->input_values[1] > 0.5f;
        bool result = false;

        switch (operation.int_value()) {
            case 0: result = a && b;     break;  // AND
            case 1: result = a || b;     break;  // OR
            case 2: result = a != b;     break;  // XOR
            case 3: result = !a;         break;  // NOT
            case 4: result = !(a && b);  break;  // NAND
            case 5: result = !(a || b);  break;  // NOR
        }

        ctx->output_values[0] = result ? 1.0f : 0.0f;
    }
};

VIVID_REGISTER(Logic)
