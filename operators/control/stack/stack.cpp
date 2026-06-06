#include "operator_api/operator.h"
#include <algorithm>

static constexpr int kMaxInputs = 16;

/**
 * @brief Combines up to 16 lane arrays into one via concatenation or interleaving.
 *
 * Merges connected input lane arrays into a single output. Concat
 * mode appends them end-to-end; interleave mode alternates elements.
 * Uses repeat-group ports for grow-on-connect UI behavior.
 *
 * @see Alternate, PatTransform
 */
struct Stack : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Stack";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    vivid::Param<int> mode {"mode", 0, {"Concat","Interleave"}};

    Stack() {
        vivid::description(mode, "Concat appends lane arrays end-to-end; Interleave alternates elements");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input_0",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  0});
        out.push_back({"input_1",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  1});
        out.push_back({"input_2",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  2});
        out.push_back({"input_3",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  3});
        out.push_back({"input_4",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  4});
        out.push_back({"input_5",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  5});
        out.push_back({"input_6",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  6});
        out.push_back({"input_7",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  7});
        out.push_back({"input_8",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  8});
        out.push_back({"input_9",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  9});
        out.push_back({"input_10", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 10});
        out.push_back({"input_11", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 11});
        out.push_back({"input_12", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 12});
        out.push_back({"input_13", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 13});
        out.push_back({"input_14", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 14});
        out.push_back({"input_15", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 15});
        out.push_back({"output", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->param_values, ctx->values, &ctx->value_outputs[0], ctx->output_values);
    }

private:
    void compute(const float* params, const VividValueView* in_values,
                 VividValueOutput* out_value, float* output_values) {
        if (!in_values || !out_value) return;

        int m = std::clamp(static_cast<int>(params[0]), 0, 1);

        // Collect non-empty input lane arrays
        uint32_t input_lengths[kMaxInputs];
        const float* input_data[kMaxInputs];
        int input_count = 0;
        for (int i = 0; i < kMaxInputs; ++i) {
            uint32_t len = vivid_value_count(&in_values[i]);
            if (len > 0) {
                input_lengths[input_count] = len;
                input_data[input_count] = vivid_value_floats(&in_values[i]);
                ++input_count;
            }
        }

        if (input_count == 0) {
            vivid_value_output_commit(out_value, 0);
            return;
        }

        if (m == 0) {
            // Concat: append all inputs end-to-end
            uint32_t total = 0;
            for (int i = 0; i < input_count; ++i)
                total += input_lengths[i];
            float* buf = vivid_value_output_floats(out_value, total);
            if (!buf) return;

            uint32_t pos = 0;
            for (int i = 0; i < input_count && pos < total; ++i) {
                for (uint32_t j = 0; j < input_lengths[i] && pos < total; ++j)
                    buf[pos++] = input_data[i][j];
            }
            vivid_value_output_commit(out_value, total);

            if (output_values)
                output_values[0] = (total > 0) ? buf[0] : 0.0f;
        } else {
            // Interleave: round-robin from non-empty inputs
            uint32_t max_len = 0;
            for (int i = 0; i < input_count; ++i)
                max_len = std::max(max_len, input_lengths[i]);

            uint32_t total = 0;
            for (int i = 0; i < input_count; ++i)
                total += input_lengths[i];
            float* buf = vivid_value_output_floats(out_value, total);
            if (!buf) return;

            uint32_t pos = 0;
            for (uint32_t round = 0; round < max_len && pos < total; ++round) {
                for (int i = 0; i < input_count && pos < total; ++i) {
                    if (round < input_lengths[i])
                        buf[pos++] = input_data[i][round];
                }
            }
            vivid_value_output_commit(out_value, total);

            if (output_values)
                output_values[0] = (total > 0) ? buf[0] : 0.0f;
        }
    }
};

VIVID_DEFINE_OP(Stack) {
}

