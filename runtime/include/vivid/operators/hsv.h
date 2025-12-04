#pragma once

#include "vivid/operator.h"

namespace vivid {

/// Adjusts Hue, Saturation, and Value of an input texture
class HSV : public TextureOperator {
public:
    std::string typeName() const override { return "HSV"; }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("hueShift", 0.0f, -180.0f, 180.0f),
            floatParam("saturation", 1.0f, 0.0f, 3.0f),
            floatParam("value", 1.0f, 0.0f, 3.0f)
        };
    }

    // Fluent API
    HSV& hueShift(float h) { hueShift_ = h; return *this; }
    HSV& saturation(float s) { saturation_ = s; return *this; }
    HSV& value(float v) { value_ = v; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    float hueShift_ = 0.0f;
    float saturation_ = 1.0f;
    float value_ = 1.0f;

    struct Constants {
        float hueShift;
        float saturation;
        float value;
        float padding;
    };
};

} // namespace vivid
