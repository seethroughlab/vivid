#pragma once

#include "vivid/operator.h"
#include <glm/glm.hpp>

namespace vivid {

enum class GradientType {
    Linear = 0,
    Radial = 1,
    Angular = 2,
    Diamond = 3
};

/// Generates gradient textures
class Gradient : public TextureOperator {
public:
    std::string typeName() const override { return "Gradient"; }

    std::vector<ParamDecl> params() override {
        return {
            intParam("type", 0, 0, 3),
            floatParam("angle", 0.0f, 0.0f, 360.0f),
            floatParam("centerX", 0.5f, 0.0f, 1.0f),
            floatParam("centerY", 0.5f, 0.0f, 1.0f),
            floatParam("scale", 1.0f, 0.1f, 10.0f)
        };
    }

    // Fluent API
    Gradient& type(GradientType t) { type_ = t; return *this; }
    Gradient& angle(float a) { angle_ = a; return *this; }
    Gradient& center(float x, float y) { centerX_ = x; centerY_ = y; return *this; }
    Gradient& scale(float s) { scale_ = s; return *this; }
    Gradient& colorA(const glm::vec4& c) { colorA_ = c; return *this; }
    Gradient& colorB(const glm::vec4& c) { colorB_ = c; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    GradientType type_ = GradientType::Linear;
    float angle_ = 0.0f;
    float centerX_ = 0.5f;
    float centerY_ = 0.5f;
    float scale_ = 1.0f;
    glm::vec4 colorA_ = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 colorB_ = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    struct Constants {
        float colorA[4];
        float colorB[4];
        int type;
        float angle;
        float centerX;
        float centerY;
        float scale;
        float padding0;
        float padding1;
        float padding2;
    };
};

} // namespace vivid
