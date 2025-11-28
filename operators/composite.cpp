// Composite Operator
// Blends two textures using various blend modes

#include <vivid/vivid.h>

using namespace vivid;

class Composite : public Operator {
public:
    Composite() = default;

    // Fluent API
    Composite& a(const std::string& node) { nodeA_ = node; return *this; }
    Composite& b(const std::string& node) { nodeB_ = node; return *this; }
    Composite& mode(int m) { mode_ = m; return *this; }
    Composite& mix(float m) { mix_ = m; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* texA = ctx.getInputTexture(nodeA_, "out");
        Texture* texB = ctx.getInputTexture(nodeB_, "out");

        // For now, use texA as input (two-texture support needs renderer enhancement)
        Context::ShaderParams params;
        params.mode = mode_;
        params.param0 = mix_;

        ctx.runShader("shaders/composite.wgsl", texA, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("mode", mode_, 0, 4),
            floatParam("mix", mix_, 0.0f, 1.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string nodeA_;
    std::string nodeB_;
    int mode_ = 0;  // 0=over, 1=add, 2=multiply, 3=screen, 4=difference
    float mix_ = 0.5f;
    Texture output_;
};

VIVID_OPERATOR(Composite)
