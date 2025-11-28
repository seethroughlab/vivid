// Displacement Operator
// Distorts a texture using a displacement map

#include <vivid/vivid.h>
#include <glm/glm.hpp>

using namespace vivid;

class Displacement : public Operator {
public:
    Displacement() = default;
    explicit Displacement(const std::string& inputNode) : inputNode_(inputNode) {}

    // Fluent API
    Displacement& input(const std::string& node) { inputNode_ = node; return *this; }
    Displacement& map(const std::string& node) { mapNode_ = node; return *this; }
    Displacement& amount(float a) { amount_ = a; return *this; }
    Displacement& channel(int c) { channel_ = c; return *this; }  // 0=lum, 1=R, 2=G, 3=RG
    Displacement& direction(glm::vec2 d) { direction_ = d; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        // Use input as both source and displacement map if no separate map specified
        Texture* input = ctx.getInputTexture(inputNode_, "out");
        // Texture* dispMap = mapNode_.empty() ? input : ctx.getInputTexture(mapNode_, "out");

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
    std::string mapNode_;  // Optional separate displacement map
    float amount_ = 0.1f;
    int channel_ = 0;  // 0=luminance, 1=R, 2=G, 3=RG
    glm::vec2 direction_ = glm::vec2(1.0f, 1.0f);
    Texture output_;
};

VIVID_OPERATOR(Displacement)
