#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Gaussian blur effect.
 *
 * Applies separable Gaussian blur with configurable radius and passes.
 *
 * Example:
 * @code
 * Blur blur_;
 * blur_.input("noise").radius(5.0f).passes(2);
 * @endcode
 */
class Blur : public Operator {
public:
    Blur() = default;
    explicit Blur(const std::string& inputNode) : inputNode_(inputNode) {}

    /// Set input texture from another operator
    Blur& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set blur radius in pixels
    Blur& radius(float r) { radius_ = r; return *this; }
    /// Set number of blur passes (more = stronger blur)
    Blur& passes(int p) { passes_ = p; return *this; }

    void init(Context& ctx) override {
        temp_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        for (int i = 0; i < passes_; i++) {
            Texture* src = (i == 0) ? input : &output_;

            // Horizontal pass
            Context::ShaderParams hParams;
            hParams.param0 = radius_;
            hParams.vec0X = 1.0f;
            hParams.vec0Y = 0.0f;
            ctx.runShader("shaders/blur.wgsl", src, temp_, hParams);

            // Vertical pass
            Context::ShaderParams vParams;
            vParams.param0 = radius_;
            vParams.vec0X = 0.0f;
            vParams.vec0Y = 1.0f;
            ctx.runShader("shaders/blur.wgsl", &temp_, output_, vParams);
        }

        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("radius", radius_, 0.0f, 50.0f),
            intParam("passes", passes_, 1, 5)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float radius_ = 5.0f;
    int passes_ = 1;
    Texture temp_;
    Texture output_;
};

} // namespace vivid
