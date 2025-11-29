#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Mosaic/pixelation effect.
 *
 * Reduces effective resolution by sampling blocks.
 *
 * Modes:
 *   0 = Square blocks
 *   1 = Aspect-corrected blocks
 *
 * Example:
 * @code
 * Pixelate pix_;
 * pix_.input("video").size(8.0f);
 * @endcode
 */
class Pixelate : public Operator {
public:
    Pixelate() = default;

    /// Set input texture from another operator
    Pixelate& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set block size in pixels
    Pixelate& size(float s) { size_ = s; return *this; }
    /// Set mode: 0=square, 1=aspect-corrected
    Pixelate& mode(int m) { mode_ = m; return *this; }

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
        params.param0 = size_;
        params.mode = mode_;

        ctx.runShader("shaders/pixelate.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    void cleanup() override {
        output_ = Texture{};
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("size", size_, 1.0f, 64.0f),
            intParam("mode", mode_, 0, 1)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    Texture output_;
    float size_ = 4.0f;
    int mode_ = 0;
};

} // namespace vivid
