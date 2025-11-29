#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Edge detection using Sobel operator.
 *
 * Modes:
 * - 0: Edges only
 * - 1: Edges overlaid on original
 * - 2: Inverted edges
 *
 * Example:
 * @code
 * Edge edge_;
 * edge_.input("image").threshold(0.1f).thickness(1.0f);
 * @endcode
 */
class Edge : public Operator {
public:
    Edge() = default;
    explicit Edge(const std::string& inputNode) : inputNode_(inputNode) {}

    /// Set input texture
    Edge& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set edge detection threshold
    Edge& threshold(float t) { threshold_ = t; return *this; }
    /// Set edge thickness
    Edge& thickness(float t) { thickness_ = t; return *this; }
    /// Set output mode (0=edges only, 1=overlay, 2=inverted)
    Edge& mode(int m) { mode_ = m; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        Context::ShaderParams params;
        params.param0 = threshold_;
        params.param1 = thickness_;
        params.mode = mode_;

        ctx.runShader("shaders/edge.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("threshold", threshold_, 0.0f, 1.0f),
            floatParam("thickness", thickness_, 0.5f, 5.0f),
            intParam("mode", mode_, 0, 2)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float threshold_ = 0.1f;
    float thickness_ = 1.0f;
    int mode_ = 0;
    Texture output_;
};

} // namespace vivid
