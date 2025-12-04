#pragma once

#include "vivid/operator.h"

namespace vivid {

/// UV displacement mapping - displaces pixels based on a displacement map
class Displacement : public TextureOperator {
public:
    std::string typeName() const override { return "Displacement"; }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("amount", 0.1f, 0.0f, 1.0f),
            floatParam("scaleX", 1.0f, 0.0f, 10.0f),
            floatParam("scaleY", 1.0f, 0.0f, 10.0f)
        };
    }

    // Fluent API
    Displacement& amount(float a) { amount_ = a; return *this; }
    Displacement& scale(float s) { scaleX_ = s; scaleY_ = s; return *this; }
    Displacement& scale(float sx, float sy) { scaleX_ = sx; scaleY_ = sy; return *this; }

    // Process override (needs two inputs: source and displacement map)
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    float amount_ = 0.1f;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;

    struct Constants {
        float amount;
        float scaleX;
        float scaleY;
        float padding;
    };
};

} // namespace vivid
