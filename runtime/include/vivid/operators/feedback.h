#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <memory>
#include <string>

namespace vivid {

/// State for Feedback operator
struct FeedbackState : OperatorState {
    bool initialized = false;
};

/**
 * @brief Feedback/trail effect using double-buffered ping-pong.
 *
 * Creates trails and echo effects by blending current input with
 * previous frame and optionally transforming it.
 *
 * Example:
 * @code
 * Feedback fb_;
 * fb_.input("particles").decay(0.95f).zoom(1.01f).rotate(0.01f);
 * @endcode
 */
class Feedback : public Operator {
public:
    Feedback() = default;
    explicit Feedback(const std::string& inputNode) : inputNode_(inputNode) {}

    /// Set input texture from another operator
    Feedback& input(const std::string& node) { inputNode_ = node; return *this; }
    /// Set decay rate (0.0 to 1.0, higher = longer trails)
    Feedback& decay(float d) { decay_ = d; return *this; }
    /// Set zoom per frame (>1.0 = zoom out, <1.0 = zoom in)
    Feedback& zoom(float z) { zoom_ = z; return *this; }
    /// Set rotation per frame in radians
    Feedback& rotate(float r) { rotate_ = r; return *this; }
    /// Set translation per frame
    Feedback& translate(glm::vec2 t) { translate_ = t; return *this; }

    void init(Context& ctx) override {
        buffer_[0] = ctx.createTexture();
        buffer_[1] = ctx.createTexture();
        currentBuffer_ = 0;
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");
        Texture& current = buffer_[currentBuffer_];
        Texture& previous = buffer_[1 - currentBuffer_];

        Context::ShaderParams params;
        params.param0 = decay_;
        params.param1 = zoom_;
        params.param2 = rotate_;
        params.vec0X = translate_.x;
        params.vec0Y = translate_.y;

        // Pass input as inputTexture, previous frame as inputTexture2
        ctx.runShader("shaders/feedback.wgsl", input, &previous, current, params);
        ctx.setOutput("out", current);

        currentBuffer_ = 1 - currentBuffer_;
    }

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<FeedbackState>();
        state->initialized = true;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {}

    std::vector<ParamDecl> params() override {
        return {
            floatParam("decay", decay_, 0.0f, 1.0f),
            floatParam("zoom", zoom_, 0.9f, 1.1f),
            floatParam("rotate", rotate_, -0.1f, 0.1f),
            vec2Param("translate", translate_)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float decay_ = 0.95f;
    float zoom_ = 1.0f;
    float rotate_ = 0.0f;
    glm::vec2 translate_ = glm::vec2(0.0f);
    Texture buffer_[2];
    int currentBuffer_ = 0;
};

} // namespace vivid
