// Test operator: kernel behavior proof-of-concept.
// Reads the whole many-value input and writes a 3-element moving average across
// elements to the output. Demonstrates cross-element (kernel) access via the
// value API (ctx->values/value_outputs) — successor to the lane views. (7d.5b)
#include "operator_api/operator.h"
#include <algorithm>

struct LaneSmoothOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "LaneSmoothOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_KERNEL;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        // Pass through scalar value
        ctx->output_values[0] = ctx->input_values[0];

        // Kernel behavior: read the full many-value input, smooth across elements
        const VividValueView* iv = ctx->values        ? &ctx->values[0]        : nullptr;
        VividValueOutput*     ov = ctx->value_outputs ? &ctx->value_outputs[0] : nullptr;
        const float*          src = vivid_value_floats(iv);
        const uint32_t        len = vivid_value_count(iv);
        if (src && len > 0) {
            float* buf = vivid_value_output_floats(ov, len);
            if (buf) {
                for (uint32_t i = 0; i < len; ++i) {
                    // 3-element moving average: average with neighbors
                    float prev = (i > 0) ? src[i - 1] : src[i];
                    float curr = src[i];
                    float next = (i + 1 < len) ? src[i + 1] : src[i];
                    buf[i] = (prev + curr + next) / 3.0f;
                }
                vivid_value_output_commit(ov, len);
            }
        }
    }
};

