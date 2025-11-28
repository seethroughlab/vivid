// Passthrough Operator
// Passes through an input texture unchanged - useful for organization
// and creating explicit output points in a chain

#include <vivid/vivid.h>

using namespace vivid;

class Passthrough : public Operator {
public:
    Passthrough() = default;
    explicit Passthrough(const std::string& inputNode) : inputNode_(inputNode) {}

    // Fluent API
    Passthrough& input(const std::string& node) { inputNode_ = node; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        if (input) {
            Context::ShaderParams params;
            ctx.runShader("shaders/passthrough.wgsl", input, output_, params);
        }

        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {};
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    Texture output_;
};

VIVID_OPERATOR(Passthrough)
