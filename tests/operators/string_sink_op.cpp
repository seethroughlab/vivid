#include "operator_api/operator.h"

#include <string>
#include <vector>

struct StringSinkOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "StringSinkOp";
    static constexpr bool kTimeDependent = false;

    std::string last_;
    std::vector<std::string> last_lanes_;
    std::vector<const char*> out_lane_ptrs_;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in", VIVID_PORT_STRING, VIVID_PORT_INPUT});
        out.push_back({"in_list", VIVID_PORT_STRING_LANES, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_STRING, VIVID_PORT_OUTPUT});
        out.push_back({"out_list", VIVID_PORT_STRING_LANES, VIVID_PORT_OUTPUT});
        out.push_back({"valid", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"count", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        last_.clear();
        if (ctx->input_string_values && ctx->input_string_values[0]) last_ = ctx->input_string_values[0];

        last_lanes_.clear();
        out_lane_ptrs_.clear();
        // Many-string input via the value API (port "in_list", index 1) —
        // successor to ctx->input_string_lanes. (7d.5b)
        if (ctx->values) {
            const VividValueView* in = &ctx->values[1];
            const char* const* data = vivid_value_strings(in);
            const uint32_t n = vivid_value_count(in);
            for (uint32_t i = 0; i < n; ++i) {
                const char* s = data ? data[i] : nullptr;
                last_lanes_.push_back(s ? s : "");
            }
        }
        out_lane_ptrs_.reserve(last_lanes_.size());
        for (const auto& s : last_lanes_) out_lane_ptrs_.push_back(s.c_str());

        if (ctx->output_string_values) ctx->output_string_values[0] = last_.c_str();
        // Many-string output via the value API (port "out_list", output ordinal 1).
        VividValueOutput* out = ctx->value_outputs ? &ctx->value_outputs[1] : nullptr;
        if (out && out->resize) {
            uint32_t n = static_cast<uint32_t>(out_lane_ptrs_.size());
            if (out->resize(out->handle, n)) {
                for (uint32_t i = 0; i < n; ++i)
                    vivid_value_output_set_string(out, i, out_lane_ptrs_[i]);
                vivid_value_output_commit(out, n);
            }
        }
        if (ctx->output_values) {
            ctx->output_values[2] = last_.empty() ? 0.0f : 1.0f;
            ctx->output_values[3] = static_cast<float>(last_lanes_.size());
        }
    }
};

