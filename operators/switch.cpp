// Switch Operator
// Selects between multiple texture inputs based on an index

#include <vivid/vivid.h>
#include <vector>

using namespace vivid;

class Switch : public Operator {
public:
    Switch() = default;

    // Fluent API
    Switch& input(int idx, const std::string& node) {
        if (idx >= 0 && idx < 8) {
            inputs_[idx] = node;
            if (idx >= numInputs_) numInputs_ = idx + 1;
        }
        return *this;
    }
    Switch& index(int idx) { index_ = idx; return *this; }
    Switch& indexFrom(const std::string& node) { indexNode_ = node; return *this; }
    Switch& blend(bool b) { blend_ = b; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        // Get index - either from parameter or from another node
        float indexFloat = indexNode_.empty()
            ? static_cast<float>(index_)
            : ctx.getInputValue(indexNode_, "out", 0.0f);

        int idx = static_cast<int>(indexFloat);

        if (blend_ && numInputs_ >= 2) {
            // Blend between adjacent inputs based on fractional part
            float frac = indexFloat - std::floor(indexFloat);
            int idxA = std::clamp(idx, 0, numInputs_ - 1);
            int idxB = std::clamp(idx + 1, 0, numInputs_ - 1);

            Texture* texA = ctx.getInputTexture(inputs_[idxA], "out");
            Texture* texB = ctx.getInputTexture(inputs_[idxB], "out");

            if (texA && texB) {
                Context::ShaderParams params;
                params.mode = 0;  // over/mix blend
                params.param0 = frac;
                ctx.runShader("shaders/composite.wgsl", texA, texB, output_, params);
            } else if (texA) {
                // Just copy texA
                Context::ShaderParams params;
                ctx.runShader("shaders/passthrough.wgsl", texA, output_, params);
            }
        } else {
            // Direct selection
            idx = std::clamp(idx, 0, numInputs_ - 1);
            Texture* input = ctx.getInputTexture(inputs_[idx], "out");

            if (input) {
                Context::ShaderParams params;
                ctx.runShader("shaders/passthrough.wgsl", input, output_, params);
            }
        }

        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("index", index_, 0, 7),
            boolParam("blend", blend_)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::string inputs_[8];
    std::string indexNode_;
    int numInputs_ = 0;
    int index_ = 0;
    bool blend_ = false;
    Texture output_;
};

VIVID_OPERATOR(Switch)
