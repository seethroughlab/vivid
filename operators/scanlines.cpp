// Scanlines Operator
// Adds CRT-style horizontal lines for retro monitor effect

#include <vivid/vivid.h>

using namespace vivid;

class Scanlines : public Operator {
public:
    Scanlines() = default;
    explicit Scanlines(const std::string& inputNode) : inputNode_(inputNode) {}

    // Fluent API
    Scanlines& input(const std::string& node) { inputNode_ = node; return *this; }
    Scanlines& density(float d) { density_ = d; return *this; }
    Scanlines& intensity(float i) { intensity_ = i; return *this; }
    Scanlines& scroll(float s) { scroll_ = s; return *this; }
    Scanlines& mode(int m) { mode_ = m; return *this; }  // 0=simple, 1=alternating, 2=RGB subpixel

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        Context::ShaderParams params;
        params.param0 = density_;
        params.param1 = intensity_;
        params.param2 = scroll_;
        params.mode = mode_;

        ctx.runShader("shaders/scanlines.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("density", density_, 100.0f, 1000.0f),
            floatParam("intensity", intensity_, 0.0f, 1.0f),
            floatParam("scroll", scroll_, 0.0f, 100.0f),
            intParam("mode", mode_, 0, 2)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float density_ = 400.0f;
    float intensity_ = 0.3f;
    float scroll_ = 0.0f;
    int mode_ = 0;  // simple dark lines
    Texture output_;
};

VIVID_OPERATOR(Scanlines)
