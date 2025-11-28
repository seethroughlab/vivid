// Hello World Vivid Example
// A simple operator chain that outputs animated noise

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

// Simple noise operator that generates animated noise
class NoiseOperator : public Operator {
public:
    NoiseOperator() = default;

    void init(Context& ctx) override {
        // Create output texture
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        // Run noise shader
        ctx.runShader("shaders/noise.wgsl", output_);

        // Store output for other operators to use
        ctx.setOutput("out", output_);
    }

    void cleanup() override {
        // Texture cleanup handled by renderer
    }

    OutputKind outputKind() override {
        return OutputKind::Texture;
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("scale", 4.0f, 0.1f, 100.0f),
            floatParam("speed", 1.0f, 0.0f, 10.0f)
        };
    }

private:
    Texture output_;
};

// Export the operator for hot-loading
VIVID_OPERATOR(NoiseOperator)
