// Chromatic Aberration Operator
// Separates RGB channels with offset for VHS/glitch aesthetic

#include <vivid/vivid.h>

using namespace vivid;

class ChromaticAberration : public Operator {
public:
    ChromaticAberration() = default;
    explicit ChromaticAberration(const std::string& inputNode) : inputNode_(inputNode) {}

    // Fluent API
    ChromaticAberration& input(const std::string& node) { inputNode_ = node; return *this; }
    ChromaticAberration& amount(float a) { amount_ = a; return *this; }
    ChromaticAberration& angle(float a) { angle_ = a; return *this; }
    ChromaticAberration& mode(int m) { mode_ = m; return *this; }  // 0=directional, 1=radial, 2=barrel

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        Context::ShaderParams params;
        params.param0 = amount_;
        params.param1 = angle_;
        params.mode = mode_;

        ctx.runShader("shaders/chromatic_aberration.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("amount", amount_, 0.0f, 0.1f),
            floatParam("angle", angle_, 0.0f, 6.28318f),
            intParam("mode", mode_, 0, 2)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float amount_ = 0.01f;
    float angle_ = 0.0f;
    int mode_ = 0;  // directional
    Texture output_;
};

VIVID_OPERATOR(ChromaticAberration)
