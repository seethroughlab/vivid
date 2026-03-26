#include "operator_api/operator.h"
#include <cmath>
#include <algorithm>

// Math — dual-cadence control operator.
//
// Inherits both FrameProcessable and AudioProcessable, making it audio-capable.
// Stateless: applies a binary operation (add/mul/min/max) to two scalar inputs.
//
struct Math : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
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

    void process_audio(const VividAudioContext* ctx) override {
        float a = ctx->input_float_values[0];
        float b = ctx->input_float_values[1];
        float result = compute(a, b, operation.int_value());
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            ctx->output_buffers[0][i] = result;
    }
};

VIVID_REGISTER(Math)
