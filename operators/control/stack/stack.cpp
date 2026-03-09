#include "operator_api/operator.h"
#include <algorithm>

struct Stack : vivid::OperatorBase {
    static constexpr const char* kName   = "Stack";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> mode {"mode", 0, {"Concat","Interleave"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);  // 0
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // in spread[0]
        out.push_back({"b",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // in spread[1]
        out.push_back({"c",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // in spread[2]
        out.push_back({"d",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // in spread[3]
        out.push_back({"output", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});  // out spread[0]
    }

    void process(VividProcessContext* ctx) override {
        if (!ctx->input_spreads || !ctx->output_spreads) return;

        auto& out = ctx->output_spreads[0];
        int m = std::clamp(static_cast<int>(ctx->param_values[0]), 0, 1);

        // Collect non-empty input spreads
        const VividSpreadPort* inputs[4];
        int input_count = 0;
        for (int i = 0; i < 4; ++i) {
            if (ctx->input_spreads[i].length > 0)
                inputs[input_count++] = &ctx->input_spreads[i];
        }

        if (input_count == 0) {
            out.length = 0;
            return;
        }

        if (m == 0) {
            // Concat: [a0..an, b0..bm, c0..ck, d0..dj]
            uint32_t total = 0;
            for (int i = 0; i < input_count; ++i)
                total += inputs[i]->length;
            total = std::min(total, out.capacity);
            out.length = total;

            uint32_t pos = 0;
            for (int i = 0; i < input_count && pos < total; ++i) {
                auto& sp = *inputs[i];
                for (uint32_t j = 0; j < sp.length && pos < total; ++j)
                    out.data[pos++] = sp.data[j];
            }
        } else {
            // Interleave: round-robin from non-empty inputs
            uint32_t max_len = 0;
            for (int i = 0; i < input_count; ++i)
                max_len = std::max(max_len, inputs[i]->length);

            uint32_t total = 0;
            for (int i = 0; i < input_count; ++i)
                total += inputs[i]->length;
            total = std::min(total, out.capacity);
            out.length = total;

            uint32_t pos = 0;
            for (uint32_t round = 0; round < max_len && pos < total; ++round) {
                for (int i = 0; i < input_count && pos < total; ++i) {
                    if (round < inputs[i]->length)
                        out.data[pos++] = inputs[i]->data[round];
                }
            }
        }

        // Scalar output = first element of output spread
        ctx->output_values[0] = (out.length > 0) ? out.data[0] : 0.0f;
    }
};

VIVID_REGISTER(Stack)
