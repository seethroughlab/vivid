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

    char port_names_[kMaxInputs][16];

    Stack() {
        vivid::description(mode, "Concat appends lane arrays end-to-end; Interleave alternates elements");
        for (int i = 0; i < kMaxInputs; ++i)
            std::snprintf(port_names_[i], sizeof(port_names_[i]), "input_%d", i);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        for (int i = 0; i < kMaxInputs; ++i) {
            VividPortDescriptor pd{};
            pd.name = port_names_[i];
            pd.type = VIVID_PORT_LANE_ARRAY;
            pd.direction = VIVID_PORT_INPUT;
            pd.repeat_group = "input";
            pd.repeat_group_idx = static_cast<uint16_t>(i);
            out.push_back(pd);
        }
        VividPortDescriptor out_port{};
        out_port.name = "output";
        out_port.type = VIVID_PORT_LANE_ARRAY;
        out_port.direction = VIVID_PORT_OUTPUT;
        out.push_back(out_port);
    }

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->param_values, ctx->input_lanes, ctx->output_lanes, ctx->output_values);
    }

private:
    void compute(const float* params, const VividLaneView* in_lanes,
                 VividLaneOutput* out_lanes, float* output_values) {
        if (!in_lanes || !out_lanes) return;

        auto& out = out_lanes[0];
        int m = std::clamp(static_cast<int>(params[0]), 0, 1);

        // Collect non-empty input lane arrays
        const VividLaneView* inputs[kMaxInputs];
        int input_count = 0;
        for (int i = 0; i < kMaxInputs; ++i) {
            if (in_lanes[i].length > 0)
                inputs[input_count++] = &in_lanes[i];
        }

        if (input_count == 0) {
            out.commit(out.handle, 0);
            return;
        }

        if (m == 0) {
            // Concat: append all inputs end-to-end
            uint32_t total = 0;
            for (int i = 0; i < input_count; ++i)
                total += inputs[i]->length;
            float* buf = out.resize(out.handle, total);
            if (!buf) return;

            uint32_t pos = 0;
            for (int i = 0; i < input_count && pos < total; ++i) {
                auto& sp = *inputs[i];
                for (uint32_t j = 0; j < sp.length && pos < total; ++j)
                    buf[pos++] = sp.data[j];
            }
            out.commit(out.handle, total);

            if (output_values)
                output_values[0] = (total > 0) ? buf[0] : 0.0f;
        } else {
            // Interleave: round-robin from non-empty inputs
            uint32_t max_len = 0;
            for (int i = 0; i < input_count; ++i)
                max_len = std::max(max_len, inputs[i]->length);

            uint32_t total = 0;
            for (int i = 0; i < input_count; ++i)
                total += inputs[i]->length;
            float* buf = out.resize(out.handle, total);
            if (!buf) return;

            uint32_t pos = 0;
            for (uint32_t round = 0; round < max_len && pos < total; ++round) {
                for (int i = 0; i < input_count && pos < total; ++i) {
                    if (round < inputs[i]->length)
                        buf[pos++] = inputs[i]->data[round];
                }
            }
            out.commit(out.handle, total);

            if (output_values)
                output_values[0] = (total > 0) ? buf[0] : 0.0f;
        }
    }
};

VIVID_REGISTER(Stack)
