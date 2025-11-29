// Pixelate Operator
// Reduces effective resolution for blocky/mosaic effect

#include <vivid/vivid.h>

using namespace vivid;

class Pixelate : public Operator {
public:
    Pixelate() = default;
    explicit Pixelate(const std::string& inputNode) : inputNode_(inputNode) {}

    // Fluent API
    Pixelate& input(const std::string& node) { inputNode_ = node; return *this; }
    Pixelate& blockSize(float size) { blockSize_ = size; return *this; }
    Pixelate& mode(int m) { mode_ = m; return *this; }  // 0=square, 1=aspect-corrected

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        Context::ShaderParams params;
        params.param0 = blockSize_;
        params.mode = mode_;

        ctx.runShader("shaders/pixelate.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("blockSize", blockSize_, 1.0f, 64.0f),
            intParam("mode", mode_, 0, 1)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float blockSize_ = 8.0f;
    int mode_ = 0;  // square blocks
    Texture output_;
};

VIVID_OPERATOR(Pixelate)
