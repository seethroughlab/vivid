#include "operator_api/operator.h"

#include <string>

struct Basename : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "Basename";
    static constexpr bool kTimeDependent = false;

    std::string result_;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"path", VIVID_PORT_STRING, VIVID_PORT_INPUT});
        out.push_back({"name", VIVID_PORT_STRING, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        result_.clear();

        if (ctx->input_string_values) {
            const char* s = ctx->input_string_values[0];
            if (s && *s) {
                std::string path(s);
                auto slash = path.rfind('/');
                if (slash != std::string::npos)
                    result_ = path.substr(slash + 1);
                else
                    result_ = path;
            }
        }

        if (ctx->output_string_values) {
            ctx->output_string_values[0] = result_.c_str();
        }
    }
};

VIVID_REGISTER(Basename)
