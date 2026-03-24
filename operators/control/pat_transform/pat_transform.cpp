#include "operator_api/operator.h"
#include <algorithm>
#include <cstdint>

struct PatTransform : vivid::ControlOperatorBase {
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

        vivid::semantic_tag(rotate, "index");
        vivid::semantic_shape(rotate, "int");

        vivid::semantic_tag(probability, "probability_01");
        vivid::semantic_shape(probability, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&reverse);      // 0
        out.push_back(&rotate);       // 1
        out.push_back(&scale);        // 2
        out.push_back(&offset);       // 3
        out.push_back(&probability);  // 4
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"pattern", VIVID_PORT_SPREAD, VIVID_PORT_INPUT});   // in spread[0]
        out.push_back({"pattern", VIVID_PORT_SPREAD, VIVID_PORT_OUTPUT});  // out spread[0]
    }

    void process(const VividProcessContext* ctx) override {
        if (!ctx->input_spreads || !ctx->output_spreads) return;

        auto& in  = ctx->input_spreads[0];
        auto& out = ctx->output_spreads[0];

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
