#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief CRT-style scanline effect.
 *
 * Adds horizontal scanlines for retro monitor aesthetic.
 *
 * Modes:
 *   0 = Simple (uniform lines)
 *   1 = Alternating (every other line)
 *   2 = RGB sub-pixel (authentic CRT phosphor simulation)
 *
 * Example:
 * @code
 * Scanlines scan_;
 * scan_.input("video").density(400.0f).intensity(0.3f).mode(2);
 * @endcode
 */
class Scanlines : public Operator {
public:
    Scanlines() = default;

    /// Set input texture from another operator
    Scanlines& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set line density (lines per screen height)
    Scanlines& density(float d) { density_ = d; return *this; }
    /// Set line darkness intensity (0.0 to 1.0)
    Scanlines& intensity(float i) { intensity_ = i; return *this; }
    /// Set scroll speed for animated lines
    Scanlines& scrollSpeed(float s) { scrollSpeed_ = s; return *this; }
    /// Set mode: 0=simple, 1=alternating, 2=RGB
    Scanlines& mode(int m) { mode_ = m; return *this; }

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
        params.param0 = density_;
        params.param1 = intensity_;
        params.param2 = scrollSpeed_;
        params.mode = mode_;

        ctx.runShader("shaders/scanlines.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    void cleanup() override {
        output_ = Texture{};
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("density", density_, 100.0f, 1000.0f),
            floatParam("intensity", intensity_, 0.0f, 1.0f),
            floatParam("scrollSpeed", scrollSpeed_, 0.0f, 100.0f),
            intParam("mode", mode_, 0, 2)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    Texture output_;
    float density_ = 400.0f;
    float intensity_ = 0.25f;
    float scrollSpeed_ = 0.0f;
    int mode_ = 0;
};

} // namespace vivid
