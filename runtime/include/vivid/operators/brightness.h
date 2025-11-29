#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Brightness and contrast adjustment.
 *
 * Adjusts brightness and contrast of an input texture.
 * Brightness can be driven by another operator's value.
 *
 * Example:
 * @code
 * Brightness bright_;
 * bright_.input("video").amount(1.2f).contrast(1.1f);
 *
 * // Or drive brightness from an LFO:
 * bright_.input("video").amount("lfo");
 * @endcode
 */
class Brightness : public Operator {
public:
    Brightness() = default;
    explicit Brightness(const std::string& inputNode) : inputNode_(inputNode) {}

    /// Set input texture from another operator
    Brightness& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set brightness amount (1.0 = unchanged)
    Brightness& amount(float a) { amount_ = a; useNode_ = false; return *this; }
    /// Set brightness from another operator's output value
    Brightness& amount(const std::string& node) { amountNode_ = node; useNode_ = true; return *this; }
    /// Set contrast (1.0 = unchanged)
    Brightness& contrast(float c) { contrast_ = c; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        float finalAmount = amount_;
        if (useNode_ && !amountNode_.empty()) {
            finalAmount = ctx.getInputValue(amountNode_, "out", amount_);
        }

        Context::ShaderParams params;
        params.param0 = finalAmount;
        params.param1 = contrast_;

        ctx.runShader("shaders/brightness.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("amount", amount_, -1.0f, 2.0f),
            floatParam("contrast", contrast_, 0.0f, 3.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    std::string amountNode_;
    bool useNode_ = false;
    float amount_ = 1.0f;
    float contrast_ = 1.0f;
    Texture output_;
};

} // namespace vivid
