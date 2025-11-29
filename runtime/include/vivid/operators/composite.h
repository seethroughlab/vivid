#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Texture compositing/blending.
 *
 * Blends two textures using various blend modes.
 *
 * Blend modes:
 * - 0: Over (normal alpha blend)
 * - 1: Add
 * - 2: Multiply
 * - 3: Screen
 * - 4: Difference
 *
 * Example:
 * @code
 * Composite comp_;
 * comp_.a("background").b("foreground").mode(1).mix(0.5f);
 * @endcode
 */
class Composite : public Operator {
public:
    Composite() = default;

    /// Set first input texture (background)
    Composite& a(const std::string& node) { nodeA_ = node; return *this; }
    /// Set second input texture (foreground)
    Composite& b(const std::string& node) { nodeB_ = node; return *this; }
    /// Set blend mode (0=over, 1=add, 2=multiply, 3=screen, 4=difference)
    Composite& mode(int m) { mode_ = m; return *this; }
    /// Set mix amount (0.0 to 1.0)
    Composite& mix(float m) { mix_ = m; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* texA = ctx.getInputTexture(nodeA_, "out");
        Texture* texB = ctx.getInputTexture(nodeB_, "out");

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
    int mode_ = 0;
    float mix_ = 0.5f;
    Texture output_;
};

} // namespace vivid
