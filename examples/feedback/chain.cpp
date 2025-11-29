// Feedback Example
// Demonstrates video feedback with trails and motion effects
//
// This example creates a hypnotic feedback loop by:
// - Generating animated noise as a seed pattern
// - Applying feedback with zoom/rotation for infinite tunnel effect
// - Adding kaleidoscope symmetry for visual interest
// - Interactive mouse control
//
// Controls:
//   Mouse X: Rotation speed
//   Mouse Y: Zoom amount
//   Left click: Clear/reset feedback
//   Right click: Toggle kaleidoscope

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

class FeedbackDemo : public Operator {
public:
    void init(Context& ctx) override {
        // Create textures for our effect pipeline
        noise_ = ctx.createTexture();
        feedbackA_ = ctx.createTexture();
        feedbackB_ = ctx.createTexture();
        kaleidoscope_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        phase_ += ctx.dt() * 0.3f;

        // Get mouse input for interactivity
        float mouseX = ctx.mouseNormX();
        float mouseY = ctx.mouseNormY();

        // Mouse X controls rotation (-0.05 to 0.05 radians/frame)
        rotate_ = (mouseX - 0.5f) * 0.1f;

        // Mouse Y controls zoom (0.98 to 1.04)
        zoom_ = 0.98f + mouseY * 0.06f;

        // Left click clears feedback
        if (ctx.wasMousePressed(0)) {
            clearNext_ = true;
        }

        // Right click toggles kaleidoscope
        if (ctx.wasMousePressed(1)) {
            kaleidoEnabled_ = !kaleidoEnabled_;
        }

        // Stage 1: Generate animated noise as seed
        Context::ShaderParams noiseParams;
        noiseParams.param0 = 4.0f;       // scale
        noiseParams.param1 = phase_;     // time phase for animation
        noiseParams.param2 = 3.0f;       // octaves
        noiseParams.param3 = 2.0f;       // lacunarity
        noiseParams.param4 = 0.5f;       // persistence
        noiseParams.mode = 2;            // fractal noise
        ctx.runShader("shaders/noise.wgsl", nullptr, noise_, noiseParams);

        // Stage 2: Apply feedback with ping-pong buffers
        Texture& current = ping_ ? feedbackA_ : feedbackB_;
        Texture& previous = ping_ ? feedbackB_ : feedbackA_;

        Context::ShaderParams fbParams;
        fbParams.param0 = clearNext_ ? 0.0f : decay_;  // decay (0 = clear)
        fbParams.param1 = zoom_;                        // zoom
        fbParams.param2 = rotate_;                      // rotation
        fbParams.vec0X = 0.0f;                          // translate X
        fbParams.vec0Y = 0.0f;                          // translate Y
        fbParams.mode = 0;                              // max blend

        ctx.runShader("shaders/feedback.wgsl", &noise_, &previous, current, fbParams);

        clearNext_ = false;
        ping_ = !ping_;

        // Stage 3: Apply kaleidoscope if enabled
        if (kaleidoEnabled_) {
            Context::ShaderParams mirrorParams;
            mirrorParams.mode = 3;           // kaleidoscope mode
            mirrorParams.param0 = 6.0f;      // 6 segments
            mirrorParams.param1 = 0.0f;      // no rotation
            mirrorParams.param2 = 0.5f;      // center X
            mirrorParams.param3 = 0.5f;      // center Y
            ctx.runShader("shaders/mirror.wgsl", &current, kaleidoscope_, mirrorParams);
        } else {
            kaleidoscope_ = current;
        }

        // Stage 4: Color enhancement - cycle hue over time
        Context::ShaderParams hsvParams;
        hsvParams.param0 = std::fmod(ctx.time() * 0.05f, 1.0f);  // hue shift
        hsvParams.param1 = 1.2f;   // saturation boost
        hsvParams.param2 = 1.05f;  // brightness boost
        ctx.runShader("shaders/hsv.wgsl", &kaleidoscope_, output_, hsvParams);

        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("decay", decay_, 0.8f, 0.99f),
            floatParam("zoom", zoom_, 0.95f, 1.05f),
            floatParam("rotate", rotate_, -0.1f, 0.1f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    // Effect textures
    Texture noise_;
    Texture feedbackA_;
    Texture feedbackB_;
    Texture kaleidoscope_;
    Texture output_;

    // Feedback ping-pong state
    bool ping_ = false;

    // Parameters
    float decay_ = 0.92f;
    float zoom_ = 1.02f;
    float rotate_ = 0.01f;
    float phase_ = 0.0f;

    // Interactive state
    bool clearNext_ = false;
    bool kaleidoEnabled_ = true;
};

VIVID_OPERATOR(FeedbackDemo)
