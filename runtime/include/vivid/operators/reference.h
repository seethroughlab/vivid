#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Reference to another operator's output.
 *
 * References another operator's output by name. Useful for creating
 * multiple references to the same source without re-computing.
 *
 * Example:
 * @code
 * Reference ref_;
 * ref_.source("video").output("out");  // Reference video.out
 * @endcode
 */
class Reference : public Operator {
public:
    Reference() = default;
    explicit Reference(const std::string& sourceNode) : sourceNode_(sourceNode) {}

    /// Set source operator name
    Reference& source(const std::string& node) { sourceNode_ = node; return *this; }
    /// Set output name to reference (default: "out")
    Reference& output(const std::string& name) { outputName_ = name; return *this; }

    void init(Context& ctx) override {}

    void process(Context& ctx) override {
        // Try to get texture first
        Texture* sourceTex = ctx.getInputTexture(sourceNode_, outputName_);
        if (sourceTex && sourceTex->valid()) {
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

        // Fall back to value
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

} // namespace vivid
