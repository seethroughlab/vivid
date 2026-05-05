#include "operator_api/operator.h"

struct FileDropTestOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "FileDropTestOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::FilePath> file{"file"};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (ctx && ctx->output_values) ctx->output_values[0] = file.str_value.empty() ? 0.0f : 1.0f;
    }
};

static const char* kFileDropTestOpExts[] = {".dropx"};
static const VividFileDropHandlerDescriptor kFileDropTestOpHandlers[] = {{
    "Create Test Asset Node",
    kFileDropTestOpExts,
    1,
    "file",
    100,
    "Test fixture for file-drop metadata.",
}};

VIVID_FILE_DROP(kFileDropTestOpHandlers)
