#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace vivid {

/**
 * @brief Input selector/switcher.
 *
 * Selects between multiple texture inputs based on an index.
 * Optionally blends between adjacent inputs for smooth transitions.
 *
 * Example:
 * @code
 * Switch sw_;
 * sw_.input(0, "video1").input(1, "video2").input(2, "video3")
 *    .indexFrom("lfo").blend(true);  // Crossfade based on LFO
 * @endcode
 */
class Switch : public Operator {
public:
    Switch() = default;

    /// Add input at specified index (0-7)
    Switch& input(int idx, const std::string& node) {
        if (idx >= 0 && idx < 8) {
            inputs_[idx] = node;
            if (idx >= numInputs_) numInputs_ = idx + 1;
        }
        return *this;
    }
    /// Set selection index directly
    Switch& index(int idx) { index_ = idx; return *this; }
    /// Set selection index from another operator's output
    Switch& indexFrom(const std::string& node) { indexNode_ = node; return *this; }
    /// Enable blending between adjacent inputs
    Switch& blend(bool b) { blend_ = b; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        float indexFloat = indexNode_.empty()
            ? static_cast<float>(index_)
            : ctx.getInputValue(indexNode_, "out", 0.0f);

        int idx = static_cast<int>(indexFloat);

        if (blend_ && numInputs_ >= 2) {
            float frac = indexFloat - std::floor(indexFloat);
            int idxA = std::clamp(idx, 0, numInputs_ - 1);
            int idxB = std::clamp(idx + 1, 0, numInputs_ - 1);

            Texture* texA = ctx.getInputTexture(inputs_[idxA], "out");
            Texture* texB = ctx.getInputTexture(inputs_[idxB], "out");

            if (texA && texB) {
                Context::ShaderParams params;
                params.mode = 0;
                params.param0 = frac;
                ctx.runShader("shaders/composite.wgsl", texA, texB, output_, params);
            } else if (texA) {
                Context::ShaderParams params;
                ctx.runShader("shaders/passthrough.wgsl", texA, output_, params);
            }
        } else {
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

} // namespace vivid
