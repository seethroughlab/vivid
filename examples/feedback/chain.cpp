// Feedback Example
// Demonstrates the feedback operator for creating trail and echo effects
//
// This example shows:
// - Using feedback for persistent visual trails
// - Combining feedback with noise for organic movement
// - State preservation for continuous animation

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

struct FeedbackState : OperatorState {
    float phase = 0.0f;
};

class FeedbackExample : public Operator {
public:
    void init(Context& ctx) override {
        noise_ = ctx.createTexture();
        feedback_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        phase_ += ctx.dt() * 0.5f;

        // Generate animated noise as the source
        Context::ShaderParams noiseParams;
        noiseParams.param0 = 6.0f;           // scale
        noiseParams.param1 = phase_;         // phase
        noiseParams.param2 = 3.0f;           // octaves
        noiseParams.param3 = 2.0f;           // lacunarity
        noiseParams.param4 = 0.5f;           // persistence
        ctx.runShader("shaders/noise.wgsl", nullptr, noise_, noiseParams);

        // Apply feedback effect
        // param0 = decay (0.0-1.0, how quickly trails fade)
        // param1 = zoom (1.0 = no zoom, >1 = zoom in)
        // param2 = rotate (radians per frame)
        // vec0 = translate offset
        Context::ShaderParams fbParams;
        fbParams.param0 = decay_;
        fbParams.param1 = zoom_;
        fbParams.param2 = rotate_;
        fbParams.vec0X = 0.0f;
        fbParams.vec0Y = 0.0f;
        ctx.runShader("shaders/feedback.wgsl", &noise_, feedback_, fbParams);

        // Copy feedback to output (feedback shader writes to its own buffer)
        output_ = feedback_;
        ctx.setOutput("out", output_);
    }

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<FeedbackState>();
        state->phase = phase_;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {
        if (auto* s = dynamic_cast<FeedbackState*>(state.get())) {
            phase_ = s->phase;
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("decay", decay_, 0.8f, 0.99f),
            floatParam("zoom", zoom_, 0.98f, 1.02f),
            floatParam("rotate", rotate_, -0.05f, 0.05f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    float decay_ = 0.95f;
    float zoom_ = 1.005f;
    float rotate_ = 0.01f;
    float phase_ = 0.0f;
    Texture noise_;
    Texture feedback_;
    Texture output_;
};

VIVID_OPERATOR(FeedbackExample)
