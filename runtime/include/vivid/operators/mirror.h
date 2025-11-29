#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Mirror and Kaleidoscope effects.
 *
 * Creates mirror reflections and kaleidoscope patterns from input.
 *
 * Modes:
 *   0 = Horizontal mirror (left half reflects to right)
 *   1 = Vertical mirror (top half reflects to bottom)
 *   2 = Quad mirror (top-left corner reflects to all quadrants)
 *   3 = Kaleidoscope (radial symmetry with configurable segments)
 *
 * Example:
 * @code
 * Mirror mirror_;
 * mirror_.input("webcam").kaleidoscope(8).rotation(0.5f);
 * @endcode
 */
class Mirror : public Operator {
public:
    Mirror() = default;

    /// Set input texture from another operator
    Mirror& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set mode: 0=horizontal, 1=vertical, 2=quad, 3=kaleidoscope
    Mirror& mode(int m) { mode_ = m; return *this; }
    /// Horizontal mirror (left reflects to right)
    Mirror& horizontal() { mode_ = 0; return *this; }
    /// Vertical mirror (top reflects to bottom)
    Mirror& vertical() { mode_ = 1; return *this; }
    /// Quad mirror (top-left to all corners)
    Mirror& quad() { mode_ = 2; return *this; }
    /// Kaleidoscope mode with specified segments
    Mirror& kaleidoscope(int segs = 6) { mode_ = 3; segments_ = static_cast<float>(segs); return *this; }
    /// Number of segments for kaleidoscope (default 6)
    Mirror& segments(float s) { segments_ = s; return *this; }
    /// Rotation angle in radians
    Mirror& rotation(float r) { rotation_ = r; return *this; }
    /// Center point for kaleidoscope (0-1 range)
    Mirror& center(float x, float y) { centerX_ = x; centerY_ = y; return *this; }

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
        params.mode = mode_;
        params.param0 = segments_;
        params.param1 = rotation_;
        params.param2 = centerX_;
        params.param3 = centerY_;

        ctx.runShader("shaders/mirror.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    void cleanup() override {
        output_ = Texture{};
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("mode", mode_, 0, 3),
            floatParam("segments", segments_, 2.0f, 16.0f),
            floatParam("rotation", rotation_, -3.14159f, 3.14159f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    Texture output_;
    int mode_ = 0;
    float segments_ = 6.0f;
    float rotation_ = 0.0f;
    float centerX_ = 0.5f;
    float centerY_ = 0.5f;
};

} // namespace vivid
