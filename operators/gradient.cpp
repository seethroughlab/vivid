// Gradient Generator Operator
// Generates color gradients (linear, radial, angular, diamond)

#include <vivid/vivid.h>
#include <glm/glm.hpp>

using namespace vivid;

class Gradient : public Operator {
public:
    Gradient() = default;

    // Fluent API
    Gradient& mode(int m) { mode_ = m; return *this; }  // 0=linear, 1=radial, 2=angular, 3=diamond, 4=animated
    Gradient& angle(float a) { angle_ = a; return *this; }
    Gradient& offset(float o) { offset_ = o; return *this; }
    Gradient& scale(float s) { scale_ = s; return *this; }
    Gradient& center(glm::vec2 c) { center_ = c; return *this; }
    Gradient& color1(glm::vec3 c) { color1_ = c; return *this; }
    Gradient& color2(glm::vec3 c) { color2_ = c; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Context::ShaderParams params;
        params.mode = mode_;
        params.param0 = angle_;
        params.param1 = offset_;
        params.param2 = scale_;
        params.vec0X = center_.x;
        params.vec0Y = center_.y;
        params.param3 = color1_.r;
        params.param4 = color1_.g;
        params.param5 = color1_.b;
        params.param6 = color2_.r;
        params.param7 = color2_.g;
        // Note: color2.b defaults to 1.0 in shader due to param limit

        ctx.runShader("shaders/gradient.wgsl", nullptr, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("mode", mode_, 0, 4),  // Added mode 4 (animated)
            floatParam("angle", angle_, 0.0f, 6.28318f),
            floatParam("offset", offset_, 0.0f, 1.0f),
            floatParam("scale", scale_, 0.1f, 10.0f),
            vec2Param("center", center_)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    int mode_ = 0;  // 0=linear, 1=radial, 2=angular, 3=diamond, 4=animated
    float angle_ = 0.0f;
    float offset_ = 0.0f;
    float scale_ = 1.0f;
    glm::vec2 center_ = glm::vec2(0.5f);
    glm::vec3 color1_ = glm::vec3(0.0f);  // Black
    glm::vec3 color2_ = glm::vec3(1.0f);  // White
    Texture output_;
};

VIVID_OPERATOR(Gradient)
