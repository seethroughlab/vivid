// Shape Operator
// Generates basic shapes using Signed Distance Fields (SDF)

#include <vivid/vivid.h>
#include <glm/glm.hpp>

using namespace vivid;

class Shape : public Operator {
public:
    // Shape types
    enum Type { Circle = 0, Rect = 1, Triangle = 2, Line = 3, Ring = 4, Star = 5 };

    Shape() = default;
    explicit Shape(Type type) : type_(type) {}

    // Fluent API
    Shape& type(Type t) { type_ = t; return *this; }
    Shape& center(glm::vec2 c) { center_ = c; return *this; }
    Shape& size(glm::vec2 s) { size_ = s; return *this; }
    Shape& radius(float r) { radius_ = r; return *this; }
    Shape& innerRadius(float r) { innerRadius_ = r; return *this; }
    Shape& rotation(float r) { rotation_ = r; return *this; }
    Shape& strokeWidth(float w) { strokeWidth_ = w; return *this; }
    Shape& color(glm::vec3 c) { color_ = c; return *this; }
    Shape& softness(float s) { softness_ = s; return *this; }
    Shape& points(int p) { points_ = p; return *this; }  // For star

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

        // param1 depends on shape type
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

        // Calculate aspect ratio from output texture
        float aspect = static_cast<float>(output_.width()) / static_cast<float>(output_.height());
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
    float strokeWidth_ = 0.0f;  // 0 = filled
    glm::vec3 color_ = glm::vec3(1.0f, 1.0f, 1.0f);
    float softness_ = 0.005f;
    int points_ = 5;  // For star
    Texture output_;
};

VIVID_OPERATOR(Shape)
