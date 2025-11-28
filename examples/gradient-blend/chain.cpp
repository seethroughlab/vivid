// Gradient Blend Example
// Demonstrates gradient generation and composite blending
//
// This example shows:
// - Creating animated gradients
// - Blending textures with different blend modes
// - Combining multiple visual elements

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

struct GradientState : OperatorState {
    float hueOffset = 0.0f;
};

class GradientBlendExample : public Operator {
public:
    void init(Context& ctx) override {
        gradient1_ = ctx.createTexture();
        gradient2_ = ctx.createTexture();
        noise_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        hueOffset_ += ctx.dt() * rotateSpeed_;

        // Create first gradient (radial, animated angle)
        // mode: 0=linear, 1=radial, 2=angular, 3=diamond
        Context::ShaderParams g1Params;
        g1Params.mode = 1;                    // radial
        g1Params.param0 = 0.5f;               // center X
        g1Params.param1 = 0.5f;               // center Y
        g1Params.param2 = 1.2f;               // scale
        g1Params.param3 = hueOffset_;         // rotation/offset
        // Colors encoded in params 4-7 (simplified - uses gradient LUT in shader)
        ctx.runShader("shaders/gradient.wgsl", nullptr, gradient1_, g1Params);

        // Create second gradient (angular, spinning)
        Context::ShaderParams g2Params;
        g2Params.mode = 2;                    // angular
        g2Params.param0 = 0.5f;
        g2Params.param1 = 0.5f;
        g2Params.param2 = 1.0f;
        g2Params.param3 = -hueOffset_ * 2.0f; // counter-rotate faster
        ctx.runShader("shaders/gradient.wgsl", nullptr, gradient2_, g2Params);

        // Generate subtle noise for texture
        Context::ShaderParams noiseParams;
        noiseParams.param0 = 8.0f;            // scale
        noiseParams.param1 = ctx.time() * 0.2f;
        noiseParams.param2 = 2.0f;            // octaves
        ctx.runShader("shaders/noise.wgsl", nullptr, noise_, noiseParams);

        // Composite: blend gradients with screen mode
        // mode: 0=over, 1=add, 2=multiply, 3=screen, 4=difference
        Context::ShaderParams compParams;
        compParams.mode = blendMode_;
        compParams.param0 = mixAmount_;       // mix amount
        ctx.runShader("shaders/composite.wgsl", &gradient1_, &gradient2_, output_, compParams);

        ctx.setOutput("out", output_);
    }

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<GradientState>();
        state->hueOffset = hueOffset_;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {
        if (auto* s = dynamic_cast<GradientState*>(state.get())) {
            hueOffset_ = s->hueOffset;
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("rotateSpeed", rotateSpeed_, 0.0f, 2.0f),
            intParam("blendMode", blendMode_, 0, 4),
            floatParam("mixAmount", mixAmount_, 0.0f, 1.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    float rotateSpeed_ = 0.3f;
    int blendMode_ = 3;       // screen
    float mixAmount_ = 0.7f;
    float hueOffset_ = 0.0f;
    Texture gradient1_;
    Texture gradient2_;
    Texture noise_;
    Texture output_;
};

VIVID_OPERATOR(GradientBlendExample)
