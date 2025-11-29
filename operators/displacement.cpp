// Displacement Operator
// Distorts a source texture using a separate displacement map texture
// Chain any texture (Noise, Gradient, ImageFile, etc.) as the displacement map

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
        Texture* source = ctx.getInputTexture(inputNode_, "out");
        Texture* dispMap = ctx.getInputTexture(mapNode_, "out");

        // If no displacement map specified, use source as map (self-displacement)
        if (!dispMap || !dispMap->valid()) {
            dispMap = source;
        }

        if (!source || !source->valid()) {
            return;
        }

        // Resize output to match source
        if (output_.width != source->width || output_.height != source->height) {
            output_ = ctx.createTexture(source->width, source->height);
        }

        Context::ShaderParams params;
        params.mode = channel_;  // 0=lum, 1=R, 2=G, 3=RG
        params.param0 = amount_;
        params.vec0X = direction_.x;
        params.vec0Y = direction_.y;

        // source = inputTexture, dispMap = inputTexture2
        ctx.runShader("shaders/displacement.wgsl", source, dispMap, output_, params);
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
    std::string mapNode_;  // Displacement map node (Noise, Gradient, any texture)
    float amount_ = 0.1f;
    int channel_ = 0;  // 0=luminance, 1=R, 2=G, 3=RG
    glm::vec2 direction_ = glm::vec2(1.0f, 1.0f);
    Texture output_;
};

VIVID_OPERATOR(Displacement)
