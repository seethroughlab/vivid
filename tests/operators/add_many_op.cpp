// Test fixture: a 2-input element-wise combiner (KERNEL — reads the whole
// collection on each input, output inferred Many). Used by test_value_normalization
// to observe frame_executor's lane-count normalization: it reads the two (already
// normalized) Many input arrays and writes out[i] = a[i] + b[i].
#include "operator_api/operator.h"
#include <algorithm>

struct AddManyOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "AddManyOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_KERNEL;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in_a", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"in_b", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"out",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        const VividValueView* va = ctx->values ? &ctx->values[0] : nullptr;
        const VividValueView* vb = ctx->values ? &ctx->values[1] : nullptr;
        const float* a = vivid_value_floats(va);
        const float* b = vivid_value_floats(vb);
        const uint32_t na = vivid_value_count(va);
        const uint32_t nb = vivid_value_count(vb);
        const uint32_t n = std::max(na, nb);

        ctx->output_values[0] = (a && na ? a[0] : 0.0f) + (b && nb ? b[0] : 0.0f);

        VividValueOutput* ov = ctx->value_outputs ? &ctx->value_outputs[0] : nullptr;
        if (n > 0) {
            float* buf = vivid_value_output_floats(ov, n);
            if (buf) {
                for (uint32_t i = 0; i < n; ++i) {
                    float av = (a && i < na) ? a[i] : 0.0f;
                    float bv = (b && i < nb) ? b[i] : 0.0f;
                    buf[i] = av + bv;
                }
                vivid_value_output_commit(ov, n);
            }
        }
    }
};
