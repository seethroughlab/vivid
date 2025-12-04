#pragma once

#include "vivid/operator.h"

namespace vivid {

/// Adjusts brightness and contrast of an input texture
class BrightnessContrast : public TextureOperator {
public:
    std::string typeName() const override { return "BrightnessContrast"; }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("brightness", 0.0f, -1.0f, 1.0f),
            floatParam("contrast", 1.0f, 0.0f, 3.0f)
        };
    }

    // Fluent API
    BrightnessContrast& brightness(float b) { brightness_ = b; return *this; }
    BrightnessContrast& contrast(float c) { contrast_ = c; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    float brightness_ = 0.0f;
    float contrast_ = 1.0f;

    struct Constants {
        float brightness;
        float contrast;
        float padding[2];
    };
};

} // namespace vivid
