// Feedback Operator
// Creates trails/echo effects using double-buffered ping-pong

#include <vivid/vivid.h>
#include <glm/glm.hpp>

using namespace vivid;

struct FeedbackState : OperatorState {
    bool initialized = false;
};

class Feedback : public Operator {
public:
    Feedback() = default;
    explicit Feedback(const std::string& inputNode) : inputNode_(inputNode) {}

    // Fluent API
    Feedback& input(const std::string& node) { inputNode_ = node; return *this; }
    Feedback& decay(float d) { decay_ = d; return *this; }
    Feedback& zoom(float z) { zoom_ = z; return *this; }
    Feedback& rotate(float r) { rotate_ = r; return *this; }
    Feedback& translate(glm::vec2 t) { translate_ = t; return *this; }
    Feedback& translate(float x, float y) { translate_ = glm::vec2(x, y); return *this; }
    Feedback& mode(int m) { mode_ = m; return *this; }  // 0=max, 1=add, 2=screen, 3=mix

    void init(Context& ctx) override {
        buffer_[0] = ctx.createTexture();
        buffer_[1] = ctx.createTexture();
        currentBuffer_ = 0;
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");
        if (!input || !input->valid()) return;

        Texture& current = buffer_[currentBuffer_];
        Texture& previous = buffer_[1 - currentBuffer_];

        // Resize buffers if input size changed
        if (current.width != input->width || current.height != input->height) {
            buffer_[0] = ctx.createTexture(input->width, input->height);
            buffer_[1] = ctx.createTexture(input->width, input->height);
        }

        // Set up shader params
        Context::ShaderParams params;
        params.param0 = decay_;
        params.param1 = zoom_;
        params.param2 = rotate_;
        params.vec0X = translate_.x;
        params.vec0Y = translate_.y;
        params.mode = mode_;

        // Run feedback shader with input + previous frame → current buffer
        ctx.runShader("shaders/feedback.wgsl", input, &previous, current, params);
        ctx.setOutput("out", current);

        // Swap buffers for next frame
        currentBuffer_ = 1 - currentBuffer_;
    }

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<FeedbackState>();
        state->initialized = true;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {
        // Note: we can't actually preserve the texture contents across reload
        // but we can preserve that we were initialized
    }

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
    int mode_ = 0;  // 0=max, 1=add, 2=screen, 3=mix
    Texture buffer_[2];
    int currentBuffer_ = 0;
};

VIVID_OPERATOR(Feedback)
