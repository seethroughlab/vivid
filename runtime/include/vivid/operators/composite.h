#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include <string>
#include <vector>

namespace vivid {

/**
 * @brief Texture compositing/blending.
 *
 * Blends multiple textures using alpha over compositing.
 * Supports up to 8 input layers.
 *
 * Example (2 inputs - legacy style):
 * @code
 * Composite comp_;
 * comp_.a("background").b("foreground").opacity(1.0f);
 * @endcode
 *
 * Example (multiple inputs):
 * @code
 * Composite comp_;
 * comp_.inputs({"layer1", "layer2", "layer3", "layer4"}).opacity(1.0f);
 * @endcode
 */
class Composite : public Operator {
public:
    Composite() = default;

    /// Set first input texture (background) - legacy 2-input API
    Composite& a(const std::string& node) {
        if (inputs_.empty()) inputs_.push_back(node);
        else inputs_[0] = node;
        return *this;
    }
    /// Set second input texture (foreground) - legacy 2-input API
    Composite& b(const std::string& node) {
        if (inputs_.size() < 2) inputs_.resize(2);
        inputs_[1] = node;
        return *this;
    }

    /// Set multiple input nodes (up to 8) - new multi-input API
    Composite& inputs(const std::vector<std::string>& nodes) {
        inputs_ = nodes;
        if (inputs_.size() > 8) inputs_.resize(8);
        return *this;
    }

    /// Add an input node
    Composite& addInput(const std::string& node) {
        if (inputs_.size() < 8) inputs_.push_back(node);
        return *this;
    }

    /// Set blend mode (0=over, 1=add, 2=multiply, 3=screen, 4=difference)
    /// Note: Currently only "over" is supported for multi-input
    Composite& mode(int m) { mode_ = m; return *this; }

    /// Set overall opacity (0.0 to 1.0)
    Composite& opacity(float o) { opacity_ = o; return *this; }

    /// Legacy: Set mix amount (alias for opacity)
    Composite& mix(float m) { opacity_ = m; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        // Gather input textures
        std::vector<const Texture*> textures;
        for (const auto& nodeName : inputs_) {
            Texture* tex = ctx.getInputTexture(nodeName, "out");
            textures.push_back(tex);
        }

        Context::ShaderParams params;
        params.mode = static_cast<int>(textures.size());
        params.param0 = opacity_;

        // Use multi-input shader
        ctx.runShaderMulti("shaders/composite_multi.wgsl", textures, output_, params);
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("mode", mode_, 0, 4),
            floatParam("opacity", opacity_, 0.0f, 1.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    std::vector<std::string> inputs_;
    int mode_ = 0;
    float opacity_ = 1.0f;
    Texture output_;
};

} // namespace vivid
