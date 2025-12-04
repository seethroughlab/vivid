#pragma once

#include "vivid/operator.h"

namespace vivid {

/// 2D transform: translate, rotate, scale
class Transform : public TextureOperator {
public:
    std::string typeName() const override { return "Transform"; }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("translateX", 0.0f, -2.0f, 2.0f),
            floatParam("translateY", 0.0f, -2.0f, 2.0f),
            floatParam("rotate", 0.0f, -360.0f, 360.0f),
            floatParam("scaleX", 1.0f, 0.01f, 10.0f),
            floatParam("scaleY", 1.0f, 0.01f, 10.0f),
            floatParam("pivotX", 0.5f, 0.0f, 1.0f),
            floatParam("pivotY", 0.5f, 0.0f, 1.0f)
        };
    }

    // Fluent API
    Transform& translate(float x, float y) { translateX_ = x; translateY_ = y; return *this; }
    Transform& rotate(float r) { rotate_ = r; return *this; }
    Transform& scale(float s) { scaleX_ = s; scaleY_ = s; return *this; }
    Transform& scale(float sx, float sy) { scaleX_ = sx; scaleY_ = sy; return *this; }
    Transform& pivot(float x, float y) { pivotX_ = x; pivotY_ = y; return *this; }

    // Process override
    void process(Context& ctx) override;

protected:
    void createPipeline(Context& ctx) override;
    void updateUniforms(Context& ctx) override;

private:
    float translateX_ = 0.0f;
    float translateY_ = 0.0f;
    float rotate_ = 0.0f;
    float scaleX_ = 1.0f;
    float scaleY_ = 1.0f;
    float pivotX_ = 0.5f;
    float pivotY_ = 0.5f;

    struct Constants {
        float translateX;
        float translateY;
        float rotate;
        float scaleX;
        float scaleY;
        float pivotX;
        float pivotY;
        float padding;
    };
};

} // namespace vivid
