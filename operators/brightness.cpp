// Brightness and Contrast Operator
// Adjusts brightness and contrast of an input texture

#include <vivid/vivid.h>

using namespace vivid;

class Brightness : public Operator {
public:
    Brightness() = default;
    explicit Brightness(const std::string& inputNode) : inputNode_(inputNode) {}

    // Fluent API
    Brightness& input(const std::string& node) { inputNode_ = node; return *this; }
    Brightness& amount(float a) { amount_ = a; return *this; }
    Brightness& amount(const std::string& node) { amountNode_ = node; useNode_ = true; return *this; }
    Brightness& contrast(float c) { contrast_ = c; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        float finalAmount = amount_;
        if (useNode_ && !amountNode_.empty()) {
            finalAmount = ctx.getInputValue(amountNode_, "out", amount_);
        }

        Context::ShaderParams params;
        params.param0 = finalAmount;
        params.param1 = contrast_;

        ctx.runShader("shaders/brightness.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("amount", amount_, -1.0f, 2.0f),
            floatParam("contrast", contrast_, 0.0f, 3.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    std::string amountNode_;
    bool useNode_ = false;
    float amount_ = 1.0f;
    float contrast_ = 1.0f;
    Texture output_;
};

VIVID_OPERATOR(Brightness)
