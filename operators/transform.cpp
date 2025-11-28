// Transform Operator
// Applies translate, scale, and rotate to an input texture

#include <vivid/vivid.h>
#include <glm/glm.hpp>

using namespace vivid;

class Transform : public Operator {
public:
    Transform() = default;
    explicit Transform(const std::string& inputNode) : inputNode_(inputNode) {}

    // Fluent API
    Transform& input(const std::string& node) { inputNode_ = node; return *this; }
    Transform& translate(glm::vec2 t) { translate_ = t; return *this; }
    Transform& scale(glm::vec2 s) { scale_ = s; return *this; }
    Transform& scale(float s) { scale_ = glm::vec2(s); return *this; }
    Transform& rotate(float r) { rotate_ = r; return *this; }
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

VIVID_OPERATOR(Transform)
