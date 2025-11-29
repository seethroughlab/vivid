#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Vignette post-processing effect.
 *
 * Darkens the edges of the image to create a subtle framing effect.
 * Commonly used in film and photography to draw attention to the center.
 *
 * Example:
 * @code
 * chain.add<Vignette>("vignette")
 *     .input("scene")
 *     .intensity(0.5f)
 *     .radius(0.8f)
 *     .softness(0.5f);
 * @endcode
 */
class Vignette : public Operator {
public:
    Vignette() = default;
    explicit Vignette(const std::string& inputNode) : inputNode_(inputNode) {}

    /// Set input texture from another operator
    Vignette& input(const std::string& node) { inputNode_ = node; return *this; }

    /// Set vignette intensity (0-2, default 0.5). Higher = darker edges.
    Vignette& intensity(float i) { intensity_ = i; return *this; }

    /// Set vignette radius (0-2, default 0.8). Lower = larger vignette area.
    Vignette& radius(float r) { radius_ = r; return *this; }

    /// Set falloff softness (0-2, default 0.5). Higher = smoother transition.
    Vignette& softness(float s) { softness_ = s; return *this; }

    /// Set center offset X (-1 to 1, default 0). Move vignette center horizontally.
    Vignette& centerX(float x) { centerX_ = x; return *this; }

    /// Set center offset Y (-1 to 1, default 0). Move vignette center vertically.
    Vignette& centerY(float y) { centerY_ = y; return *this; }

    /// Set center offset (shorthand for centerX and centerY)
    Vignette& center(float x, float y) { centerX_ = x; centerY_ = y; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");
        if (!input) return;

        Context::ShaderParams params;
        params.param0 = intensity_;
        params.param1 = radius_;
        params.param2 = softness_;
        params.vec0X = centerX_;
        params.vec0Y = centerY_;

        ctx.runShader("shaders/vignette.wgsl", input, output_, params);

        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("intensity", intensity_, 0.0f, 2.0f),
            floatParam("radius", radius_, 0.0f, 2.0f),
            floatParam("softness", softness_, 0.0f, 2.0f),
            floatParam("centerX", centerX_, -1.0f, 1.0f),
            floatParam("centerY", centerY_, -1.0f, 1.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float intensity_ = 0.5f;
    float radius_ = 0.8f;
    float softness_ = 0.5f;
    float centerX_ = 0.0f;
    float centerY_ = 0.0f;

    Texture output_;
};

} // namespace vivid
