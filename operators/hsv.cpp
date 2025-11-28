// HSV Adjustment Operator
// Adjusts hue, saturation, and value of an input texture

#include <vivid/vivid.h>

using namespace vivid;

class HSVAdjust : public Operator {
public:
    HSVAdjust() = default;
    explicit HSVAdjust(const std::string& inputNode) : inputNode_(inputNode) {}

    // Fluent API
    HSVAdjust& input(const std::string& node) { inputNode_ = node; return *this; }
    HSVAdjust& hueShift(float h) { hueShift_ = h; return *this; }
    HSVAdjust& saturation(float s) { saturation_ = s; return *this; }
    HSVAdjust& value(float v) { value_ = v; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        Context::ShaderParams params;
        params.param0 = hueShift_;
        params.param1 = saturation_;
        params.param2 = value_;

        ctx.runShader("shaders/hsv.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("hueShift", hueShift_, -1.0f, 1.0f),
            floatParam("saturation", saturation_, 0.0f, 3.0f),
            floatParam("value", value_, 0.0f, 3.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float hueShift_ = 0.0f;
    float saturation_ = 1.0f;
    float value_ = 1.0f;
    Texture output_;
};

VIVID_OPERATOR(HSVAdjust)
