#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"

namespace vivid {

/**
 * @brief Constant value or color generator.
 *
 * Generates a solid color texture or outputs a fixed numeric value.
 *
 * Example:
 * @code
 * Constant bg_;
 * bg_.color(0.1f, 0.1f, 0.1f);  // Dark gray background
 *
 * Constant val_;
 * val_.value(0.5f);  // Fixed value
 * @endcode
 */
class Constant : public Operator {
public:
    Constant() = default;

    /// Set RGBA color (outputs texture)
    Constant& color(float r, float g, float b, float a = 1.0f) {
        color_ = glm::vec4(r, g, b, a);
        outputTexture_ = true;
        return *this;
    }
    /// Set RGB color (outputs texture)
    Constant& color(const glm::vec3& c) {
        color_ = glm::vec4(c, 1.0f);
        outputTexture_ = true;
        return *this;
    }
    /// Set RGBA color (outputs texture)
    Constant& color(const glm::vec4& c) {
        color_ = c;
        outputTexture_ = true;
        return *this;
    }
    /// Set numeric value (outputs value)
    Constant& value(float v) {
        value_ = v;
        outputTexture_ = false;
        return *this;
    }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        if (outputTexture_) {
            Context::ShaderParams params;
            params.param0 = color_.r;
            params.param1 = color_.g;
            params.param2 = color_.b;
            params.param3 = color_.a;
            ctx.runShader("shaders/constant.wgsl", nullptr, output_, params);
            ctx.setOutput("out", output_);
        } else {
            ctx.setOutput("out", value_);
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            colorParam("color", glm::vec3(color_)),
            floatParam("value", value_, -1000.0f, 1000.0f)
        };
    }

    OutputKind outputKind() override {
        return outputTexture_ ? OutputKind::Texture : OutputKind::Value;
    }

private:
    glm::vec4 color_{1.0f, 1.0f, 1.0f, 1.0f};
    float value_ = 0.0f;
    bool outputTexture_ = true;
    Texture output_;
};

} // namespace vivid
