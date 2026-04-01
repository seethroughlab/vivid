#include "operator_api/operator.h"
#include <cmath>
#include <algorithm>
/**
 * @brief Binary math operation on two control signals.
 *
 * Performs add, multiply, min, or max on inputs A and B. Chain multiple
 * Math operators for complex expressions.
 *
 * @see Logic, Macro, Quantizer
 */
struct Math : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Math";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> operation{"operation", 0, {"add", "multiply", "min", "max"}};

    Math() {
        vivid::description(operation, "Binary operation applied to inputs A and B");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&operation);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"b",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"result", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    float compute(float a, float b, int op) const {
        switch (op) {
            case 0: return a + b;
            case 1: return a * b;
            case 2: return std::min(a, b);
            case 3: return std::max(a, b);
        }
        return 0.0f;
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = compute(ctx->input_values[0], ctx->input_values[1],
                                        operation.int_value());
    }

};

VIVID_REGISTER(Math)
