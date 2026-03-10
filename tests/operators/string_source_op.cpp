#include "operator_api/operator.h"

#include <algorithm>
#include <string>
#include <vector>

struct StringSourceOp : vivid::ControlOperatorBase {
    static constexpr const char* kName = "StringSourceOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::TextValue> value{"value", "alpha"};
    std::vector<std::string> spread_{"alpha", "beta", "gamma"};
    std::vector<const char*> spread_ptrs_;

    StringSourceOp() {
        spread_ptrs_.reserve(spread_.size());
        for (const auto& s : spread_) spread_ptrs_.push_back(s.c_str());
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&value); }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_STRING, VIVID_PORT_OUTPUT});
        out.push_back({"list", VIVID_PORT_STRING_SPREAD, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        if (ctx->output_string_values) ctx->output_string_values[0] = value.str_value.c_str();
        if (ctx->output_string_spreads && ctx->output_string_spreads[1].data) {
            auto& sp = ctx->output_string_spreads[1];
            uint32_t n = std::min<uint32_t>(sp.capacity, static_cast<uint32_t>(spread_ptrs_.size()));
            sp.length = n;
            for (uint32_t i = 0; i < n; ++i) sp.data[i] = spread_ptrs_[i];
        }
    }
};

VIVID_REGISTER(StringSourceOp)
