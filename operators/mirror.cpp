// Mirror / Kaleidoscope Effect
// Creates mirror reflections and kaleidoscope patterns from input
//
// Modes:
//   0 = Horizontal mirror (left half reflects to right)
//   1 = Vertical mirror (top half reflects to bottom)
//   2 = Quad mirror (top-left corner reflects to all quadrants)
//   3 = Kaleidoscope (radial symmetry with configurable segments)
//
// Example:
//   Mirror()
//       .input("webcam")
//       .mode(3)          // Kaleidoscope
//       .segments(8)      // 8-way symmetry
//       .rotation(0.5)    // Rotate pattern
//       .center(0.5, 0.5) // Center point

#include <vivid/vivid.h>

using namespace vivid;

class Mirror : public Operator {
public:
    Mirror& input(const std::string& node) {
        inputNode_ = node;
        return *this;
    }

    // Mode: 0=horizontal, 1=vertical, 2=quad, 3=kaleidoscope
    Mirror& mode(int m) {
        mode_ = m;
        return *this;
    }

    // Horizontal mirror (left reflects to right)
    Mirror& horizontal() {
        mode_ = 0;
        return *this;
    }

    // Vertical mirror (top reflects to bottom)
    Mirror& vertical() {
        mode_ = 1;
        return *this;
    }

    // Quad mirror (top-left to all corners)
    Mirror& quad() {
        mode_ = 2;
        return *this;
    }

    // Kaleidoscope mode
    Mirror& kaleidoscope(int segs = 6) {
        mode_ = 3;
        segments_ = static_cast<float>(segs);
        return *this;
    }

    // Number of segments for kaleidoscope (default 6)
    Mirror& segments(float s) {
        segments_ = s;
        return *this;
    }

    // Rotation angle in radians
    Mirror& rotation(float r) {
        rotation_ = r;
        return *this;
    }

    // Center point for kaleidoscope (0-1 range)
    Mirror& center(float x, float y) {
        centerX_ = x;
        centerY_ = y;
        return *this;
    }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_);
        if (!input || !input->valid()) return;

        // Resize output if input changed
        if (output_.width != input->width || output_.height != input->height) {
            output_ = ctx.createTexture(input->width, input->height);
        }

        Context::ShaderParams params;
        params.mode = mode_;
        params.param0 = segments_;
        params.param1 = rotation_;
        params.param2 = centerX_;
        params.param3 = centerY_;

        ctx.runShader("shaders/mirror.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    void cleanup() override {
        output_ = Texture{};
    }

private:
    std::string inputNode_;
    Texture output_;
    int mode_ = 0;           // Default: horizontal mirror
    float segments_ = 6.0f;  // For kaleidoscope
    float rotation_ = 0.0f;
    float centerX_ = 0.5f;
    float centerY_ = 0.5f;
};

VIVID_OPERATOR(Mirror)
