#pragma once

#include "vivid/operator.h"
#include <glm/glm.hpp>

namespace vivid {

enum class ShapeType {
    Circle = 0,
    Rectangle = 1,
    Triangle = 2,
    Line = 3,
    Ring = 4,
    Star = 5
};

/// SDF-based shape generator
class Shape : public TextureOperator {
public:
    std::string typeName() const override { return "Shape"; }

    std::vector<ParamDecl> params() override {
        return {
            intParam("type", 0, 0, 5),
            floatParam("centerX", 0.5f, 0.0f, 1.0f),
            floatParam("centerY", 0.5f, 0.0f, 1.0f),
            floatParam("radius", 0.2f, 0.0f, 1.0f),
            floatParam("innerRadius", 0.1f, 0.0f, 1.0f),
            floatParam("width", 0.3f, 0.0f, 1.0f),
            floatParam("height", 0.3f, 0.0f, 1.0f),
            floatParam("rotation", 0.0f, -3.14159f, 3.14159f),
            floatParam("softness", 0.005f, 0.001f, 0.1f),
            intParam("points", 5, 3, 12)
        };
    }

    // Fluent API
    Shape& type(ShapeType t) { type_ = t; return *this; }
    Shape& center(float x, float y) { centerX_ = x; centerY_ = y; return *this; }
    Shape& radius(float r) { radius_ = r; return *this; }
    Shape& innerRadius(float r) { innerRadius_ = r; return *this; }
    Shape& size(float w, float h) { width_ = w; height_ = h; return *this; }
    Shape& rotation(float r) { rotation_ = r; return *this; }
    Shape& softness(float s) { softness_ = s; return *this; }
    Shape& points(int p) { points_ = p; return *this; }
    Shape& color(const glm::vec3& c) { color_ = c; return *this; }
    Shape& backgroundColor(const glm::vec4& c) { bgColor_ = c; return *this; }

    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    ShapeType type_ = ShapeType::Circle;
    float centerX_ = 0.5f;
    float centerY_ = 0.5f;
    float radius_ = 0.2f;
    float innerRadius_ = 0.1f;
    float width_ = 0.3f;
    float height_ = 0.3f;
    float rotation_ = 0.0f;
    float softness_ = 0.005f;
    int points_ = 5;
    glm::vec3 color_ = glm::vec3(1.0f, 1.0f, 1.0f);
    glm::vec4 bgColor_ = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    struct Constants {
        float centerX;
        float centerY;
        float radius;
        float innerRadius;
        float width;
        float height;
        float rotation;
        float softness;
        float colorR;
        float colorG;
        float colorB;
        int shapeType;
        int points;
        float aspectRatio;
        float bgColorR;
        float bgColorG;
        float bgColorB;
        float bgColorA;
        float padding0;
        float padding1;
    };
};

} // namespace vivid
