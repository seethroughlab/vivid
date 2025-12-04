#pragma once

#include "vivid/operator.h"

namespace vivid {

/// Chromatic aberration - RGB channel separation effect
class ChromaticAberration : public TextureOperator {
public:
    std::string typeName() const override { return "ChromaticAberration"; }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("amount", 0.01f, 0.0f, 0.1f),
            floatParam("angle", 0.0f, 0.0f, 360.0f),
            floatParam("centerX", 0.5f, 0.0f, 1.0f),
            floatParam("centerY", 0.5f, 0.0f, 1.0f)
        };
    }

    // Fluent API
    ChromaticAberration& amount(float a) { amount_ = a; return *this; }
    ChromaticAberration& angle(float a) { angle_ = a; return *this; }
    ChromaticAberration& center(float x, float y) { centerX_ = x; centerY_ = y; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    float amount_ = 0.01f;
    float angle_ = 0.0f;
    float centerX_ = 0.5f;
    float centerY_ = 0.5f;

    struct Constants {
        float amount;
        float angle;
        float centerX;
        float centerY;
    };
};

} // namespace vivid
