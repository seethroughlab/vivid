#include "operator_api/operator.h"
#include <algorithm>
#include <cstdint>
/**
 * @brief Transforms a spread pattern with reverse, rotate, scale, and probability.
 *
 * Applies a chain of transformations to an input spread: reverse, rotate
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
        out.push_back({"pattern", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // in spread[0]
        out.push_back({"pattern", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // out spread[0]
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (!ctx->input_lanes || !ctx->output_lanes) return;

        auto& in  = ctx->input_lanes[0];
        auto& out = ctx->output_lanes[0];

        if (in.length == 0) {
            out.length = 0;
            return;
        }

        bool rev  = ctx->param_values[0] > 0.5f;
        int rot   = static_cast<int>(ctx->param_values[1]);
        float sc  = ctx->param_values[2];
        float off = ctx->param_values[3];
        float prob = ctx->param_values[4];

        uint32_t n = std::min(in.length, out.capacity);
        out.length = n;

        // Copy input to output buffer (we'll transform in-place in the output)
        for (uint32_t i = 0; i < n; ++i)
            out.data[i] = in.data[i];

        // Transform order: reverse -> rotate -> scale -> offset -> probability

        // 1. Reverse
        if (rev) {
            for (uint32_t i = 0; i < n / 2; ++i) {
                float tmp = out.data[i];
                out.data[i] = out.data[n - 1 - i];
                out.data[n - 1 - i] = tmp;
            }
        }

        // 2. Rotate
        if (rot != 0) {
            int shift = ((rot % static_cast<int>(n)) + static_cast<int>(n)) % static_cast<int>(n);
            if (shift != 0) {
                float tmp[1024];
                for (uint32_t i = 0; i < n; ++i)
                    tmp[i] = out.data[(i + shift) % n];
                for (uint32_t i = 0; i < n; ++i)
                    out.data[i] = tmp[i];
            }
        }

        // 3. Scale
        if (sc != 1.0f) {
            for (uint32_t i = 0; i < n; ++i)
                out.data[i] *= sc;
        }

        // 4. Offset
        if (off != 0.0f) {
            for (uint32_t i = 0; i < n; ++i)
                out.data[i] += off;
        }

        // 5. Probability: zero out elements that don't survive.
        //    Deterministic per element index using Knuth multiplicative hash.
        if (prob < 1.0f) {
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t hash = (i + 1) * 2654435761u;
                float rand01 = static_cast<float>(hash) / 4294967295.0f;
                if (rand01 >= prob)
                    out.data[i] = 0.0f;
            }
        }

        // Scalar fallback
        ctx->output_values[0] = (n > 0) ? out.data[0] : 0.0f;
    }
};

VIVID_REGISTER(PatTransform)
