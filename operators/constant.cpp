// Constant Operator
// Generates a solid color texture or outputs a fixed numeric value

#include <vivid/vivid.h>

using namespace vivid;

class Constant : public Operator {
public:
    Constant() = default;

    // Fluent API
    Constant& color(float r, float g, float b, float a = 1.0f) {
        color_ = glm::vec4(r, g, b, a);
        outputTexture_ = true;
        return *this;
    }
    Constant& color(const glm::vec3& c) {
        color_ = glm::vec4(c, 1.0f);
        outputTexture_ = true;
        return *this;
    }
    Constant& color(const glm::vec4& c) {
        color_ = c;
        outputTexture_ = true;
        return *this;
    }
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
            // Generate solid color texture
            Context::ShaderParams params;
            params.param0 = color_.r;
            params.param1 = color_.g;
            params.param2 = color_.b;
            params.param3 = color_.a;
            ctx.runShader("shaders/constant.wgsl", nullptr, output_, params);
            ctx.setOutput("out", output_);
        } else {
            // Output numeric value
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

VIVID_OPERATOR(Constant)
