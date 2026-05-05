// Fixture operator for Phase 1 editor ABI tests. Exercises VIVID_EDITOR symbol
// discovery and editor_metadata round-trip. draw_editor is intentionally a
// no-op because Phase 1 only validates the loader plumbing, not rendering.
#include "operator_api/operator.h"

struct EditorTestOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "EditorTestOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> value{"value", 0.0f, 0.0f, 1.0f};
    vivid::Param<vivid::FilePath> file_path{"file_path", ""};
    vivid::Param<vivid::TextValue> note{"note", ""};
    bool saw_editor_strings_ = false;
    bool editor_string_order_ok_ = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&value);
        out.push_back(&file_path);
        out.push_back(&note);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        (void)ctx;
        ctx->output_values[0] =
            (saw_editor_strings_ && editor_string_order_ok_) ? 1.0f : 0.0f;
    }

    static VividEditorMetadata editor_metadata() {
        return VividEditorMetadata{
            /*default_width*/  900,
            /*default_height*/ 520,
            /*min_width*/      640,
            /*min_height*/     360,
            /*title_suffix*/   "Editor Fixture",
        };
    }

    void draw_editor(VividEditorContext* ctx) {
        saw_editor_strings_ = ctx && ctx->string_param_values != nullptr &&
            ctx->string_param_count == 2;
        editor_string_order_ok_ = saw_editor_strings_ &&
            std::strcmp(ctx->string_param_values[0], "fixture.mov") == 0 &&
            std::strcmp(ctx->string_param_values[1], "hello editor") == 0;
    }
};

VIVID_EDITOR(EditorTestOp)
