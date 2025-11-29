#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief HSV color adjustment.
 *
 * Adjusts hue, saturation, and value of an input texture.
 *
 * Example:
 * @code
 * HSVAdjust hsv_;
 * hsv_.input("image").hueShift(0.5f).saturation(1.2f);
 * @endcode
 */
class HSVAdjust : public Operator {
public:
    HSVAdjust() = default;
    explicit HSVAdjust(const std::string& inputNode) : inputNode_(inputNode) {}

    /// Set input texture from another operator
    HSVAdjust& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set hue shift (-1.0 to 1.0, wraps around)
    HSVAdjust& hueShift(float h) { hueShift_ = h; return *this; }
    /// Set saturation multiplier (1.0 = unchanged) or absolute value in colorize mode
    HSVAdjust& saturation(float s) { saturation_ = s; return *this; }
    /// Set value/brightness multiplier (1.0 = unchanged)
    HSVAdjust& value(float v) { value_ = v; return *this; }
    /// Alias for value()
    HSVAdjust& brightness(float b) { value_ = b; return *this; }
    /// Enable colorize mode (sets saturation absolutely, good for grayscale input)
    HSVAdjust& colorize(bool c = true) { colorize_ = c; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        Context::ShaderParams params;
        params.param0 = hueShift_;
        params.param1 = saturation_;
        params.param2 = value_;
        params.mode = colorize_ ? 1 : 0;

        ctx.runShader("shaders/hsv.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("hueShift", hueShift_, -1.0f, 1.0f),
            floatParam("saturation", saturation_, 0.0f, 3.0f),
            floatParam("value", value_, 0.0f, 3.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float hueShift_ = 0.0f;
    float saturation_ = 1.0f;
    float value_ = 1.0f;
    bool colorize_ = false;
    Texture output_;
};

// Convenient alias
using HSV = HSVAdjust;

} // namespace vivid
