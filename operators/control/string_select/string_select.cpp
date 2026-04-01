#include "operator_api/operator.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
/**
 * @brief Selects a string from an input string lane array by index.
 *
 * Picks one string from an input string lane array at the given index.
 * Optional wrap mode cycles the index; otherwise it clamps.
 *
 * @see FolderList, Basename, Stack
 */
struct StringSelect : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "StringSelect";
    static constexpr bool kTimeDependent = false;

    vivid::Param<bool> wrap{"wrap", true};

    StringSelect() {
        vivid::semantic_tag(wrap, "enabled");
        vivid::semantic_shape(wrap, "bool");
        vivid::description(wrap, "Wrap index around the list instead of clamping");
    }

    std::string selected_;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&wrap);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"files", VIVID_PORT_STRING_LANES, VIVID_PORT_INPUT});
        out.push_back({"index", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"file", VIVID_PORT_STRING, VIVID_PORT_OUTPUT});
        out.push_back({"valid", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"resolved_index", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        selected_.clear();
        int resolved = -1;
        bool valid = false;

        const VividStringLanePort* in_lanes =
            ctx->input_string_lanes ? &ctx->input_string_lanes[0] : nullptr;
        const uint32_t n = (in_lanes && in_lanes->data) ? in_lanes->length : 0;
        if (n > 0) {
            int idx = static_cast<int>(std::floor(ctx->input_values[1]));
            if (wrap.bool_value()) {
                int m = static_cast<int>(n);
                idx = ((idx % m) + m) % m;
            } else {
                idx = std::max(0, std::min(idx, static_cast<int>(n) - 1));
            }
            const char* s = in_lanes->data[idx];
            if (s && *s) {
                selected_ = s;
                resolved = idx;
                valid = true;
            }
        }

        if (ctx->output_string_values) {
            ctx->output_string_values[0] = selected_.c_str();
        }
        if (ctx->output_values) {
            ctx->output_values[1] = valid ? 1.0f : 0.0f;
            ctx->output_values[2] = static_cast<float>(std::max(0, resolved));
        }
    }
};

VIVID_REGISTER(StringSelect)
