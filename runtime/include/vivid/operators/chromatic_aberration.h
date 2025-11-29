#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief RGB channel separation for VHS/glitch aesthetic.
 *
 * Separates R, G, B channels with offset for chromatic aberration effect.
 *
 * Modes:
 *   0 = Directional (channels offset along angle)
 *   1 = Radial (channels offset from center)
 *   2 = Barrel (lens distortion style)
 *
 * Example:
 * @code
 * ChromaticAberration chroma_;
 * chroma_.input("video").amount(0.02f).mode(1);
 * @endcode
 */
class ChromaticAberration : public Operator {
public:
    ChromaticAberration() = default;

    /// Set input texture from another operator
    ChromaticAberration& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set separation amount (0.0 to 0.1)
    ChromaticAberration& amount(float a) { amount_ = a; return *this; }
    /// Set angle for directional mode (radians)
    ChromaticAberration& angle(float a) { angle_ = a; return *this; }
    /// Set mode: 0=directional, 1=radial, 2=barrel
    ChromaticAberration& mode(int m) { mode_ = m; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_);
        if (!input || !input->valid()) return;

        if (output_.width != input->width || output_.height != input->height) {
            output_ = ctx.createTexture(input->width, input->height);
        }

        Context::ShaderParams params;
        params.param0 = amount_;
        params.param1 = angle_;
        params.mode = mode_;

        ctx.runShader("shaders/chromatic_aberration.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    void cleanup() override {
        output_ = Texture{};
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("amount", amount_, 0.0f, 0.1f),
            floatParam("angle", angle_, 0.0f, 6.28f),
            intParam("mode", mode_, 0, 2)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    Texture output_;
    float amount_ = 0.01f;
    float angle_ = 0.0f;
    int mode_ = 0;
};

} // namespace vivid
