#include "operator_api/operator.h"
#include <algorithm>
/**
 * @brief Combines up to 4 lane arrays into one via concatenation or interleaving.
 *
 * Merges input lane arrays A through D into a single output. Concat
 * mode appends them end-to-end; interleave mode alternates elements.
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
        out.push_back(&mode);  // 0
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"a",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // in lane[0]
        out.push_back({"b",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // in lane[1]
        out.push_back({"c",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // in lane[2]
        out.push_back({"d",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});   // in lane[3]
        out.push_back({"output", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // out lane[0]
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
        const VividLaneView* inputs[4];
        int input_count = 0;
        for (int i = 0; i < 4; ++i) {
            if (in_lanes[i].length > 0)
                inputs[input_count++] = &in_lanes[i];
        }

        if (input_count == 0) {
            out.commit(out.handle, 0);
            return;
        }

        if (m == 0) {
            // Concat: [a0..an, b0..bm, c0..ck, d0..dj]
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
