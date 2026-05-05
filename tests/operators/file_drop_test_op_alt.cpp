#include "operator_api/operator.h"

struct FileDropTestOpAlt : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "FileDropTestOpAlt";
    static constexpr bool kTimeDependent = false;

    vivid::Param<vivid::FilePath> file{"file"};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (ctx && ctx->output_values) ctx->output_values[0] = file.str_value.empty() ? 0.0f : 2.0f;
    }
};

static const char* kFileDropTestOpAltExts[] = {".dropx"};
static const VividFileDropHandlerDescriptor kFileDropTestOpAltHandlers[] = {{
    "Create Alternate Test Asset Node",
    kFileDropTestOpAltExts,
    1,
    "file",
    50,
    "Alternate test fixture for file-drop metadata.",
}};

VIVID_FILE_DROP(kFileDropTestOpAltHandlers)
