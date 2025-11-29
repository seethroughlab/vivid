#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief 2D texture transformation.
 *
 * Applies translate, scale, and rotate transformations to an input texture.
 *
 * Example:
 * @code
 * Transform xform_;
 * xform_.input("image").scale(0.5f).rotate(0.785f).translate({0.25f, 0.0f});
 * @endcode
 */
class Transform : public Operator {
public:
    Transform() = default;
    explicit Transform(const std::string& inputNode) : inputNode_(inputNode) {}

    /// Set input texture from another operator
    Transform& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set translation
    Transform& translate(glm::vec2 t) { translate_ = t; return *this; }
    /// Set non-uniform scale
    Transform& scale(glm::vec2 s) { scale_ = s; return *this; }
    /// Set uniform scale
    Transform& scale(float s) { scale_ = glm::vec2(s); return *this; }
    /// Set rotation in radians
    Transform& rotate(float r) { rotate_ = r; return *this; }
    /// Set pivot point for rotation/scale (default: center)
    Transform& pivot(glm::vec2 p) { pivot_ = p; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        Context::ShaderParams params;
        params.vec0X = translate_.x;
        params.vec0Y = translate_.y;
        params.vec1X = scale_.x;
        params.vec1Y = scale_.y;
        params.param0 = rotate_;
        params.param1 = pivot_.x;
        params.param2 = pivot_.y;

        ctx.runShader("shaders/transform.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            vec2Param("translate", translate_),
            vec2Param("scale", scale_),
            floatParam("rotate", rotate_, -3.14159f, 3.14159f),
            vec2Param("pivot", pivot_)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    glm::vec2 translate_ = glm::vec2(0.0f);
    glm::vec2 scale_ = glm::vec2(1.0f);
    float rotate_ = 0.0f;
    glm::vec2 pivot_ = glm::vec2(0.5f);
    Texture output_;
};

} // namespace vivid
