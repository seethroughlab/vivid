#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"

namespace vivid {

/**
 * @brief SDF-based shape generator.
 *
 * Generates basic shapes using Signed Distance Fields.
 *
 * Shape types:
 * - Circle (0)
 * - Rect (1)
 * - Triangle (2)
 * - Line (3)
 * - Ring (4)
 * - Star (5)
 *
 * Example:
 * @code
 * Shape circle_;
 * circle_.type(Shape::Circle).radius(0.3f).color({1,0,0});
 * @endcode
 */
class Shape : public Operator {
public:
    enum Type { Circle = 0, Rect = 1, Triangle = 2, Line = 3, Ring = 4, Star = 5 };

    Shape() = default;
    explicit Shape(Type type) : type_(type) {}

    /// Set shape type
    Shape& type(Type t) { type_ = t; return *this; }
    /// Set center position (0-1 normalized)
    Shape& center(glm::vec2 c) { center_ = c; return *this; }
    /// Set size (for rect/triangle)
    Shape& size(glm::vec2 s) { size_ = s; return *this; }
    /// Set radius (for circle/ring/star)
    Shape& radius(float r) { radius_ = r; return *this; }
    /// Set inner radius (for ring)
    Shape& innerRadius(float r) { innerRadius_ = r; return *this; }
    /// Set rotation in radians
    Shape& rotation(float r) { rotation_ = r; return *this; }
    /// Set stroke width (0 = filled)
    Shape& strokeWidth(float w) { strokeWidth_ = w; return *this; }
    /// Set fill color
    Shape& color(glm::vec3 c) { color_ = c; return *this; }
    /// Set edge softness/antialiasing
    Shape& softness(float s) { softness_ = s; return *this; }
    /// Set number of points (for star)
    Shape& points(int p) { points_ = p; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Context::ShaderParams params;
        params.mode = static_cast<int>(type_);
        params.vec0X = center_.x;
        params.vec0Y = center_.y;
        params.vec1X = size_.x;
        params.vec1Y = size_.y;
        params.param0 = radius_;

        if (type_ == Ring) {
            params.param1 = innerRadius_;
        } else if (type_ == Star) {
            params.param1 = static_cast<float>(points_);
        } else {
            params.param1 = rotation_;
        }

        params.param2 = strokeWidth_;
        params.param3 = color_.r;
        params.param4 = color_.g;
        params.param5 = color_.b;
        params.param6 = softness_;

        float aspect = static_cast<float>(output_.width) / static_cast<float>(output_.height);
        params.param7 = aspect;

        ctx.runShader("shaders/shape.wgsl", nullptr, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("type", static_cast<int>(type_), 0, 5),
            vec2Param("center", center_),
            vec2Param("size", size_),
            floatParam("radius", radius_, 0.0f, 1.0f),
            floatParam("innerRadius", innerRadius_, 0.0f, 1.0f),
            floatParam("rotation", rotation_, -3.14159f, 3.14159f),
            floatParam("strokeWidth", strokeWidth_, 0.0f, 0.1f),
            colorParam("color", color_),
            floatParam("softness", softness_, 0.001f, 0.1f),
            intParam("points", points_, 3, 12)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    Type type_ = Circle;
    glm::vec2 center_ = glm::vec2(0.5f, 0.5f);
    glm::vec2 size_ = glm::vec2(0.3f, 0.3f);
    float radius_ = 0.2f;
    float innerRadius_ = 0.1f;
    float rotation_ = 0.0f;
    float strokeWidth_ = 0.0f;
    glm::vec3 color_ = glm::vec3(1.0f, 1.0f, 1.0f);
    float softness_ = 0.005f;
    int points_ = 5;
    Texture output_;
};

} // namespace vivid
