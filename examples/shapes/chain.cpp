// Shapes Example
// Demonstrates SDF-based shape rendering
//
// This example shows:
// - Rendering SDF shapes (circle, rectangle, star, etc.)
// - Animating shape parameters
// - Combining shapes with composite blending

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

struct ShapesState : OperatorState {
    float time = 0.0f;
};

class ShapesExample : public Operator {
public:
    void init(Context& ctx) override {
        shape1_ = ctx.createTexture();
        shape2_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        time_ += ctx.dt();

        // Animated parameters
        float pulse = std::sin(time_ * 2.0f) * 0.5f + 0.5f;
        float rotation = time_ * 0.5f;

        // Shape 1: Animated ring
        // mode: 0=circle, 1=rectangle, 2=triangle, 3=line, 4=ring, 5=star
        Context::ShaderParams s1Params;
        s1Params.mode = 4;                        // ring
        s1Params.param0 = 0.5f;                   // center X
        s1Params.param1 = 0.5f;                   // center Y
        s1Params.param2 = 0.2f + pulse * 0.15f;   // outer radius (animated)
        s1Params.param3 = 0.15f + pulse * 0.1f;   // inner radius (animated)
        s1Params.param4 = 0.02f;                  // softness
        s1Params.param5 = rotation;               // rotation
        ctx.runShader("shaders/shape.wgsl", nullptr, shape1_, s1Params);

        // Shape 2: Rotating star
        Context::ShaderParams s2Params;
        s2Params.mode = 5;                        // star
        s2Params.param0 = 0.5f;                   // center X
        s2Params.param1 = 0.5f;                   // center Y
        s2Params.param2 = 0.3f;                   // outer radius
        s2Params.param3 = 0.15f;                  // inner radius
        s2Params.param4 = 0.01f;                  // softness
        s2Params.param5 = -rotation * 1.5f;       // counter-rotate
        s2Params.param6 = static_cast<float>(numPoints_); // star points
        ctx.runShader("shaders/shape.wgsl", nullptr, shape2_, s2Params);

        // Composite with add blend
        Context::ShaderParams compParams;
        compParams.mode = 1;                      // add blend
        compParams.param0 = 1.0f;                 // full mix
        ctx.runShader("shaders/composite.wgsl", &shape1_, &shape2_, output_, compParams);

        ctx.setOutput("out", output_);
    }

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<ShapesState>();
        state->time = time_;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {
        if (auto* s = dynamic_cast<ShapesState*>(state.get())) {
            time_ = s->time;
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("numPoints", numPoints_, 3, 12)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    int numPoints_ = 5;       // Star points
    float time_ = 0.0f;
    Texture shape1_;
    Texture shape2_;
    Texture output_;
};

VIVID_OPERATOR(ShapesExample)
