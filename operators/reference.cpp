// Reference Operator
// References another operator's output by name
// Useful for creating multiple references to the same source

#include <vivid/vivid.h>

using namespace vivid;

class Reference : public Operator {
public:
    Reference() = default;
    explicit Reference(const std::string& sourceNode) : sourceNode_(sourceNode) {}

    // Fluent API
    Reference& source(const std::string& node) { sourceNode_ = node; return *this; }
    Reference& output(const std::string& name) { outputName_ = name; return *this; }

    void init(Context& ctx) override {
        // We'll create the output texture lazily when we know the source dimensions
    }

    void process(Context& ctx) override {
        // Try to get texture first
        Texture* sourceTex = ctx.getInputTexture(sourceNode_, outputName_);
        if (sourceTex && sourceTex->valid()) {
            // Create output matching source if needed
            if (!output_.valid() ||
                output_.width != sourceTex->width ||
                output_.height != sourceTex->height) {
                output_ = ctx.createTextureMatching(*sourceTex);
            }

            Context::ShaderParams params;
            ctx.runShader("shaders/passthrough.wgsl", sourceTex, output_, params);
            ctx.setOutput("out", output_);
            outputsTexture_ = true;
            return;
        }

        // Try to get value
        float value = ctx.getInputValue(sourceNode_, outputName_, 0.0f);
        ctx.setOutput("out", value);
        outputsTexture_ = false;
    }

    std::vector<ParamDecl> params() override {
        return {
            stringParam("source", sourceNode_),
            stringParam("output", outputName_)
        };
    }

    OutputKind outputKind() override {
        return outputsTexture_ ? OutputKind::Texture : OutputKind::Value;
    }

private:
    std::string sourceNode_;
    std::string outputName_ = "out";
    Texture output_;
    bool outputsTexture_ = true;
};

VIVID_OPERATOR(Reference)
