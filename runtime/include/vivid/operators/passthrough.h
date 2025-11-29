#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>

namespace vivid {

/**
 * @brief Pass-through operator.
 *
 * Passes through an input texture unchanged. Useful for organization
 * and creating explicit output points in a chain.
 *
 * Example:
 * @code
 * Passthrough out_;
 * out_.input("final_composite");
 * @endcode
 */
class Passthrough : public Operator {
public:
    Passthrough() = default;
    explicit Passthrough(const std::string& inputNode) : inputNode_(inputNode) {}

    /// Set input texture from another operator
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

} // namespace vivid
