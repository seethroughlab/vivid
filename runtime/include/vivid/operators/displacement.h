#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Texture displacement effect.
 *
 * Distorts a texture using a displacement map.
 *
 * Channel modes:
 * - 0: Luminance
 * - 1: Red channel
 * - 2: Green channel
 * - 3: Red/Green (X/Y displacement)
 *
 * Example:
 * @code
 * Displacement disp_;
 * disp_.input("image").map("noise").amount(0.1f).channel(0);
 * @endcode
 */
class Displacement : public Operator {
public:
    Displacement() = default;
    explicit Displacement(const std::string& inputNode) : inputNode_(inputNode) {}

    /// Set input texture to displace
    Displacement& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set displacement map texture
    Displacement& map(const std::string& node) { mapNode_ = node; return *this; }
    /// Set displacement amount
    Displacement& amount(float a) { amount_ = a; return *this; }
    /// Set channel mode (0=lum, 1=R, 2=G, 3=RG)
    Displacement& channel(int c) { channel_ = c; return *this; }
    /// Set displacement direction
    Displacement& direction(glm::vec2 d) { direction_ = d; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        Context::ShaderParams params;
        params.param0 = amount_;
        params.param1 = static_cast<float>(channel_);
        params.vec0X = direction_.x;
        params.vec0Y = direction_.y;

        ctx.runShader("shaders/displacement.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("amount", amount_, 0.0f, 0.5f),
            intParam("channel", channel_, 0, 3),
            vec2Param("direction", direction_)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    std::string mapNode_;
    float amount_ = 0.1f;
    int channel_ = 0;
    glm::vec2 direction_ = glm::vec2(1.0f, 1.0f);
    Texture output_;
};

} // namespace vivid
