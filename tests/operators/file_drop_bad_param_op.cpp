#include "operator_api/operator.h"

struct FileDropBadParamOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "FileDropBadParamOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 2.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (ctx && ctx->output_values) ctx->output_values[0] = gain.value;
    }
};

static const char* kFileDropBadParamExts[] = {".dropbad"};
static const VividFileDropHandlerDescriptor kFileDropBadParamHandlers[] = {{
    "Broken Test Asset",
    kFileDropBadParamExts,
    1,
    "missing_file_param",
    10,
    "Invalid file-drop metadata fixture.",
}};

VIVID_FILE_DROP(kFileDropBadParamHandlers)
