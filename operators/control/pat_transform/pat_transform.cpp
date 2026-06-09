#include "operator_api/operator.h"
#include <algorithm>
#include <cstdint>
/**
 * @brief Transforms a lane-array pattern with reverse, rotate, scale, and probability.
 *
 * Applies a chain of transformations to an input lane array: reverse, rotate
 * by N positions, scale and offset all values, then probabilistically
 * mask elements using a deterministic hash.
 *
 * @param probability Chance each element survives (1.0 = all pass, 0.0 = all masked).
 * @see Stack, Alternate, Euclidean
 */
struct PatTransform : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "PatTransform";
    static constexpr bool kTimeDependent = false;

    vivid::Param<bool>  reverse     {"reverse",     false};
    vivid::Param<int>   rotate      {"rotate",      0, -32, 32};
    vivid::Param<float> scale       {"scale",       1.0f, -100.0f, 100.0f};
    vivid::Param<float> offset      {"offset",      0.0f, -10000.0f, 10000.0f};
    vivid::Param<float> probability {"probability", 1.0f, 0.0f, 1.0f};

    PatTransform() {
        vivid::semantic_tag(reverse, "enabled");
        vivid::semantic_shape(reverse, "bool");
        vivid::description(reverse, "Flip the pattern so the last element becomes the first");

        vivid::semantic_tag(rotate, "index");
        vivid::semantic_shape(rotate, "int");
        vivid::description(rotate, "Shift the pattern left or right by N positions");

        vivid::description(scale, "Multiplier applied to every element in the pattern");
        vivid::description(offset, "Constant added to every element after scaling");

        vivid::semantic_tag(probability, "probability_01");
        vivid::semantic_shape(probability, "scalar");
        vivid::description(probability, "Chance each element survives, 0 = all masked, 1 = all pass");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&reverse);      // 0
        out.push_back(&rotate);       // 1
        out.push_back(&scale);        // 2
        out.push_back(&offset);       // 3
        out.push_back(&probability);  // 4
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({.name="pattern", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});   // in lane_array[0]
        out.push_back({.name="pattern", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});  // out lane_array[0]
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (!ctx->values || !ctx->value_outputs) return;

        const VividValueView* in = &ctx->values[0];
        VividValueOutput*     out = &ctx->value_outputs[0];

        uint32_t in_count = vivid_value_count(in);
        if (in_count == 0) {
            vivid_value_output_commit(out, 0);
            return;
        }

        bool rev  = ctx->param_values[0] > 0.5f;
        int rot   = static_cast<int>(ctx->param_values[1]);
        float sc  = ctx->param_values[2];
        float off = ctx->param_values[3];
        float prob = ctx->param_values[4];

        uint32_t n = in_count;
        const float* in_data = vivid_value_floats(in);
        float* buf = vivid_value_output_floats(out, n);
        if (!buf) return;

        // Copy input to output buffer (we'll transform in-place in the output)
        for (uint32_t i = 0; i < n; ++i)
            buf[i] = in_data[i];

        // Transform order: reverse -> rotate -> scale -> offset -> probability

        // 1. Reverse
        if (rev) {
            for (uint32_t i = 0; i < n / 2; ++i) {
                float tmp = buf[i];
                buf[i] = buf[n - 1 - i];
                buf[n - 1 - i] = tmp;
            }
        }

        // 2. Rotate
        if (rot != 0) {
            int shift = ((rot % static_cast<int>(n)) + static_cast<int>(n)) % static_cast<int>(n);
            if (shift != 0) {
                float tmp[1024];
                for (uint32_t i = 0; i < n; ++i)
                    tmp[i] = buf[(i + shift) % n];
                for (uint32_t i = 0; i < n; ++i)
                    buf[i] = tmp[i];
            }
        }

        // 3. Scale
        if (sc != 1.0f) {
            for (uint32_t i = 0; i < n; ++i)
                buf[i] *= sc;
        }

        // 4. Offset
        if (off != 0.0f) {
            for (uint32_t i = 0; i < n; ++i)
                buf[i] += off;
        }

        // 5. Probability: zero out elements that don't survive.
        //    Deterministic per element index using Knuth multiplicative hash.
        if (prob < 1.0f) {
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t hash = (i + 1) * 2654435761u;
                float rand01 = static_cast<float>(hash) / 4294967295.0f;
                if (rand01 >= prob)
                    buf[i] = 0.0f;
            }
        }

        vivid_value_output_commit(out, n);

        // Scalar fallback
        ctx->output_values[0] = (n > 0) ? buf[0] : 0.0f;
    }
};

VIVID_DEFINE_OP(PatTransform) {
}

