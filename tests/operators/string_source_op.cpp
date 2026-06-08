#include "operator_api/operator.h"

#include <string>
#include <vector>

struct StringSourceOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "StringSourceOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::TextValue> value{"value", "alpha"};
    std::vector<std::string> lanes_{"alpha", "beta", "gamma"};
    std::vector<const char*> lanes_ptrs_;

    StringSourceOp() {
        lanes_ptrs_.reserve(lanes_.size());
        for (const auto& s : lanes_) lanes_ptrs_.push_back(s.c_str());
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&value); }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_STRING, VIVID_PORT_OUTPUT});
        out.push_back({.name="list", .type=VIVID_PORT_STRING, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (ctx->output_string_values) ctx->output_string_values[0] = value.str_value.c_str();
        // Many-string output via the value API (port "list", index 1) — successor
        // to ctx->output_string_lanes. (7d.5b)
        VividValueOutput* out = ctx->value_outputs ? &ctx->value_outputs[1] : nullptr;
        if (out && out->resize) {
            uint32_t n = static_cast<uint32_t>(lanes_ptrs_.size());
            if (out->resize(out->handle, n)) {
                for (uint32_t i = 0; i < n; ++i)
                    vivid_value_output_set_string(out, i, lanes_ptrs_[i]);
                vivid_value_output_commit(out, n);
            }
        }
    }
};

