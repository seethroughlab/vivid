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
        out.push_back({.name="input_0",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=0,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_1",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=1,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_2",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=2,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_3",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=3,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_4",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=4,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_5",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=5,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_6",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=6,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_7",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=7,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_8",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=8,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_9",  .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=9,  .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_10", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=10, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_11", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=11, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_12", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=12, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_13", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=13, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_14", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=14, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="input_15", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .repeat_group="input", .repeat_group_idx=15, .multiplicity=VIVID_MULTIPLICITY_MANY});
        out.push_back({.name="output", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
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

