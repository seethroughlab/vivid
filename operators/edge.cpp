// Edge Detection Operator
// Sobel edge detection with multiple output modes

#include <vivid/vivid.h>

using namespace vivid;

class Edge : public Operator {
public:
    Edge() = default;
    explicit Edge(const std::string& inputNode) : inputNode_(inputNode) {}

    // Fluent API
    Edge& input(const std::string& node) { inputNode_ = node; return *this; }
    Edge& threshold(float t) { threshold_ = t; return *this; }
    Edge& thickness(float t) { thickness_ = t; return *this; }
    Edge& mode(int m) { mode_ = m; return *this; }  // 0=edges only, 1=edges+original, 2=inverted

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture* input = ctx.getInputTexture(inputNode_, "out");

        Context::ShaderParams params;
        params.param0 = threshold_;
        params.param1 = thickness_;
        params.mode = mode_;

        ctx.runShader("shaders/edge.wgsl", input, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("threshold", threshold_, 0.0f, 1.0f),
            floatParam("thickness", thickness_, 0.5f, 5.0f),
            intParam("mode", mode_, 0, 2)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputNode_;
    float threshold_ = 0.1f;
    float thickness_ = 1.0f;
    int mode_ = 0;  // 0=edges only, 1=edges+original, 2=inverted
    Texture output_;
};

VIVID_OPERATOR(Edge)
