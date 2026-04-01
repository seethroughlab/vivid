#include "operator_api/operator.h"
/**
 * @brief Boolean logic gate operating on two control signals.
 *
 * Applies AND, OR, XOR, NOT, NAND, or NOR to two inputs (threshold
 * > 0.5 = true). NOT only uses input A.
 *
 * @see Math, Gate
 */
struct Logic : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Logic";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> operation{"operation", 0, {"AND", "OR", "XOR", "NOT", "NAND", "NOR"}};

    Logic() {
        vivid::description(operation, "Boolean operation to apply (NOT uses only input A)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&operation);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"b",      VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"result", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    float compute(float a_val, float b_val, int op) const {
        bool a = a_val > 0.5f;
        bool b = b_val > 0.5f;
        bool result = false;
        switch (op) {
            case 0: result = a && b;     break;  // AND
            case 1: result = a || b;     break;  // OR
            case 2: result = a != b;     break;  // XOR
            case 3: result = !a;         break;  // NOT
            case 4: result = !(a && b);  break;  // NAND
            case 5: result = !(a || b);  break;  // NOR
        }
        return result ? 1.0f : 0.0f;
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = compute(ctx->input_values[0], ctx->input_values[1],
                                        operation.int_value());
    }

};

VIVID_REGISTER(Logic)
